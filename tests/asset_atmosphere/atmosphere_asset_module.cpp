// tests/asset_atmosphere/atmosphere_asset_module.cpp
//
// The Atmosphere asset: the global fog a frame is viewed through, promoted to a
// first-class asset so the VIEW-frequency constants every program reads
// (RenderBindingViewHead::CameraFog) have ONE authorable producer instead of a
// per-renderable copy each look could drift from. A pure params recipe with no
// dependencies -- the sibling of Placement, not of ClipmapLatticeSchedule. These
// are device-free tests: the compiler copies authored dials into a validated
// struct, never GPU data.

#include <engine/assets/atmosphere/atmosphere.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/atmosphere.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/editor/asset_graph_schema_registry.h>

#include <asset/compiler.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace
{
    wz::fs::Path test_root(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }
}

// The type id and schema id are stable identity contracts -- pin them so an
// accidental renumber is caught. kAssetTypeAtmosphere sits next to
// kAssetTypeClipmapLatticeSchedule (201); the schema shares Placement's
// world-data range.
TEST(AtmosphereAssetModule, TypeAndSchemaAreRegistered)
{
    EXPECT_EQ(static_cast<int>(wz::engine::assets::kAssetTypeAtmosphere), 2289);
    EXPECT_EQ(
        wz::engine::assets::kAtmosphereSchema.value,
        0xF11ECA55E7001006ull);
}

// The other half of the declared-params contract: the DECLARATION itself. The
// test above proves the compiler reads the right param names; this proves the
// editor is told those names exist. An undeclared param is stored as a string,
// pb.get<T> ignores strings, and the dial goes silently dead -- so a declaration
// dropped or renamed out of step with the decoder has to fail here rather than
// in a scene where the fog quietly stops responding.
TEST(AtmosphereAssetModule, DeclaresEveryParamItReads)
{
    using namespace wz::engine::assets;

    // The device-free schema registry carries the SAME declarations the real
    // compiler registry uses, which is what the editor reads.
    const wz::asset::CompilerRegistry registry =
        wz::engine::editor::build_asset_graph_schema_registry();

    const wz::asset::AssetCompiler* compiler =
        registry.find(kAtmosphereSchema, kAssetTypeAtmosphere);
    ASSERT_NE(compiler, nullptr);

    // A pure params source: fog is authored, never derived from upstream data.
    EXPECT_TRUE(compiler->input_ports.empty());

    const auto declares =
        [compiler](std::string_view name, wz::asset::ParamType type) {
            return std::any_of(
                compiler->parameters.begin(),
                compiler->parameters.end(),
                [&](const wz::asset::ParamDecl& decl) {
                    return decl.name == name && decl.type == type;
                });
        };

    EXPECT_TRUE(declares("name", wz::asset::ParamType::String));
    // Color, not Float3 -- the editor owes this dial a swatch.
    EXPECT_TRUE(declares("fog_color", wz::asset::ParamType::Color));
    EXPECT_TRUE(declares("fog_density", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("fog_start_distance", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("fog_height_falloff", wz::asset::ParamType::Float));
    EXPECT_TRUE(declares("fog_enabled", wz::asset::ParamType::Bool));
}

// Every authored dial survives a compile. The module's typed create API is the
// programmatic half of the two-way meta read, so this pins that the compiler
// copies the desc through verbatim rather than substituting its own defaults.
TEST(AtmosphereAssetModule, AuthoredDialsRoundTripThroughCompile)
{
    const wz::fs::Path root = test_root("wozzits_atmosphere_roundtrip_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    // All six authored params, every one distinct from its default so a dropped
    // read shows up as a mismatch rather than as an accidental pass.
    const auto atmosphere = assets.atmospheres().create_atmosphere({
        .name = "atmosphere/roundtrip",
        .fog_color = { 0.25f, 0.5f, 0.75f },
        .fog_density = 0.125f,
        .fog_start_distance = 400.0f,
        .fog_height_falloff = 0.03125f,
        .fog_enabled = true,
    });
    ASSERT_TRUE(atmosphere.valid());

    // A params-only recipe: the node must carry no DAG edges at all.
    bool found_node = false;
    for (const auto& entry : assets.system().registered_assets()) {
        if (!(entry.node.key == atmosphere.output)) {
            continue;
        }
        found_node = true;
        EXPECT_TRUE(entry.dep_keys.empty());
        EXPECT_EQ(entry.node.type, kAssetTypeAtmosphere);
        EXPECT_EQ(entry.node.schema, kAtmosphereSchema);
    }
    EXPECT_TRUE(found_node);

    ASSERT_TRUE(assets.commit());
    EXPECT_TRUE(assets.resolve_all().ok());

    const AtmosphereData* data =
        assets.atmospheres().get_atmosphere_data(
            assets.atmospheres().get_atmosphere(atmosphere));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());

    EXPECT_FLOAT_EQ(data->fog_color[0], 0.25f);
    EXPECT_FLOAT_EQ(data->fog_color[1], 0.5f);
    EXPECT_FLOAT_EQ(data->fog_color[2], 0.75f);
    EXPECT_FLOAT_EQ(data->fog_density, 0.125f);
    EXPECT_FLOAT_EQ(data->fog_start_distance, 400.0f);
    EXPECT_FLOAT_EQ(data->fog_height_falloff, 0.03125f);
    EXPECT_TRUE(data->fog_enabled);
}

// The same six values through the OTHER half of the two-way read: a ParamBlock,
// which is what the editor/graph path supplies. This is the contract the
// compiler's .parameters declaration exists to keep -- an undeclared param is
// stored as a string, pb.get<T> ignores strings, and the dial goes silently
// dead. Naming each param here means a rename or a dropped declaration fails a
// test instead of quietly reverting a dial to its default.
TEST(AtmosphereAssetModule, ParamBlockDialsRoundTripThroughCompile)
{
    const wz::fs::Path root = test_root("wozzits_atmosphere_paramblock_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const float fog_color[3] = { 0.125f, 0.25f, 0.375f };

    // ParamType::Color and ParamType::Float3 share one storage alternative --
    // std::array<float,3> -- so the colour is authored as a vector, not as three
    // scalars. Floats arrive as double, the storage the Float widget writes.
    wz::asset::ParamBlock params;
    params.values["name"] = std::string{ "atmosphere/params" };
    params.values["fog_color"] =
        std::array<float, 3>{ fog_color[0], fog_color[1], fog_color[2] };
    params.values["fog_density"] = 0.0625;
    params.values["fog_start_distance"] = 250.0;
    params.values["fog_height_falloff"] = 0.015625;
    params.values["fog_enabled"] = true;

    const wz::asset::AssetKey key = make_atmosphere_key(
        "atmosphere/params",
        fog_color,
        0.0625f,
        250.0f,
        0.015625f,
        /*fog_enabled=*/true);

    wz::asset::AssetNode node{};
    node.key = key;
    node.type = kAssetTypeAtmosphere;
    node.schema = kAtmosphereSchema;
    node.stage = wz::asset::AssetStage::Source;
    node.payload = std::vector<uint8_t>{};
    node.meta = params;
    ASSERT_TRUE(assets.system().register_asset(std::move(node)));

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const AtmosphereData* data =
        assets.atmospheres().get_atmosphere_data(
            assets.atmospheres().get_atmosphere(AtmosphereAsset{ .output = key }));
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->valid());

    EXPECT_FLOAT_EQ(data->fog_color[0], 0.125f);
    EXPECT_FLOAT_EQ(data->fog_color[1], 0.25f);
    EXPECT_FLOAT_EQ(data->fog_color[2], 0.375f);
    EXPECT_FLOAT_EQ(data->fog_density, 0.0625f);
    EXPECT_FLOAT_EQ(data->fog_start_distance, 250.0f);
    EXPECT_FLOAT_EQ(data->fog_height_falloff, 0.015625f);
    EXPECT_TRUE(data->fog_enabled);
}

// An all-default atmosphere is VALID -- it means "no fog", not "unfinished".
// This is the load-bearing half of AtmosphereData::valid(): a scene that has
// never authored fog must still compile an atmosphere the view constants can be
// filled from, rather than failing resolve and leaving the block undefined.
TEST(AtmosphereAssetModule, AllDefaultAtmosphereIsValidAndMeansNoFog)
{
    const wz::fs::Path root = test_root("wozzits_atmosphere_default_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    // The struct's own defaults, with no consumer having touched them.
    const AtmosphereData defaults{};
    EXPECT_TRUE(defaults.valid());
    EXPECT_FALSE(defaults.fog_enabled);
    EXPECT_FLOAT_EQ(defaults.fog_density, 0.0f);

    // ...and the same thing end-to-end: a desc that names the asset and leaves
    // every dial alone resolves, rather than being rejected as incomplete.
    const auto atmosphere = assets.atmospheres().create_atmosphere({
        .name = "atmosphere/defaults",
    });
    ASSERT_TRUE(atmosphere.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const AtmosphereData* data =
        assets.atmospheres().get_atmosphere_data(
            assets.atmospheres().get_atmosphere(atmosphere));
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_FALSE(data->fog_enabled);
    EXPECT_FLOAT_EQ(data->fog_density, 0.0f);
    EXPECT_FLOAT_EQ(data->fog_start_distance, 0.0f);
    EXPECT_FLOAT_EQ(data->fog_height_falloff, 0.0f);
    // The default colour is a pale daylight haze, inert while the fog is off.
    EXPECT_FLOAT_EQ(data->fog_color[0], 0.6f);
    EXPECT_FLOAT_EQ(data->fog_color[1], 0.7f);
    EXPECT_FLOAT_EQ(data->fog_color[2], 0.8f);
}

// Every dial is folded into content_hash, so nudging the fog colour re-keys the
// asset and everything descending from it recompiles. Without this an edit in
// the editor would resolve straight back to the cached old atmosphere and the
// dial would look dead.
TEST(AtmosphereAssetModule, DistinctFogColorRekeysAtmosphere)
{
    const wz::fs::Path root = test_root("wozzits_atmosphere_rekey_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    // Identical name, identical dials -- only the colour differs.
    const auto warm = assets.atmospheres().create_atmosphere({
        .name = "atmosphere/rekey",
        .fog_color = { 0.9f, 0.6f, 0.4f },
        .fog_density = 0.05f,
        .fog_start_distance = 100.0f,
        .fog_height_falloff = 0.01f,
        .fog_enabled = true,
    });
    const auto cool = assets.atmospheres().create_atmosphere({
        .name = "atmosphere/rekey",
        .fog_color = { 0.4f, 0.6f, 0.9f },
        .fog_density = 0.05f,
        .fog_start_distance = 100.0f,
        .fog_height_falloff = 0.01f,
        .fog_enabled = true,
    });
    ASSERT_TRUE(warm.valid());
    ASSERT_TRUE(cool.valid());

    EXPECT_FALSE(warm.output == cool.output);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    // Two distinct keys mean two distinct compiled atmospheres, each holding the
    // colour it was authored with -- the second did not collapse onto the first.
    auto& atmospheres = assets.atmospheres();
    const AtmosphereData* warm_data =
        atmospheres.get_atmosphere_data(atmospheres.get_atmosphere(warm));
    const AtmosphereData* cool_data =
        atmospheres.get_atmosphere_data(atmospheres.get_atmosphere(cool));
    ASSERT_NE(warm_data, nullptr);
    ASSERT_NE(cool_data, nullptr);

    EXPECT_FLOAT_EQ(warm_data->fog_color[0], 0.9f);
    EXPECT_FLOAT_EQ(warm_data->fog_color[2], 0.4f);
    EXPECT_FLOAT_EQ(cool_data->fog_color[0], 0.4f);
    EXPECT_FLOAT_EQ(cool_data->fog_color[2], 0.9f);
}

// The module rejects an unnamed atmosphere before registering anything: name is
// an identity input to the key factory, so an empty one would collide with every
// other unnamed atmosphere sharing its dials.
TEST(AtmosphereAssetModule, RejectsMissingName)
{
    const wz::fs::Path root = test_root("wozzits_atmosphere_reject_tests");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    const auto no_name = assets.atmospheres().create_atmosphere({
        .name = {},
        .fog_enabled = true,
    });
    EXPECT_FALSE(no_name.valid());
}
