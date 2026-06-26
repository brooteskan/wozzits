// tests/asset_scene/scene_source_expansion_tests.cpp
//
// Coverage for the issue #213 scene-source mechanism: a host scene node
// consumes the output of a "Scene from GLB" asset (a whole Scene — a hierarchy
// of GLB-named nodes, each with a renderable) as a sub-tree.
//
// These are device-free (a default wz::gpu::Device resolves the GLB scene + its
// per-mesh renderables on the CPU). The referenced Scene asset is built via the
// REAL authoring path — SceneAssetModule::create_scene_from_glb — using the
// bundled tank1.glb fixture (nodes: body -> turret -> gun, each with a styled
// renderable). The tests exercise the load-bearing engine pieces:
//   - expand_scene_source_children: the shared expander (runtime graft + flatten)
//   - bridge_scene_source_keys:     the authored-node-id -> resolved-key bridge
//   - compute_scene_node_world_transforms: the SAME function the RHI renderer
//     uses, proving grafted children compose under the host transform
//   - instantiate_scene:            proves a grafted child is behavior-addressable
//                                   (authored_to_runtime) with a drivable transform
// The persistence/snapshot surfacing + the WozzitsApp_v1 on-device wiring are
// thin wrappers over these and are covered by construction (see the report).

#include "scene_asset_module_test_support.h"

#include <engine/assets/scene/scene_authoring_materialize.h>
// compute_scene_node_world_transforms is the renderer's parent-composition pass;
// reused here so the test asserts on exactly the world transforms the renderer
// would draw grafted children with.
#include <engine/rendering/rhi_scene_renderer.h>

#include <asset/draft.h>

namespace
{
    using namespace wz::engine::assets;

    // Build the tank1.glb Scene asset via the real per-mesh authoring path and
    // return its resolved SceneAssetData (body -> turret -> gun, renderables set).
    const SceneAssetData* build_tank_subscene(
        EngineAssetLibrary& assets, SceneAsset& out_asset)
    {
        out_asset = assets.scenes().create_scene_from_glb({
            .name = "tank_subscene",
            .path = "gltf/tank1.glb",
        });
        if (!out_asset.valid()) {
            return nullptr;
        }
        if (!assets.commit()) {
            return nullptr;
        }
        const auto report = assets.resolve_all();
        if (!report.ok()) {
            return nullptr;
        }
        return assets.scenes().get_scene_data(
            assets.scenes().get_scene(out_asset));
    }

    SceneNodeAsset make_host(const std::string& id)
    {
        SceneNodeAsset host{};
        host.id = id;
        host.name = id;
        // A non-trivial host transform so "compose under the host" is observable.
        host.local.translation[0] = 10.0f;
        host.local.translation[1] = 0.0f;
        host.local.translation[2] = -4.0f;
        return host;
    }
}

// (a) Instance expansion grafts the GLB-named children: ids are namespaced under
// the host, sub-scene parenting is preserved (roots reparent to the host), and
// each child carries its renderable.
TEST(SceneSourceExpansion, ExpandGraftsGlbNamedChildren)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };

    SceneAsset scene_asset{};
    const SceneAssetData* sub = build_tank_subscene(assets, scene_asset);
    ASSERT_NE(sub, nullptr);
    ASSERT_EQ(sub->nodes.size(), 3u);

    const SceneNodeAsset host = make_host("tank");
    const std::vector<SceneNodeAsset> children =
        expand_scene_source_children(host, *sub);

    ASSERT_EQ(children.size(), 3u);

    auto find = [&](const std::string& id) -> const SceneNodeAsset* {
        for (const auto& n : children) {
            if (n.id == id) return &n;
        }
        return nullptr;
    };

    const SceneNodeAsset* graft_body = find("tank/body");
    ASSERT_NE(graft_body, nullptr) << "expected GLB-named, host-namespaced id";
    ASSERT_TRUE(graft_body->parent_id.has_value());
    EXPECT_EQ(*graft_body->parent_id, "tank")
        << "a sub-scene root must reparent to the host";
    ASSERT_TRUE(graft_body->renderable_asset.has_value());
    EXPECT_FALSE(*graft_body->renderable_asset == wz::asset::AssetKey{});

    const SceneNodeAsset* graft_turret = find("tank/turret");
    ASSERT_NE(graft_turret, nullptr);
    ASSERT_TRUE(graft_turret->parent_id.has_value());
    EXPECT_EQ(*graft_turret->parent_id, "tank/body")
        << "non-root parenting must be preserved in the host namespace";
    ASSERT_TRUE(graft_turret->renderable_asset.has_value());

    const SceneNodeAsset* graft_gun = find("tank/gun");
    ASSERT_NE(graft_gun, nullptr);
    ASSERT_TRUE(graft_gun->parent_id.has_value());
    EXPECT_EQ(*graft_gun->parent_id, "tank/turret");

    // Grafted children never carry their own scene-source (they ARE the expansion).
    for (const auto& child : children) {
        EXPECT_FALSE(child.scene_source_node_id.has_value());
        EXPECT_FALSE(child.scene_source.has_value());
    }
}

// (b) Grafted children compose under the host transform: using the RHI
// renderer's own world-transform pass, the body's world translation equals the
// host translation (body local is carried unchanged and parented to the host).
TEST(SceneSourceExpansion, GraftedChildrenComposeUnderHost)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };

    SceneAsset scene_asset{};
    const SceneAssetData* sub = build_tank_subscene(assets, scene_asset);
    ASSERT_NE(sub, nullptr);

    // The body's authored local translation in the sub-scene (the reference the
    // composed world is measured against).
    const SceneNodeAsset* sub_body = find_scene_node(*sub, "body");
    ASSERT_NE(sub_body, nullptr);

    const SceneNodeAsset host = make_host("tank");
    std::vector<SceneNodeAsset> nodes;
    nodes.push_back(host);
    const std::vector<SceneNodeAsset> children =
        expand_scene_source_children(host, *sub);
    nodes.insert(nodes.end(), children.begin(), children.end());

    const std::vector<wz::math::Mat4> world =
        wz::engine::rendering::compute_scene_node_world_transforms(nodes);
    ASSERT_EQ(world.size(), nodes.size());

    // Locate body's index in `nodes` to read its world transform.
    std::size_t body_index = nodes.size();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].id == "tank/body") {
            body_index = i;
            break;
        }
    }
    ASSERT_LT(body_index, nodes.size());

    // world = host_world * body_local. With an axis-aligned (identity-rotation)
    // host, the body's world translation = host.translation + body.local
    // (scaled by host scale = 1). Column-major Mat4: translation in m[12..14].
    const wz::math::Mat4& body_world = world[body_index];
    EXPECT_FLOAT_EQ(
        body_world.m[12],
        host.local.translation[0] + sub_body->local.translation[0]);
    EXPECT_FLOAT_EQ(
        body_world.m[13],
        host.local.translation[1] + sub_body->local.translation[1]);
    EXPECT_FLOAT_EQ(
        body_world.m[14],
        host.local.translation[2] + sub_body->local.translation[2]);
}

// (c) A grafted child is behavior-addressable by its namespaced name and its
// transform is drivable: instantiate_scene maps "tank/turret" to a runtime
// entity, and writing that entity's local transform takes effect (read back).
TEST(SceneSourceExpansion, GraftedChildIsAddressableAndDrivable)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };

    SceneAsset scene_asset{};
    const SceneAssetData* sub = build_tank_subscene(assets, scene_asset);
    ASSERT_NE(sub, nullptr);

    const SceneNodeAsset host = make_host("tank");
    SceneAssetData live{};
    live.nodes.push_back(host);
    const std::vector<SceneNodeAsset> children =
        expand_scene_source_children(host, *sub);
    live.nodes.insert(live.nodes.end(), children.begin(), children.end());

    // Mirror WozzitsApp_v1::rebuild_behavior_scene: the behavior instance uses a
    // null renderable resolver, so renderable references are stripped from the
    // instance node copies (the polytree + behavior tables are all it needs).
    for (SceneNodeAsset& n : live.nodes) {
        n.renderable.reset();
        n.renderable_asset.reset();
        n.renderable_asset_node_id.reset();
    }

    // Instantiate the host + grafted children: the runtime SceneInstance is what
    // the behavior API resolves entities against (find_entity_by_* use the
    // authored_to_runtime map keyed by authored id).
    SceneInstantiateContext ctx{};
    ctx.logger = &logger;
    ctx.log_owner = "scene_source_expansion_test";
    SceneInstantiateResult res = instantiate_scene(live, ctx);
    ASSERT_TRUE(res.ok()) << res.error_detail;

    // Addressable: the grafted turret resolves to a runtime entity by its
    // host-namespaced authored id (this is what find_entity_by_name /
    // find_entity_by_authored_id key on; a behavior targets the turret this way).
    const auto it = res.instance.authored_to_runtime.find("tank/turret");
    ASSERT_NE(it, res.instance.authored_to_runtime.end())
        << "grafted child must be addressable by its namespaced authored id";
    const wz::scene::RuntimeEntityId turret = it->second;

    // Drivable + independent: the grafted child is a real runtime entity whose
    // own local transform (the GLB-authored one) lands in the polytree — so a
    // behavior transform command targeting THIS entity moves only the turret,
    // independently of the host/body. The sub-scene turret's authored local +Y
    // is 1.6363635 (see scene_asset_compile_tests' tank1 import).
    const SceneNodeAsset* sub_turret = find_scene_node(*sub, "turret");
    ASSERT_NE(sub_turret, nullptr);
    const wz::math::Mat4& turret_local =
        wz::core::graph::node_data(res.instance.storage.polytree, turret).local;
    EXPECT_FLOAT_EQ(turret_local.m[13], sub_turret->local.translation[1])
        << "grafted child must carry its own independent local transform";

    // The host itself is also addressable (the whole tank moves as a unit when
    // a behavior drives the host, with body/turret/gun inheriting via parenting).
    EXPECT_NE(
        res.instance.authored_to_runtime.find("tank"),
        res.instance.authored_to_runtime.end());
    // body and gun are independently addressable too.
    EXPECT_NE(
        res.instance.authored_to_runtime.find("tank/body"),
        res.instance.authored_to_runtime.end());
    EXPECT_NE(
        res.instance.authored_to_runtime.find("tank/gun"),
        res.instance.authored_to_runtime.end());
}

// (d) Flatten semantics: the expansion appended to the host's node list IS the
// persistent content (real nodes), and after flatten the host carries no
// scene-source reference (detach). This mirrors WozzitsApp_v1::flatten_scene_
// source's persistent expand + detach over the live scene_nodes_.
TEST(SceneSourceExpansion, FlattenExpandsPersistentlyAndDropsReference)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };

    SceneAsset scene_asset{};
    const SceneAssetData* sub = build_tank_subscene(assets, scene_asset);
    ASSERT_NE(sub, nullptr);

    // A host that carries a (resolved) scene-source reference, as it would after
    // bridging in instance mode.
    SceneNodeAsset host = make_host("tank");
    host.scene_source = scene_asset.output;
    host.scene_source_node_id = 42u;

    std::vector<SceneNodeAsset> scene_nodes;
    scene_nodes.push_back(host);

    // Flatten = expand persistently into the node list + drop the reference.
    // Expand from a host snapshot first (the append below may reallocate the
    // vector and invalidate a pointer into it), then re-find the host to detach.
    const SceneNodeAsset host_snapshot = *find_scene_node(scene_nodes, "tank");
    const std::vector<SceneNodeAsset> children =
        expand_scene_source_children(host_snapshot, *sub);
    for (const auto& child : children) {
        scene_nodes.push_back(child);
    }
    SceneNodeAsset* live_host = find_scene_node(scene_nodes, "tank");
    ASSERT_NE(live_host, nullptr);
    detach_scene_source(*live_host);

    // Persistent: the children are now real nodes in the list.
    EXPECT_EQ(scene_nodes.size(), 1u + sub->nodes.size());
    EXPECT_NE(find_scene_node(scene_nodes, "tank/body"), nullptr);
    EXPECT_NE(find_scene_node(scene_nodes, "tank/turret"), nullptr);
    EXPECT_NE(find_scene_node(scene_nodes, "tank/gun"), nullptr);

    // Reference dropped: the host is now fully editable, no live link.
    const SceneNodeAsset* host_after = find_scene_node(scene_nodes, "tank");
    ASSERT_NE(host_after, nullptr);
    EXPECT_FALSE(host_after->scene_source.has_value());
    EXPECT_FALSE(host_after->scene_source_node_id.has_value());
}

// bridge_scene_source_keys resolves a host's authored scene-source node id to
// the live Scene AssetKey for that draft node (mirrors the renderable bridge),
// and clears the key when the authored node id is absent from the graph.
TEST(SceneSourceExpansion, BridgeResolvesAuthoredNodeIdToSceneKey)
{
    // A draft with a single node carrying a (stand-in) resolved Scene key.
    wz::asset::AssetGraphDraft draft;
    wz::asset::AssetGraphDraftNode node{};
    node.id = 7u;
    node.node.key =
        wz::asset::AssetKey{ .content_hash = { 0x1234ULL, 0x5678ULL } };
    draft.nodes.push_back(node);
    wz::asset::rebuild_asset_graph_draft_indexes(draft);

    // Host references that authored node id; bridge should fill scene_source.
    std::vector<SceneNodeAsset> nodes;
    SceneNodeAsset host{};
    host.id = "host";
    host.scene_source_node_id = 7u;
    nodes.push_back(host);

    const uint32_t bridged = bridge_scene_source_keys(nodes, draft);
    EXPECT_EQ(bridged, 1u);
    ASSERT_TRUE(nodes[0].scene_source.has_value());
    EXPECT_TRUE(*nodes[0].scene_source == node.node.key);

    // Point at an absent authored node id: the key must clear (stop resolving the
    // previous graph's key), and nothing is bridged.
    nodes[0].scene_source_node_id = 999u;
    const uint32_t bridged_absent = bridge_scene_source_keys(nodes, draft);
    EXPECT_EQ(bridged_absent, 0u);
    EXPECT_FALSE(nodes[0].scene_source.has_value());
}
