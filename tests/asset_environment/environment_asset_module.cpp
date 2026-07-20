// tests/asset_environment/environment_asset_module.cpp
//
// The FrameEnvironment asset: the single CONNECTED producer of a frame's global
// environment (atmosphere, ambient light, HDRI, global directional light),
// promoted to a first-class asset so the "environment island" — an unconnected
// atmosphere node reached only through a scattered per-node scene component — is
// replaced by one node the pieces feed by edge. A pure AGGREGATOR: it copies the
// connected pieces' keys into a bundle, located by asset type so any subset
// (including none) compiles. These are device-free tests.

#include <engine/assets/environment/environment.h>
#include <engine/assets/environment_asset_module.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/environment.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/editor/asset_graph_schema_registry.h>

#include <asset/compiler.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace
{
    wz::fs::Path test_root(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }

    // A count of the compiler's input ports declaring a given name + type +
    // optional requirement. Used to prove the four aggregator ports exist and are
    // all optional, which is what makes "any subset is a valid environment" true.
    bool declares_optional_port(
        const wz::asset::AssetCompiler& compiler,
        std::string_view name,
        wz::asset::AssetType type)
    {
        return std::any_of(
            compiler.input_ports.begin(),
            compiler.input_ports.end(),
            [&](const wz::asset::InputPort& port) {
                return port.name == name && port.type == type
                    && wz::asset::input_port_optional(port);
            });
    }
}

// The type id and schema id are stable identity contracts (they persist into
// disk-cache keys) — pin them so an accidental renumber is caught.
// kAssetTypeFrameEnvironment sits next to kAssetTypeAtmosphere (2289); the schema
// is next after kAtmosphereSchema in the lighting/environment range.
TEST(EnvironmentAssetModule, TypeAndSchemaAreRegistered)
{
    EXPECT_EQ(
        static_cast<int>(wz::engine::assets::kAssetTypeFrameEnvironment), 2290);
    EXPECT_EQ(
        wz::engine::assets::kFrameEnvironmentSchema.value,
        0xF11ECA55E7001007ull);
}

// The editor-facing contract: the aggregator declares a name param and four
// OPTIONAL ports, one per frame-global piece, each accepting its own asset type.
// Optional is load-bearing — a frame that authors only fog must still compile.
TEST(EnvironmentAssetModule, DeclaresNameAndFourOptionalPorts)
{
    using namespace wz::engine::assets;

    const wz::asset::CompilerRegistry registry =
        wz::engine::editor::build_asset_graph_schema_registry();

    const wz::asset::AssetCompiler* compiler =
        registry.find(kFrameEnvironmentSchema, kAssetTypeFrameEnvironment);
    ASSERT_NE(compiler, nullptr);

    const bool declares_name = std::any_of(
        compiler->parameters.begin(),
        compiler->parameters.end(),
        [](const wz::asset::ParamDecl& decl) {
            return decl.name == "name"
                && decl.type == wz::asset::ParamType::String;
        });
    EXPECT_TRUE(declares_name);

    EXPECT_EQ(compiler->input_ports.size(), 4u);
    EXPECT_TRUE(
        declares_optional_port(*compiler, "atmosphere", kAssetTypeAtmosphere));
    EXPECT_TRUE(declares_optional_port(
        *compiler, "ambient_lighting", kAssetTypeAmbientLighting));
    EXPECT_TRUE(declares_optional_port(
        *compiler, "hdri_environment", kAssetTypeEnvironmentMap));
    EXPECT_TRUE(declares_optional_port(
        *compiler, "directional_light", kAssetTypeDirectLight));
}

// The aggregator's whole job: a referenced piece's key lands in the bundle at its
// role. Wire an atmosphere in and nothing else; the compiled EnvironmentData must
// carry that atmosphere's key and leave the other three roles empty. This proves
// the by-type dependency resolution end to end.
TEST(EnvironmentAssetModule, AtmosphereReferenceRoundTripsThroughCompile)
{
    const wz::fs::Path root = test_root("wozzits_environment_roundtrip_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto atmosphere = assets.atmospheres().create_atmosphere({
        .name = "environment/atmosphere",
        .fog_density = 0.1f,
        .fog_enabled = true,
    });
    ASSERT_TRUE(atmosphere.valid());

    const auto environment = assets.environments().create_environment({
        .name = "environment/frame",
        .atmosphere = atmosphere.output,
    });
    ASSERT_TRUE(environment.valid());

    // The environment carries exactly ONE DAG edge — the atmosphere — since the
    // other three role slots were left empty (empty keys create no edge).
    bool found_node = false;
    for (const auto& entry : assets.system().registered_assets()) {
        if (!(entry.node.key == environment.output)) {
            continue;
        }
        found_node = true;
        EXPECT_EQ(entry.node.type, kAssetTypeFrameEnvironment);
        EXPECT_EQ(entry.node.schema, kFrameEnvironmentSchema);
    }
    EXPECT_TRUE(found_node);

    ASSERT_TRUE(assets.commit());
    EXPECT_TRUE(assets.resolve_all().ok());

    const EnvironmentData* data =
        assets.environments().get_environment_data(
            assets.environments().get_environment(environment));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());

    // The atmosphere role holds the atmosphere's key; the others stay unbound.
    EXPECT_TRUE(data->atmosphere == atmosphere.output);
    EXPECT_TRUE(data->ambient_lighting == wz::asset::AssetKey{});
    EXPECT_TRUE(data->hdri_environment == wz::asset::AssetKey{});
    EXPECT_TRUE(data->directional_light == wz::asset::AssetKey{});
}

// An environment referencing nothing is VALID — it means "no authored
// environment", the parallel of an all-default Atmosphere meaning "no fog". A
// scene may carry the node before any piece is wired, and it must still resolve.
TEST(EnvironmentAssetModule, EmptyEnvironmentIsValidAndResolves)
{
    const wz::fs::Path root = test_root("wozzits_environment_empty_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const EnvironmentData defaults{};
    EXPECT_TRUE(defaults.valid());

    const auto environment = assets.environments().create_environment({
        .name = "environment/empty",
    });
    ASSERT_TRUE(environment.valid());

    ASSERT_TRUE(assets.commit());
    EXPECT_TRUE(assets.resolve_all().ok());

    const EnvironmentData* data =
        assets.environments().get_environment_data(
            assets.environments().get_environment(environment));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_TRUE(data->atmosphere == wz::asset::AssetKey{});
    EXPECT_TRUE(data->directional_light == wz::asset::AssetKey{});
}

// Which pieces an environment references is part of its identity: two
// environments with the same name but a different atmosphere must key apart, or
// an edit that repoints the atmosphere would resolve back to the cached bundle
// and the change would look dead.
TEST(EnvironmentAssetModule, DistinctAtmosphereReferenceRekeysEnvironment)
{
    const wz::fs::Path root = test_root("wozzits_environment_rekey_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const auto warm = assets.atmospheres().create_atmosphere({
        .name = "environment/warm", .fog_color = { 0.9f, 0.6f, 0.4f },
        .fog_density = 0.05f, .fog_enabled = true,
    });
    const auto cool = assets.atmospheres().create_atmosphere({
        .name = "environment/cool", .fog_color = { 0.4f, 0.6f, 0.9f },
        .fog_density = 0.05f, .fog_enabled = true,
    });
    ASSERT_TRUE(warm.valid());
    ASSERT_TRUE(cool.valid());
    EXPECT_FALSE(warm.output == cool.output);

    const auto env_warm = assets.environments().create_environment({
        .name = "environment/frame", .atmosphere = warm.output,
    });
    const auto env_cool = assets.environments().create_environment({
        .name = "environment/frame", .atmosphere = cool.output,
    });
    ASSERT_TRUE(env_warm.valid());
    ASSERT_TRUE(env_cool.valid());

    // Same name, different referenced atmosphere -> different environment key.
    EXPECT_FALSE(env_warm.output == env_cool.output);
}

// The module rejects an unnamed environment before registering anything: name is
// an identity input to the key factory, so an empty one would collide with every
// other unnamed environment referencing the same pieces.
TEST(EnvironmentAssetModule, RejectsMissingName)
{
    const wz::fs::Path root = test_root("wozzits_environment_reject_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto no_name = assets.environments().create_environment({
        .name = {},
    });
    EXPECT_FALSE(no_name.valid());
}
