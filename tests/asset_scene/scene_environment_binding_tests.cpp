// tests/asset_scene/scene_environment_binding_tests.cpp
//
// The scene half of the FrameEnvironment: authoring an Environment reference onto
// a scene node and resolving it to the bundle a renderer reads its frame-global
// pieces from. FrameEnvironment is the single CONNECTED producer that supersedes
// the standalone Atmosphere component; the app prefers it and falls back to a
// standalone atmosphere for scenes authored before it (that fallback is covered
// in scene_atmosphere_binding_tests).
//
// Environment binds BY REFERENCE, the same identity model as Atmosphere/Collision:
// the node persists an asset-graph node id (the stable intent) and the resolved
// AssetKey is disposable, re-derived by bridge_scene_environment_keys on every
// (re)bind. resolve_scene_frame_environment is the read half (key -> bundle); the
// app then resolves the bundle's atmosphere role to the fog data it hands the
// renderer. Device-free: FrameEnvironment and Atmosphere are pure params recipes.
//
// Scope note: the component-kind vocabulary, the SceneInstance projection and the
// authored/instance summaries are deliberately NOT covered here -- they are the
// editor half (a later seam) and do not yet know the Environment component.

#include "scene_asset_module_test_support.h"

#include <engine/assets/atmosphere/atmosphere.h>
#include <engine/assets/environment/environment.h>
#include <engine/assets/scene/scene_authoring_materialize.h>

#include <asset/draft.h>

#include <string>
#include <vector>

namespace
{
    using namespace wz::engine::assets;

    struct EnvironmentFixture
    {
        wz::fs::Path  root;
        wz::Logger    logger;
        wz::gpu::Device device{};
        std::optional<EngineAssetLibrary> assets;

        explicit EnvironmentFixture(const char* name)
            : root(wz::fs::join(wz::fs::temp_directory_path(), name))
        {
            wz::fs::create_directories(root);
            assets.emplace(device, logger, root);
        }
    };

    // Distinctive dials, so fog arriving through the environment can only have
    // come from THIS atmosphere.
    AtmosphereDesc test_haze(std::string name)
    {
        AtmosphereDesc desc{};
        desc.name = std::move(name);
        desc.fog_color[0] = 0.25f;
        desc.fog_color[1] = 0.50f;
        desc.fog_color[2] = 0.75f;
        desc.fog_density = 0.0325f;
        desc.fog_start_distance = 12.5f;
        desc.fog_height_falloff = 0.125f;
        desc.fog_enabled = true;
        return desc;
    }

    // Register + compile an atmosphere, then a FrameEnvironment referencing it,
    // and return both output keys. commit()/resolve_all() once covers both.
    struct ResolvedEnvironment
    {
        wz::asset::AssetKey atmosphere_key;
        wz::asset::AssetKey environment_key;
    };

    ResolvedEnvironment make_resolved_environment(
        EngineAssetLibrary& assets,
        const std::string& name)
    {
        const AtmosphereAsset atmosphere =
            assets.atmospheres().create_atmosphere(test_haze(name + "/haze"));
        EXPECT_TRUE(atmosphere.valid());

        const EnvironmentAsset environment =
            assets.environments().create_environment({
                .name = name + "/env",
                .atmosphere = atmosphere.output,
            });
        EXPECT_TRUE(environment.valid());

        EXPECT_TRUE(assets.commit());
        EXPECT_TRUE(assets.resolve_all().ok());
        return { atmosphere.output, environment.output };
    }

    wz::asset::AssetGraphDraft draft_with_node(
        wz::asset::AssetGraphDraftNodeId id,
        wz::asset::AssetKey key)
    {
        wz::asset::AssetGraphDraft draft;
        wz::asset::AssetGraphDraftNode node{};
        node.id = id;
        node.node.key = key;
        draft.nodes.push_back(node);
        wz::asset::rebuild_asset_graph_draft_indexes(draft);
        return draft;
    }
}

// ─── The no-change property ─────────────────────────────────────────────────
//
// A scene with no Environment component selects nothing here; the app then falls
// back to a standalone Atmosphere, so a pre-FrameEnvironment scene is unaffected.

TEST(SceneEnvironmentBinding, SceneWithNoEnvironmentSelectsNothing)
{
    EnvironmentFixture fx{ "wz_scene_environment_absent" };

    // An environment EXISTS in the library, just not authored onto any node.
    (void)make_resolved_environment(*fx.assets, "unreferenced");

    std::vector<SceneNodeAsset> nodes;
    nodes.push_back(make_scene_node("root"));
    nodes.push_back(make_scene_node("prop"));

    const SceneFrameEnvironment frame =
        resolve_scene_frame_environment(nodes, *fx.assets);

    EXPECT_EQ(frame.environment, nullptr);
    EXPECT_EQ(frame.source, nullptr);
    EXPECT_EQ(frame.duplicate, nullptr);
}

// ─── An authored environment resolves to its bundle, and its atmosphere role
//     resolves onward to the fog data ──────────────────────────────────────────
//
// This is the whole seam-2 chain: the node references the FrameEnvironment, the
// resolver yields the bundle, and the bundle's atmosphere role resolves to the
// same fog data a standalone atmosphere would -- which is what the app hands the
// renderer.

TEST(SceneEnvironmentBinding, AuthoredEnvironmentResolvesBundleAndFog)
{
    EnvironmentFixture fx{ "wz_scene_environment_authored" };

    const ResolvedEnvironment resolved =
        make_resolved_environment(*fx.assets, "authored");

    SceneNodeAsset weather = make_scene_node("weather");
    weather.environment =
        SceneEnvironmentAsset{ .environment_asset = resolved.environment_key };
    std::vector<SceneNodeAsset> nodes{ std::move(weather) };

    const SceneFrameEnvironment frame =
        resolve_scene_frame_environment(nodes, *fx.assets);

    ASSERT_NE(frame.environment, nullptr);
    ASSERT_NE(frame.source, nullptr);
    EXPECT_EQ(frame.source->id, "weather");
    EXPECT_EQ(frame.duplicate, nullptr);

    // The bundle names exactly the atmosphere it was built from; the other roles
    // are unbound (this environment references only an atmosphere).
    EXPECT_TRUE(frame.environment->atmosphere == resolved.atmosphere_key);
    EXPECT_TRUE(frame.environment->ambient_lighting == wz::asset::AssetKey{});
    EXPECT_TRUE(frame.environment->hdri_environment == wz::asset::AssetKey{});
    EXPECT_TRUE(frame.environment->directional_light == wz::asset::AssetKey{});

    // The app's next step: resolve the atmosphere role to the fog data. Every
    // dial must survive, since the renderer packs all of them into the view
    // constants -- a dropped field is a silent wrong look.
    const AtmosphereData* fog =
        fx.assets->atmospheres().get_atmosphere_data(
            fx.assets->atmospheres().find_atmosphere(
                AtmosphereAsset{ .output = frame.environment->atmosphere }));
    ASSERT_NE(fog, nullptr);
    EXPECT_FLOAT_EQ(fog->fog_color[0], 0.25f);
    EXPECT_FLOAT_EQ(fog->fog_color[1], 0.50f);
    EXPECT_FLOAT_EQ(fog->fog_color[2], 0.75f);
    EXPECT_FLOAT_EQ(fog->fog_density, 0.0325f);
    EXPECT_FLOAT_EQ(fog->fog_start_distance, 12.5f);
    EXPECT_FLOAT_EQ(fog->fog_height_falloff, 0.125f);
    EXPECT_TRUE(fog->fog_enabled);
}

// ─── The binding's own switch ───────────────────────────────────────────────

TEST(SceneEnvironmentBinding, DisabledEnvironmentSelectsNothing)
{
    EnvironmentFixture fx{ "wz_scene_environment_disabled" };

    const ResolvedEnvironment resolved =
        make_resolved_environment(*fx.assets, "disabled");

    SceneNodeAsset weather = make_scene_node("weather");
    weather.environment = SceneEnvironmentAsset{
        .environment_asset = resolved.environment_key,
        .enabled = false,
    };
    std::vector<SceneNodeAsset> nodes{ std::move(weather) };

    const SceneFrameEnvironment frame =
        resolve_scene_frame_environment(nodes, *fx.assets);

    // Fully resolvable asset -- only the binding is off.
    EXPECT_EQ(frame.environment, nullptr);
    EXPECT_EQ(frame.source, nullptr);
}

// ─── Frame-global state: a second one is an error, not a blend ──────────────

TEST(SceneEnvironmentBinding, SecondEnabledEnvironmentIsReportedNotBlended)
{
    EnvironmentFixture fx{ "wz_scene_environment_duplicate" };

    const ResolvedEnvironment resolved =
        make_resolved_environment(*fx.assets, "dup");

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset first = make_scene_node("weather_a");
    first.environment =
        SceneEnvironmentAsset{ .environment_asset = resolved.environment_key };
    nodes.push_back(std::move(first));

    SceneNodeAsset second = make_scene_node("weather_b");
    second.environment =
        SceneEnvironmentAsset{ .environment_asset = resolved.environment_key };
    nodes.push_back(std::move(second));

    const SceneFrameEnvironment frame =
        resolve_scene_frame_environment(nodes, *fx.assets);

    ASSERT_NE(frame.source, nullptr);
    EXPECT_EQ(frame.source->id, "weather_a");
    ASSERT_NE(frame.duplicate, nullptr);
    EXPECT_EQ(frame.duplicate->id, "weather_b");
    EXPECT_NE(frame.environment, nullptr);
}

// A node whose environment key has not been bridged yet is SELECTED (source set)
// but resolves to no bundle: the key is legitimately empty between a graph edit
// and the (re)bind that re-bridges it. The app treats this as "no fog this
// frame", NOT as a reason to fall back to a standalone atmosphere.
TEST(SceneEnvironmentBinding, UnresolvedEnvironmentKeyYieldsNoBundle)
{
    EnvironmentFixture fx{ "wz_scene_environment_unresolved" };

    SceneNodeAsset weather = make_scene_node("weather");
    weather.environment =
        SceneEnvironmentAsset{ .environment_asset_node_id = 7u };
    std::vector<SceneNodeAsset> nodes{ std::move(weather) };

    const SceneFrameEnvironment frame =
        resolve_scene_frame_environment(nodes, *fx.assets);

    ASSERT_NE(frame.source, nullptr);       // the component IS selected
    EXPECT_EQ(frame.environment, nullptr);  // it just has nothing to resolve to
}

// ─── The node-id -> key re-bridge ───────────────────────────────────────────

TEST(SceneEnvironmentBinding, BridgeResolvesAuthoredNodeIdToEnvironmentKey)
{
    const wz::asset::AssetKey graph_key{
        .content_hash = { 0xA1ULL, 0xB2ULL } };
    const wz::asset::AssetGraphDraft draft = draft_with_node(7u, graph_key);

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset weather = make_scene_node("weather");
    weather.environment =
        SceneEnvironmentAsset{ .environment_asset_node_id = 7u };
    nodes.push_back(std::move(weather));

    EXPECT_EQ(bridge_scene_environment_keys(nodes, draft), 1u);
    ASSERT_TRUE(nodes[0].environment.has_value());
    EXPECT_EQ(nodes[0].environment->environment_asset, graph_key);
    EXPECT_EQ(nodes[0].environment->environment_asset_node_id, 7u);
}

TEST(SceneEnvironmentBinding, RebindReResolvesAndClearsOnRemoval)
{
    const wz::asset::AssetKey stale_key{
        .content_hash = { 0xDEADULL, 0xBEEFULL } };
    const wz::asset::AssetKey fresh_key{
        .content_hash = { 0xF00DULL, 0xCAFEULL } };
    const wz::asset::AssetGraphDraft draft = draft_with_node(7u, fresh_key);

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset weather = make_scene_node("weather");
    weather.environment = SceneEnvironmentAsset{
        .environment_asset = stale_key,
        .environment_asset_node_id = 7u,
    };
    nodes.push_back(std::move(weather));

    // A graph swap re-resolves the same intent to the fresh key.
    EXPECT_EQ(bridge_scene_environment_keys(nodes, draft), 1u);
    ASSERT_TRUE(nodes[0].environment.has_value());
    EXPECT_EQ(nodes[0].environment->environment_asset, fresh_key);

    // The authored node is now GONE: the key must CLEAR, leaving the frame with
    // no environment rather than a stale one.
    nodes[0].environment->environment_asset_node_id = 999u;
    EXPECT_EQ(bridge_scene_environment_keys(nodes, draft), 0u);
    EXPECT_EQ(nodes[0].environment->environment_asset, wz::asset::AssetKey{});
}

TEST(SceneEnvironmentBinding, BridgeLeavesAKeyOnlyBindingUntouched)
{
    const wz::asset::AssetKey graph_key{
        .content_hash = { 0xA1ULL, 0xB2ULL } };
    const wz::asset::AssetKey direct_key{
        .content_hash = { 0x11ULL, 0x22ULL } };
    const wz::asset::AssetGraphDraft draft = draft_with_node(7u, graph_key);

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset weather = make_scene_node("weather");
    weather.environment =
        SceneEnvironmentAsset{ .environment_asset = direct_key };
    nodes.push_back(std::move(weather));

    EXPECT_EQ(bridge_scene_environment_keys(nodes, draft), 0u);
    EXPECT_EQ(nodes[0].environment->environment_asset, direct_key);
}

TEST(SceneEnvironmentBinding, BridgeIgnoresNodesWithoutTheComponent)
{
    const wz::asset::AssetGraphDraft draft = draft_with_node(
        7u, wz::asset::AssetKey{ .content_hash = { 0xA1ULL, 0xB2ULL } });

    std::vector<SceneNodeAsset> nodes;
    nodes.push_back(make_scene_node("plain"));

    EXPECT_EQ(bridge_scene_environment_keys(nodes, draft), 0u);
    EXPECT_FALSE(nodes[0].environment.has_value());
}

// ─── Persistence ────────────────────────────────────────────────────────────
//
// The node id is what PERSISTS: export writes the intent (not the disposable
// key), the parser reads it back, and the disabled switch survives.

TEST(SceneEnvironmentBinding, EnvironmentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_environment_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "environment_scene";
    SceneNodeAsset weather = make_scene_node("weather");
    // A node-id binding ALSO carrying a resolved key: export must persist the id
    // and drop the key, because the key is re-derived on load.
    weather.environment = SceneEnvironmentAsset{
        .environment_asset =
            wz::asset::AssetKey{ .content_hash = { 0xDEADULL, 0xBEEFULL } },
        .environment_asset_node_id = 21u,
        .enabled = true,
    };
    authored.nodes.push_back(std::move(weather));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"environment\""), std::string::npos);
    EXPECT_NE(exported.find("\"environment_asset_node_id\""),
        std::string::npos);
    // The disposable key is NOT persisted beside the intent it would contradict.
    EXPECT_EQ(exported.find("asset-key:"), std::string::npos);

    auto rel_path = write_scene_json(root, "environment.scene.json", exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "environment_scene",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 1u);
    ASSERT_TRUE(data->nodes[0].environment.has_value());

    const SceneEnvironmentAsset& parsed = *data->nodes[0].environment;
    EXPECT_EQ(parsed.environment_asset_node_id, 21u);
    EXPECT_TRUE(parsed.enabled);
    // Round-tripped through disk, the key is unset and awaits the bridge.
    EXPECT_EQ(parsed.environment_asset, wz::asset::AssetKey{});
}

TEST(SceneEnvironmentBinding, DisabledFlagSurvivesTheSceneJSONRoundTrip)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_environment_disabled_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "environment_off_scene";
    SceneNodeAsset weather = make_scene_node("weather");
    weather.environment = SceneEnvironmentAsset{
        .environment_asset_node_id = 21u,
        .enabled = false,
    };
    authored.nodes.push_back(std::move(weather));

    auto rel_path = write_scene_json(
        root,
        "environment_off.scene.json",
        wz::json::serialize_json(export_scene_to_json_document(authored)));

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "environment_off_scene",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 1u);
    ASSERT_TRUE(data->nodes[0].environment.has_value());
    EXPECT_FALSE(data->nodes[0].environment->enabled);
}

// The scene fingerprint must MOVE when an environment is authored/edited: the
// fingerprint gates rebuilds, so a component it ignores is one whose edits never
// take effect.
TEST(SceneEnvironmentBinding, FingerprintTracksTheEnvironmentBinding)
{
    SceneAssetData plain{};
    plain.name = "fingerprint_scene";
    plain.nodes.push_back(make_scene_node("weather"));

    SceneAssetData bound = plain;
    bound.nodes[0].environment =
        SceneEnvironmentAsset{ .environment_asset_node_id = 21u };

    SceneAssetData other_node_id = plain;
    other_node_id.nodes[0].environment =
        SceneEnvironmentAsset{ .environment_asset_node_id = 22u };

    SceneAssetData disabled = plain;
    disabled.nodes[0].environment = SceneEnvironmentAsset{
        .environment_asset_node_id = 21u,
        .enabled = false,
    };

    const auto fp = [](const SceneAssetData& s) {
        return scene_asset_fingerprint(s);
    };

    EXPECT_NE(fp(plain), fp(bound));            // presence
    EXPECT_NE(fp(bound), fp(other_node_id));    // the persisted intent
    EXPECT_NE(fp(bound), fp(disabled));         // the switch
}
