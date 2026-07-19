// tests/asset_scene/scene_atmosphere_binding_tests.cpp
//
// The scene half of the frame's global fog: authoring an Atmosphere asset
// (6c51cf5) onto a scene node and getting its values to the renderer's
// VIEW-frequency constants (da6c952).
//
// Atmosphere binds BY REFERENCE, the same identity model as Collision
// (#216/#217): the node persists an asset-graph node id (the stable intent) and
// the resolved AssetKey is disposable, re-derived by bridge_scene_atmosphere_keys
// on every (re)bind. resolve_scene_frame_atmosphere is the read half — key ->
// data — and is what WozzitsApp_v1::render_scene hands the renderer.
//
// Device-free: an Atmosphere is a pure params recipe (no GPU data), so a default
// wz::gpu::Device resolves one on the CPU. That keeps the load-bearing property
// below — "a scene with no atmosphere is unchanged" — observable on any machine
// rather than skipped where there is no device.

#include "scene_asset_module_test_support.h"

#include <engine/assets/atmosphere/atmosphere.h>
#include <engine/assets/scene/scene_authoring_materialize.h>

#include <asset/draft.h>

#include <string>
#include <vector>

namespace
{
    using namespace wz::engine::assets;

    // A library rooted in its own temp dir, so tests never share asset state.
    struct AtmosphereFixture
    {
        wz::fs::Path  root;
        wz::Logger    logger;
        wz::gpu::Device device{};
        std::optional<EngineAssetLibrary> assets;

        explicit AtmosphereFixture(const char* name)
            : root(wz::fs::join(wz::fs::temp_directory_path(), name))
        {
            wz::fs::create_directories(root);
            assets.emplace(device, logger, root);
        }
    };

    // Distinctive dials, so a value arriving at the renderer can only have come
    // from THIS asset — a default-shaped atmosphere would pass a laxer check.
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

    // Register + compile an atmosphere and return its output key.
    wz::asset::AssetKey make_resolved_atmosphere(
        EngineAssetLibrary& assets,
        const AtmosphereDesc& desc)
    {
        const AtmosphereAsset asset = assets.atmospheres().create_atmosphere(desc);
        EXPECT_TRUE(asset.valid());
        EXPECT_TRUE(assets.commit());
        EXPECT_TRUE(assets.resolve_all().ok());
        return asset.output;
    }

    // A draft carrying one committed node, the shape bridge_* resolves against.
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
// The whole point of the seam is that it is INVISIBLE to every scene authored
// before it existed. A scene with no atmosphere component must still hand the
// renderer nullptr, which is what keeps fog off and every existing scene
// pixel-identical. This is the test that would fail if the seam ever started
// inventing a default atmosphere.

TEST(SceneAtmosphereBinding, SceneWithNoAtmosphereYieldsNoFog)
{
    AtmosphereFixture fx{ "wz_scene_atmosphere_absent" };

    // An atmosphere EXISTS in the library — it is simply not authored onto any
    // node. Resolution must key off the scene, not off the library's contents.
    (void)make_resolved_atmosphere(*fx.assets, test_haze("unreferenced"));

    std::vector<SceneNodeAsset> nodes;
    nodes.push_back(make_scene_node("root"));
    nodes.push_back(make_scene_node("prop"));

    const SceneFrameAtmosphere frame =
        resolve_scene_frame_atmosphere(nodes, *fx.assets);

    EXPECT_EQ(frame.atmosphere, nullptr);
    EXPECT_EQ(frame.source, nullptr);
    EXPECT_EQ(frame.duplicate, nullptr);
}

// ─── An authored atmosphere reaches the renderer intact ─────────────────────

TEST(SceneAtmosphereBinding, AuthoredAtmosphereResolvesWithItsValuesIntact)
{
    AtmosphereFixture fx{ "wz_scene_atmosphere_authored" };

    const wz::asset::AssetKey key =
        make_resolved_atmosphere(*fx.assets, test_haze("authored_haze"));

    SceneNodeAsset weather = make_scene_node("weather");
    weather.atmosphere = SceneAtmosphereAsset{ .atmosphere_asset = key };
    std::vector<SceneNodeAsset> nodes{ std::move(weather) };

    const SceneFrameAtmosphere frame =
        resolve_scene_frame_atmosphere(nodes, *fx.assets);

    ASSERT_NE(frame.atmosphere, nullptr);
    ASSERT_NE(frame.source, nullptr);
    EXPECT_EQ(frame.source->id, "weather");
    EXPECT_EQ(frame.duplicate, nullptr);

    // Every dial, not just a presence check: the renderer packs all of these
    // into the view constants, so a single dropped field is a silent wrong look.
    EXPECT_FLOAT_EQ(frame.atmosphere->fog_color[0], 0.25f);
    EXPECT_FLOAT_EQ(frame.atmosphere->fog_color[1], 0.50f);
    EXPECT_FLOAT_EQ(frame.atmosphere->fog_color[2], 0.75f);
    EXPECT_FLOAT_EQ(frame.atmosphere->fog_density, 0.0325f);
    EXPECT_FLOAT_EQ(frame.atmosphere->fog_start_distance, 12.5f);
    EXPECT_FLOAT_EQ(frame.atmosphere->fog_height_falloff, 0.125f);
    EXPECT_TRUE(frame.atmosphere->fog_enabled);
}

// ─── The binding's own switch ───────────────────────────────────────────────
//
// enabled=false is the BINDING's switch, distinct from AtmosphereData::
// fog_enabled (the asset's). Switching the binding off must yield nullptr —
// "this frame has no atmosphere at all" — rather than resolving to an asset
// whose own fog_enabled happens to be true.

TEST(SceneAtmosphereBinding, DisabledAtmosphereYieldsNoFog)
{
    AtmosphereFixture fx{ "wz_scene_atmosphere_disabled" };

    const wz::asset::AssetKey key =
        make_resolved_atmosphere(*fx.assets, test_haze("disabled_haze"));

    SceneNodeAsset weather = make_scene_node("weather");
    weather.atmosphere = SceneAtmosphereAsset{
        .atmosphere_asset = key,
        .enabled = false,
    };
    std::vector<SceneNodeAsset> nodes{ std::move(weather) };

    const SceneFrameAtmosphere frame =
        resolve_scene_frame_atmosphere(nodes, *fx.assets);

    // The asset itself is fully resolvable — only the binding is off.
    EXPECT_EQ(frame.atmosphere, nullptr);
    EXPECT_EQ(frame.source, nullptr);
}

// ─── Frame-global state: a second one is an error, not a blend ──────────────

TEST(SceneAtmosphereBinding, SecondEnabledAtmosphereIsReportedNotBlended)
{
    AtmosphereFixture fx{ "wz_scene_atmosphere_duplicate" };

    const wz::asset::AssetKey key =
        make_resolved_atmosphere(*fx.assets, test_haze("dup_haze"));

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset first = make_scene_node("weather_a");
    first.atmosphere = SceneAtmosphereAsset{ .atmosphere_asset = key };
    nodes.push_back(std::move(first));

    SceneNodeAsset second = make_scene_node("weather_b");
    second.atmosphere = SceneAtmosphereAsset{ .atmosphere_asset = key };
    nodes.push_back(std::move(second));

    const SceneFrameAtmosphere frame =
        resolve_scene_frame_atmosphere(nodes, *fx.assets);

    // First wins; the second is NAMED so the caller can warn about it.
    ASSERT_NE(frame.source, nullptr);
    EXPECT_EQ(frame.source->id, "weather_a");
    ASSERT_NE(frame.duplicate, nullptr);
    EXPECT_EQ(frame.duplicate->id, "weather_b");
    EXPECT_NE(frame.atmosphere, nullptr);
}

// A DISABLED second atmosphere is not a duplicate: switching one off is how an
// author stages a replacement, and that must not read as an authoring error.
TEST(SceneAtmosphereBinding, DisabledSecondAtmosphereIsNotADuplicate)
{
    AtmosphereFixture fx{ "wz_scene_atmosphere_staged" };

    const wz::asset::AssetKey key =
        make_resolved_atmosphere(*fx.assets, test_haze("staged_haze"));

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset live = make_scene_node("weather_live");
    live.atmosphere = SceneAtmosphereAsset{ .atmosphere_asset = key };
    nodes.push_back(std::move(live));

    SceneNodeAsset staged = make_scene_node("weather_staged");
    staged.atmosphere = SceneAtmosphereAsset{
        .atmosphere_asset = key,
        .enabled = false,
    };
    nodes.push_back(std::move(staged));

    const SceneFrameAtmosphere frame =
        resolve_scene_frame_atmosphere(nodes, *fx.assets);

    ASSERT_NE(frame.source, nullptr);
    EXPECT_EQ(frame.source->id, "weather_live");
    EXPECT_EQ(frame.duplicate, nullptr);
}

// A node whose atmosphere key has not been bridged yet resolves to no fog rather
// than to an error: the key is legitimately empty between a graph edit and the
// (re)bind that re-bridges it.
TEST(SceneAtmosphereBinding, UnresolvedAtmosphereKeyYieldsNoFog)
{
    AtmosphereFixture fx{ "wz_scene_atmosphere_unresolved" };

    SceneNodeAsset weather = make_scene_node("weather");
    weather.atmosphere = SceneAtmosphereAsset{ .atmosphere_asset_node_id = 7u };
    std::vector<SceneNodeAsset> nodes{ std::move(weather) };

    const SceneFrameAtmosphere frame =
        resolve_scene_frame_atmosphere(nodes, *fx.assets);

    ASSERT_NE(frame.source, nullptr);      // the component IS selected
    EXPECT_EQ(frame.atmosphere, nullptr);  // it just has nothing to resolve to
}

// ─── The node-id -> key re-bridge ───────────────────────────────────────────
//
// The persisted intent is the node id; the key is disposable. A rebind must
// RE-RESOLVE from the graph rather than trust whatever key the component is
// carrying — that is what makes an authored atmosphere survive a graph swap
// (which mints new keys) and what makes a deleted one stop resolving instead of
// silently drawing the previous graph's fog. Mirrors
// SceneSourceExpansion.BridgeResolvesAuthoredNodeIdToSceneKey.

TEST(SceneAtmosphereBinding, BridgeResolvesAuthoredNodeIdToAtmosphereKey)
{
    const wz::asset::AssetKey graph_key{
        .content_hash = { 0xA1ULL, 0xB2ULL } };
    const wz::asset::AssetGraphDraft draft = draft_with_node(7u, graph_key);

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset weather = make_scene_node("weather");
    weather.atmosphere = SceneAtmosphereAsset{ .atmosphere_asset_node_id = 7u };
    nodes.push_back(std::move(weather));

    EXPECT_EQ(bridge_scene_atmosphere_keys(nodes, draft), 1u);
    ASSERT_TRUE(nodes[0].atmosphere.has_value());
    EXPECT_EQ(nodes[0].atmosphere->atmosphere_asset, graph_key);
    // The intent is untouched — only the disposable half moved.
    EXPECT_EQ(nodes[0].atmosphere->atmosphere_asset_node_id, 7u);
}

TEST(SceneAtmosphereBinding, RebindReResolvesRatherThanTrustingAStaleKey)
{
    // The SAME authored node id now resolves to a DIFFERENT key, as after a
    // graph swap. The component still carries the outgoing graph's key.
    const wz::asset::AssetKey stale_key{
        .content_hash = { 0xDEADULL, 0xBEEFULL } };
    const wz::asset::AssetKey fresh_key{
        .content_hash = { 0xF00DULL, 0xCAFEULL } };
    const wz::asset::AssetGraphDraft draft = draft_with_node(7u, fresh_key);

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset weather = make_scene_node("weather");
    weather.atmosphere = SceneAtmosphereAsset{
        .atmosphere_asset = stale_key,
        .atmosphere_asset_node_id = 7u,
    };
    nodes.push_back(std::move(weather));

    EXPECT_EQ(bridge_scene_atmosphere_keys(nodes, draft), 1u);
    ASSERT_TRUE(nodes[0].atmosphere.has_value());
    EXPECT_EQ(nodes[0].atmosphere->atmosphere_asset, fresh_key);

    // The authored node is now GONE from the graph. The key must CLEAR — a
    // removed atmosphere leaves the frame unfogged, never stale-fogged.
    nodes[0].atmosphere->atmosphere_asset_node_id = 999u;
    EXPECT_EQ(bridge_scene_atmosphere_keys(nodes, draft), 0u);
    EXPECT_EQ(nodes[0].atmosphere->atmosphere_asset, wz::asset::AssetKey{});
}

// A component holding ONLY a pre-resolved key (no node id) is left alone: the
// bridge resolves authored intent, and there is none to resolve here.
TEST(SceneAtmosphereBinding, BridgeLeavesAKeyOnlyBindingUntouched)
{
    const wz::asset::AssetKey graph_key{
        .content_hash = { 0xA1ULL, 0xB2ULL } };
    const wz::asset::AssetKey direct_key{
        .content_hash = { 0x11ULL, 0x22ULL } };
    const wz::asset::AssetGraphDraft draft = draft_with_node(7u, graph_key);

    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset weather = make_scene_node("weather");
    weather.atmosphere = SceneAtmosphereAsset{ .atmosphere_asset = direct_key };
    nodes.push_back(std::move(weather));

    EXPECT_EQ(bridge_scene_atmosphere_keys(nodes, draft), 0u);
    EXPECT_EQ(nodes[0].atmosphere->atmosphere_asset, direct_key);
}

// The bridge must not fabricate a component on a node that has none.
TEST(SceneAtmosphereBinding, BridgeIgnoresNodesWithoutTheComponent)
{
    const wz::asset::AssetGraphDraft draft = draft_with_node(
        7u, wz::asset::AssetKey{ .content_hash = { 0xA1ULL, 0xB2ULL } });

    std::vector<SceneNodeAsset> nodes;
    nodes.push_back(make_scene_node("plain"));

    EXPECT_EQ(bridge_scene_atmosphere_keys(nodes, draft), 0u);
    EXPECT_FALSE(nodes[0].atmosphere.has_value());
}

// ─── Persistence + runtime projection ───────────────────────────────────────
//
// The node id is what PERSISTS. Export must write the intent (not the disposable
// key), the parser must read it back, and instantiate_scene must project the
// component into the SceneInstance the way collision does.

TEST(SceneAtmosphereBinding, AtmosphereRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_atmosphere_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "atmosphere_scene";
    SceneNodeAsset weather = make_scene_node("weather");
    // A node-id binding ALSO carrying a resolved key: export must persist the
    // id and drop the key, because the key is re-derived on load.
    weather.atmosphere = SceneAtmosphereAsset{
        .atmosphere_asset =
            wz::asset::AssetKey{ .content_hash = { 0xDEADULL, 0xBEEFULL } },
        .atmosphere_asset_node_id = 21u,
        .enabled = true,
    };
    authored.nodes.push_back(std::move(weather));

    // The component is visible to the authored-component vocabulary.
    const auto kinds = authored_components_for_node(authored.nodes[0]);
    EXPECT_EQ(std::count(
        kinds.begin(),
        kinds.end(),
        wz::scene::SceneAuthoredComponentKind::Atmosphere), 1);
    EXPECT_EQ(summarize_authored_scene_components(authored).atmospheres, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"atmosphere\""), std::string::npos);
    EXPECT_NE(exported.find("\"atmosphere_asset_node_id\""), std::string::npos);
    // The disposable key is NOT persisted beside the intent it would contradict.
    EXPECT_EQ(exported.find("asset-key:"), std::string::npos);

    auto rel_path = write_scene_json(root, "atmosphere.scene.json", exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "atmosphere_scene",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 1u);
    ASSERT_TRUE(data->nodes[0].atmosphere.has_value());

    const SceneAtmosphereAsset& parsed = *data->nodes[0].atmosphere;
    EXPECT_EQ(parsed.atmosphere_asset_node_id, 21u);
    EXPECT_TRUE(parsed.enabled);
    // Round-tripped through disk, the key is unset and awaits the bridge.
    EXPECT_EQ(parsed.atmosphere_asset, wz::asset::AssetKey{});
}

TEST(SceneAtmosphereBinding, DisabledFlagSurvivesTheSceneJSONRoundTrip)
{
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wz_scene_atmosphere_disabled_roundtrip");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "atmosphere_off_scene";
    SceneNodeAsset weather = make_scene_node("weather");
    weather.atmosphere = SceneAtmosphereAsset{
        .atmosphere_asset_node_id = 21u,
        .enabled = false,
    };
    authored.nodes.push_back(std::move(weather));

    auto rel_path = write_scene_json(
        root,
        "atmosphere_off.scene.json",
        wz::json::serialize_json(export_scene_to_json_document(authored)));

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "atmosphere_off_scene",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 1u);
    ASSERT_TRUE(data->nodes[0].atmosphere.has_value());
    // A default-true flag is exactly the one a round trip silently resets.
    EXPECT_FALSE(data->nodes[0].atmosphere->enabled);
}

TEST(SceneAtmosphereBinding, AtmosphereMaterializesIntoTheSceneInstance)
{
    const wz::asset::AssetKey key{ .content_hash = { 0xA1ULL, 0xB2ULL } };

    SceneAssetData authored{};
    authored.name = "atmosphere_instance_scene";
    SceneNodeAsset weather = make_scene_node("weather");
    weather.atmosphere = SceneAtmosphereAsset{
        .atmosphere_asset = key,
        .enabled = true,
    };
    authored.nodes.push_back(std::move(weather));

    auto result = instantiate_scene(authored);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    ASSERT_EQ(result.instance.atmospheres.size(), 1u);
    ASSERT_TRUE(result.instance.authored_to_runtime.contains("weather"));
    EXPECT_EQ(
        result.instance.atmospheres[0].node,
        result.instance.authored_to_runtime["weather"]);
    EXPECT_EQ(
        result.instance.atmospheres[0].component.atmosphere_asset, key);
    EXPECT_TRUE(result.instance.atmospheres[0].component.enabled);

    EXPECT_EQ(
        summarize_scene_instance_components(result.instance).atmospheres, 1u);
}

// A scene with no atmosphere projects no record — the runtime-side half of the
// no-change property.
TEST(SceneAtmosphereBinding, SceneWithNoAtmosphereProjectsNoRecord)
{
    SceneAssetData authored{};
    authored.name = "plain_scene";
    authored.nodes.push_back(make_scene_node("root"));

    auto result = instantiate_scene(authored);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    EXPECT_TRUE(result.instance.atmospheres.empty());
    EXPECT_EQ(
        summarize_scene_instance_components(result.instance).atmospheres, 0u);
    EXPECT_EQ(summarize_authored_scene_components(authored).atmospheres, 0u);
}

// The scene fingerprint must MOVE when an atmosphere is authored/edited:
// the fingerprint gates rebuilds, so a component it ignores is a component whose
// edits never take effect.
TEST(SceneAtmosphereBinding, FingerprintTracksTheAtmosphereBinding)
{
    SceneAssetData plain{};
    plain.name = "fingerprint_scene";
    plain.nodes.push_back(make_scene_node("weather"));

    SceneAssetData bound = plain;
    bound.nodes[0].atmosphere =
        SceneAtmosphereAsset{ .atmosphere_asset_node_id = 21u };

    SceneAssetData other_node_id = plain;
    other_node_id.nodes[0].atmosphere =
        SceneAtmosphereAsset{ .atmosphere_asset_node_id = 22u };

    SceneAssetData disabled = plain;
    disabled.nodes[0].atmosphere = SceneAtmosphereAsset{
        .atmosphere_asset_node_id = 21u,
        .enabled = false,
    };

    const auto fp = [](const SceneAssetData& s) {
        return scene_asset_fingerprint(s);
    };

    EXPECT_NE(fp(plain), fp(bound));            // presence
    EXPECT_NE(fp(bound), fp(other_node_id));    // the persisted intent
    EXPECT_NE(fp(bound), fp(disabled));         // the switch
}
