#include <gtest/gtest.h>

#include <engine/assets/authoring/scene_authoring.h>
#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/scene/scene_json_export.h>

#include <external/json/json_parser.h>
#include <external/json/json_writer.h>

#include <logging/logger.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace wz::engine::assets;
    namespace authoring = wz::engine::assets::authoring;
}

TEST(SceneAuthoring, AddFindRename)
{
    SceneAssetData scene;
    ASSERT_TRUE(authoring::add_scene_node(scene, "root", std::nullopt, "Root"));
    ASSERT_TRUE(authoring::add_scene_node(scene, "child", std::string("root"), "Child"));
    EXPECT_FALSE(authoring::add_scene_node(scene, "child", std::nullopt));  // duplicate id
    EXPECT_FALSE(authoring::add_scene_node(scene, "", std::nullopt));       // empty id

    const SceneNodeAsset* child = find_scene_node(scene, "child");
    ASSERT_NE(child, nullptr);
    ASSERT_TRUE(child->parent_id.has_value());
    EXPECT_EQ(*child->parent_id, "root");
    EXPECT_TRUE(child->visible);

    EXPECT_TRUE(authoring::rename_scene_node(scene, "child", "Renamed"));
    EXPECT_EQ(find_scene_node(scene, "child")->name, "Renamed");
    EXPECT_FALSE(authoring::rename_scene_node(scene, "missing", "x"));
}

TEST(SceneAuthoring, AddRejectsBadParent)
{
    SceneAssetData scene;
    ASSERT_TRUE(authoring::add_scene_node(scene, "root", std::nullopt));

    EXPECT_FALSE(authoring::add_scene_node(scene, "x", std::string("missing")));
    EXPECT_FALSE(authoring::scene_node_exists(scene, "x"));

    EXPECT_FALSE(authoring::add_scene_node(scene, "self", std::string("self")));
    EXPECT_FALSE(authoring::scene_node_exists(scene, "self"));

    EXPECT_TRUE(authoring::add_scene_node(scene, "ok", std::string("root")));
}

TEST(SceneAuthoring, ReparentGuards)
{
    SceneAssetData scene;
    authoring::add_scene_node(scene, "a", std::nullopt);
    authoring::add_scene_node(scene, "b", std::string("a"));
    authoring::add_scene_node(scene, "c", std::string("b"));

    EXPECT_FALSE(authoring::reparent_scene_node(scene, "a", std::string("a")));        // self
    EXPECT_FALSE(authoring::reparent_scene_node(scene, "a", std::string("c")));        // cycle
    EXPECT_FALSE(authoring::reparent_scene_node(scene, "a", std::string("missing")));  // bad parent
    EXPECT_FALSE(authoring::reparent_scene_node(scene, "missing", std::nullopt));      // bad node

    EXPECT_TRUE(authoring::reparent_scene_node(scene, "c", std::string("a")));
    EXPECT_EQ(*find_scene_node(scene, "c")->parent_id, "a");
    EXPECT_TRUE(authoring::reparent_scene_node(scene, "c", std::nullopt));
    EXPECT_FALSE(find_scene_node(scene, "c")->parent_id.has_value());
}

TEST(SceneAuthoring, RemoveSubtreeClearsAllReferences)
{
    SceneAssetData scene;
    authoring::add_scene_node(scene, "root", std::nullopt);
    authoring::add_scene_node(scene, "terrain", std::string("root"));
    authoring::add_scene_node(scene, "refs", std::string("root"));
    authoring::add_scene_node(scene, "dlight", std::string("refs"));
    authoring::add_scene_node(scene, "alight", std::string("refs"));
    authoring::add_scene_node(scene, "env", std::string("refs"));
    authoring::add_scene_node(scene, "meshsrc", std::string("refs"));
    authoring::add_scene_node(scene, "hfsrc", std::string("refs"));
    authoring::add_scene_node(scene, "cam", std::string("refs"));

    scene.defaults.active_camera_node = "cam";

    SceneNodeAsset* terrain = find_scene_node(scene, "terrain");
    ASSERT_NE(terrain, nullptr);
    SceneTerrainRenderStyleAsset style{};
    style.directional_light_node = "dlight";
    style.ambient_light_node = "alight";
    style.environment_node = "env";
    terrain->terrain_render_style = style;
    SceneTerrainMeshSourceAsset mesh_src{};
    mesh_src.source_node = "meshsrc";
    terrain->terrain_mesh_source = mesh_src;
    SceneTerrainHeightFieldSourceAsset hf_src{};
    hf_src.source_node = "hfsrc";
    terrain->terrain_height_field_source = hf_src;

    const auto removed = authoring::remove_scene_node(scene, "refs");
    EXPECT_EQ(removed.size(), 7u);  // refs + 6 children
    EXPECT_FALSE(scene.defaults.active_camera_node.has_value());  // 'cam' was active

    const SceneNodeAsset* t = find_scene_node(scene, "terrain");
    ASSERT_NE(t, nullptr);
    ASSERT_TRUE(t->terrain_render_style.has_value());
    EXPECT_TRUE(t->terrain_render_style->directional_light_node.empty());
    EXPECT_TRUE(t->terrain_render_style->ambient_light_node.empty());
    EXPECT_TRUE(t->terrain_render_style->environment_node.empty());
    ASSERT_TRUE(t->terrain_mesh_source.has_value());
    EXPECT_TRUE(t->terrain_mesh_source->source_node.empty());
    ASSERT_TRUE(t->terrain_height_field_source.has_value());
    EXPECT_TRUE(t->terrain_height_field_source->source_node.empty());
}

TEST(SceneAuthoring, TransformVisibilityMotion)
{
    SceneAssetData scene;
    authoring::add_scene_node(scene, "n", std::nullopt);

    EXPECT_TRUE(authoring::set_node_translation(scene, "n", { 1.f, 2.f, 3.f }));
    EXPECT_TRUE(authoring::set_node_scale(scene, "n", { 4.f, 5.f, 6.f }));
    EXPECT_TRUE(authoring::set_node_rotation_quat(scene, "n", { 0.f, 0.f, 0.f, 1.f }));
    EXPECT_TRUE(authoring::set_node_visible(scene, "n", false));
    EXPECT_TRUE(authoring::set_node_motion_type(
        scene, "n", wz::scene::TransformNode::MotionType::Animated));

    const SceneNodeAsset* n = find_scene_node(scene, "n");
    EXPECT_FLOAT_EQ(n->local.translation[1], 2.f);
    EXPECT_FLOAT_EQ(n->local.scale[2], 6.f);
    EXPECT_FLOAT_EQ(n->local.rotation_quat[3], 1.f);
    EXPECT_FALSE(n->visible);
    EXPECT_EQ(n->motion_type, wz::scene::TransformNode::MotionType::Animated);

    EXPECT_FALSE(authoring::set_node_visible(scene, "missing", true));
}

// WozzitsApp_v1 keeps its live scene as a flat std::vector<SceneNodeAsset> (not
// a SceneAssetData), and set_node_transform finds the node by id in that list
// and overwrites its local transform. These cover that exact path device-free.
TEST(SceneNodeList, FindSceneNodeInVector)
{
    std::vector<SceneNodeAsset> nodes(2);
    nodes[0].id = "alpha";
    nodes[1].id = "beta";

    EXPECT_EQ(find_scene_node(nodes, "alpha"), &nodes[0]);
    EXPECT_EQ(find_scene_node(nodes, "beta"), &nodes[1]);
    EXPECT_EQ(find_scene_node(nodes, "missing"), nullptr);

    const std::vector<SceneNodeAsset>& const_nodes = nodes;
    EXPECT_EQ(find_scene_node(const_nodes, "beta"), &const_nodes[1]);
    EXPECT_EQ(find_scene_node(const_nodes, "missing"), nullptr);
}

TEST(SceneNodeList, SetTransformOverwritesOnlyTargetLocal)
{
    std::vector<SceneNodeAsset> nodes(2);
    nodes[0].id = "a";
    nodes[1].id = "b";

    const AuthoredTransform xform{
        { 1.f, 2.f, 3.f },        // translation
        { 0.f, 0.f, 0.f, 1.f },   // rotation_quat
        { 4.f, 5.f, 6.f },        // scale
    };

    SceneNodeAsset* target = find_scene_node(nodes, "b");
    ASSERT_NE(target, nullptr);
    set_transform(*target, xform);

    EXPECT_FLOAT_EQ(nodes[1].local.translation[0], 1.f);
    EXPECT_FLOAT_EQ(nodes[1].local.translation[2], 3.f);
    EXPECT_FLOAT_EQ(nodes[1].local.rotation_quat[3], 1.f);
    EXPECT_FLOAT_EQ(nodes[1].local.scale[1], 5.f);

    // The sibling keeps its defaults (identity translate, unit scale): a
    // by-id edit must touch only the target node.
    EXPECT_FLOAT_EQ(nodes[0].local.translation[0], 0.f);
    EXPECT_FLOAT_EQ(nodes[0].local.scale[0], 1.f);
}

TEST(SceneNodeList, MintIdIsACounterSkippingNamedNodes)
{
    std::vector<SceneNodeAsset> nodes(3);
    nodes[0].id = "root";   // named — ignored by the counter
    nodes[1].id = "5";      // numeric slot
    nodes[2].id = "mesh";   // named — ignored

    EXPECT_EQ(mint_scene_node_id(nodes), "6");  // max numeric (5) + 1

    std::vector<SceneNodeAsset> empty;
    EXPECT_EQ(mint_scene_node_id(empty), "1");

    std::vector<SceneNodeAsset> named(1);
    named[0].id = "root";
    EXPECT_EQ(mint_scene_node_id(named), "1");  // no numeric ids yet
}

TEST(SceneNodeList, AddChildUnderParent)
{
    std::vector<SceneNodeAsset> nodes(1);
    nodes[0].id = "root";

    const auto result = add_child_scene_node(nodes, "root");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.new_id, "1");
    EXPECT_EQ(nodes.size(), 2u);

    const SceneNodeAsset* added = find_scene_node(nodes, "1");
    ASSERT_NE(added, nullptr);
    ASSERT_TRUE(added->parent_id.has_value());
    EXPECT_EQ(*added->parent_id, "root");
}

TEST(SceneNodeList, AddChildAtTopLevel)
{
    std::vector<SceneNodeAsset> nodes;

    const auto result = add_child_scene_node(nodes, "");  // empty parent => root
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.new_id, "1");
    ASSERT_NE(find_scene_node(nodes, "1"), nullptr);
    EXPECT_FALSE(find_scene_node(nodes, "1")->parent_id.has_value());
}

TEST(SceneNodeList, AddChildRejectsMissingParent)
{
    std::vector<SceneNodeAsset> nodes;

    const auto result = add_child_scene_node(nodes, "nope");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(nodes.empty());
}

TEST(SceneNodeList, SetPropertiesUpdatesNameAndVisibility)
{
    std::vector<SceneNodeAsset> nodes(1);
    nodes[0].id = "n";
    nodes[0].visible = true;

    EXPECT_TRUE(set_scene_node_properties(nodes, "n", "Left Arm", false));
    EXPECT_EQ(nodes[0].name, "Left Arm");
    EXPECT_FALSE(nodes[0].visible);

    EXPECT_FALSE(set_scene_node_properties(nodes, "missing", "x", true));
}

TEST(SceneNodeList, ReparentMovesNodeAndRejectsCycles)
{
    std::vector<SceneNodeAsset> nodes(3);
    nodes[0].id = "a";
    nodes[1].id = "b";
    nodes[1].parent_id = "a";
    nodes[2].id = "c";
    nodes[2].parent_id = "b";

    // c (under b under a) -> under a directly
    EXPECT_TRUE(reparent_scene_node(nodes, "c", "a"));
    EXPECT_EQ(*find_scene_node(nodes, "c")->parent_id, "a");

    // detach to the top level
    EXPECT_TRUE(reparent_scene_node(nodes, "c", ""));
    EXPECT_FALSE(find_scene_node(nodes, "c")->parent_id.has_value());

    EXPECT_FALSE(reparent_scene_node(nodes, "a", "a"));        // self
    EXPECT_FALSE(reparent_scene_node(nodes, "a", "missing"));  // bad parent
    EXPECT_FALSE(reparent_scene_node(nodes, "missing", "a"));  // bad node
    EXPECT_FALSE(reparent_scene_node(nodes, "a", "b"));        // b under a -> cycle
    EXPECT_EQ(*find_scene_node(nodes, "b")->parent_id, "a");   // unchanged
}

// B3-C1 (#312): the ancestry walk must TERMINATE on a parent cycle that does not
// contain the node being reparented. It used to spin forever -- on the engine
// thread, inside the edit drain, above simulation_tick and render, with no stop
// check. Cyclic documents genuinely load (the JSON compiler copies `parent`
// verbatim and a failed instantiate is logged and tolerated), and a behavior can
// drive reparent with no editor present, so this was reachable without the editor.
//
// These assertions are secondary. The real assertion is that the test RETURNS:
// a reintroduced hang shows up as the suite timing out on this case. Kept cheap
// (3-4 nodes) so the timeout is unambiguous when it fires.
TEST(SceneNodeList, ReparentTerminatesOnPreExistingParentCycle)
{
    {   // 2-cycle a <-> b, reparent an unrelated node onto it
        std::vector<SceneNodeAsset> nodes(3);
        nodes[0].id = "a";
        nodes[0].parent_id = "b";
        nodes[1].id = "b";
        nodes[1].parent_id = "a";
        nodes[2].id = "c";

        EXPECT_FALSE(reparent_scene_node(nodes, "c", "a"));
        EXPECT_FALSE(find_scene_node(nodes, "c")->parent_id.has_value());
    }

    {   // self-parent
        std::vector<SceneNodeAsset> nodes(2);
        nodes[0].id = "q";
        nodes[0].parent_id = "q";
        nodes[1].id = "c";

        EXPECT_FALSE(reparent_scene_node(nodes, "c", "q"));
    }

    {   // 3-cycle x -> y -> z -> x
        std::vector<SceneNodeAsset> nodes(4);
        nodes[0].id = "x";
        nodes[0].parent_id = "y";
        nodes[1].id = "y";
        nodes[1].parent_id = "z";
        nodes[2].id = "z";
        nodes[2].parent_id = "x";
        nodes[3].id = "c";

        EXPECT_FALSE(reparent_scene_node(nodes, "c", "x"));
    }

    {   // A cycle elsewhere must not block a legitimate reparent in the acyclic
        // part -- the bound is a cycle detector, not a blanket refusal.
        std::vector<SceneNodeAsset> nodes(4);
        nodes[0].id = "a";
        nodes[0].parent_id = "b";
        nodes[1].id = "b";
        nodes[1].parent_id = "a";
        nodes[2].id = "p";
        nodes[3].id = "c";

        EXPECT_TRUE(reparent_scene_node(nodes, "c", "p"));
        EXPECT_EQ(*find_scene_node(nodes, "c")->parent_id, "p");
    }
}

// The same walk in the authoring module, whose only live caller is its own
// reparent. Latent today, identical shape -- fixed together so the next caller
// does not reintroduce it.
TEST(SceneAuthoring, DescendantCheckTerminatesOnParentCycle)
{
    SceneAssetData scene;
    scene.nodes.resize(3);
    scene.nodes[0].id = "a";
    scene.nodes[0].parent_id = "b";
    scene.nodes[1].id = "b";
    scene.nodes[1].parent_id = "a";
    scene.nodes[2].id = "c";

    EXPECT_FALSE(
        authoring::reparent_scene_node(scene, "c", std::optional<std::string>("a")));
}

TEST(SceneNodeList, RemoveNodeDropsSubtree)
{
    std::vector<SceneNodeAsset> nodes(4);
    nodes[0].id = "root";
    nodes[1].id = "a";
    nodes[1].parent_id = "root";
    nodes[2].id = "b";
    nodes[2].parent_id = "a";    // a's child
    nodes[3].id = "c";
    nodes[3].parent_id = "root"; // a's sibling

    const auto removed = remove_scene_node(nodes, "a");  // a + b
    EXPECT_EQ(removed.size(), 2u);
    EXPECT_EQ(find_scene_node(nodes, "a"), nullptr);
    EXPECT_EQ(find_scene_node(nodes, "b"), nullptr);
    EXPECT_NE(find_scene_node(nodes, "root"), nullptr);  // kept
    EXPECT_NE(find_scene_node(nodes, "c"), nullptr);     // sibling kept

    EXPECT_TRUE(remove_scene_node(nodes, "missing").empty());
}

TEST(SceneNodeList, SortByRenderOrderIsStableAndLowerFirst)
{
    std::vector<SceneNodeAsset> nodes(4);
    nodes[0].id = "terrain"; nodes[0].render_order = 0;
    nodes[1].id = "overlay"; nodes[1].render_order = 100;
    nodes[2].id = "sky";     nodes[2].render_order = -100;
    nodes[3].id = "stars";   nodes[3].render_order = -100;  // ties sky, authored after

    sort_scene_nodes_by_render_order(nodes);

    // Lower render_order draws first; ties keep authored array order, so sky
    // stays ahead of stars within the background layer.
    EXPECT_EQ(nodes[0].id, "sky");
    EXPECT_EQ(nodes[1].id, "stars");
    EXPECT_EQ(nodes[2].id, "terrain");
    EXPECT_EQ(nodes[3].id, "overlay");
}

TEST(SceneNodeList, SortByRenderOrderIsNoOpForAllDefault)
{
    // An all-default (0) scene must be left byte-identical: same order, so
    // existing scenes render exactly as before.
    std::vector<SceneNodeAsset> nodes(3);
    nodes[0].id = "a";
    nodes[1].id = "b";
    nodes[2].id = "c";

    sort_scene_nodes_by_render_order(nodes);

    EXPECT_EQ(nodes[0].id, "a");
    EXPECT_EQ(nodes[1].id, "b");
    EXPECT_EQ(nodes[2].id, "c");
}

TEST(SceneNodeList, FlattenPreorderGroupsSubtreesKeepingSiblingOrder)
{
    // Interleaved array (children not adjacent to parents): [root,a,b,a1,b1].
    std::vector<SceneNodeAsset> nodes(5);
    nodes[0].id = "root";
    nodes[1].id = "a";  nodes[1].parent_id = "root";
    nodes[2].id = "b";  nodes[2].parent_id = "root";
    nodes[3].id = "a1"; nodes[3].parent_id = "a";
    nodes[4].id = "b1"; nodes[4].parent_id = "b";

    flatten_scene_nodes_preorder(nodes);

    // Pre-order: each subtree contiguous, sibling order a<b preserved.
    std::vector<std::string> ids;
    for (const auto& n : nodes) ids.push_back(n.id);
    EXPECT_EQ(ids, (std::vector<std::string>{ "root", "a", "a1", "b", "b1" }));

    // Idempotent: flattening an already-pre-order array changes nothing.
    flatten_scene_nodes_preorder(nodes);
    std::vector<std::string> again;
    for (const auto& n : nodes) again.push_back(n.id);
    EXPECT_EQ(ids, again);
}

TEST(SceneNodeList, FlattenReflectsSiblingArrayOrder)
{
    std::vector<SceneNodeAsset> nodes(5);
    nodes[0].id = "root";
    nodes[1].id = "a";  nodes[1].parent_id = "root";
    nodes[2].id = "b";  nodes[2].parent_id = "root";
    nodes[3].id = "a1"; nodes[3].parent_id = "a";
    nodes[4].id = "b1"; nodes[4].parent_id = "b";

    // Reorder the draw slot so sibling 'b' precedes 'a'; the flatten follows.
    EXPECT_TRUE(reorder_scene_node(nodes, "b", "a"));
    flatten_scene_nodes_preorder(nodes);

    std::vector<std::string> ids;
    for (const auto& n : nodes) ids.push_back(n.id);
    EXPECT_EQ(ids, (std::vector<std::string>{ "root", "b", "b1", "a", "a1" }));
}

TEST(SceneNodeList, FlattenKeepsAllNodesUnderAParentCycle)
{
    // a <-> b form a parent cycle (neither is a root); c is a real root.
    std::vector<SceneNodeAsset> nodes(3);
    nodes[0].id = "a"; nodes[0].parent_id = "b";
    nodes[1].id = "b"; nodes[1].parent_id = "a";
    nodes[2].id = "c";

    flatten_scene_nodes_preorder(nodes);

    // No node is dropped; the cyclic pair is kept (falls to the end).
    EXPECT_EQ(nodes.size(), 3u);
    EXPECT_NE(find_scene_node(nodes, "a"), nullptr);
    EXPECT_NE(find_scene_node(nodes, "b"), nullptr);
    EXPECT_NE(find_scene_node(nodes, "c"), nullptr);
}

TEST(SceneNodeList, BakeDefaultsToTreeOrderThenRenderOrderOverrides)
{
    // Tree root -> [world_a, sky]; sky lifted to the Sky layer.
    std::vector<SceneNodeAsset> nodes(3);
    nodes[0].id = "root";
    nodes[1].id = "world_a"; nodes[1].parent_id = "root";
    nodes[2].id = "sky";     nodes[2].parent_id = "root";
    nodes[2].render_order = render_layer::Sky;

    bake_scene_node_draw_order(nodes);

    // Tree pre-order is root, world_a, sky; the Sky layer pulls sky to the front,
    // the rest keep tree order.
    std::vector<std::string> ids;
    for (const auto& n : nodes) ids.push_back(n.id);
    EXPECT_EQ(ids, (std::vector<std::string>{ "sky", "root", "world_a" }));
}

TEST(SceneNodeList, SetRenderOrderChangesValueAndReportsNoOp)
{
    std::vector<SceneNodeAsset> nodes(2);
    nodes[0].id = "a";
    nodes[1].id = "b";

    EXPECT_TRUE(set_scene_node_render_order(nodes, "a", render_layer::Sky));
    EXPECT_EQ(find_scene_node(nodes, "a")->render_order, render_layer::Sky);
    // Same value => no-op (returns false so the caller can skip the re-bake).
    EXPECT_FALSE(set_scene_node_render_order(nodes, "a", render_layer::Sky));
    // Missing node => false.
    EXPECT_FALSE(set_scene_node_render_order(nodes, "missing", 5));
}

TEST(SceneNodeList, ReorderMovesArraySlotWithoutTouchingHierarchy)
{
    std::vector<SceneNodeAsset> nodes(4);
    nodes[0].id = "a";
    nodes[1].id = "b";
    nodes[2].id = "c";
    nodes[2].parent_id = "a";  // c nests under a; reorder must not disturb this
    nodes[3].id = "d";

    // Move "d" to just before "b": [a, d, b, c].
    EXPECT_TRUE(reorder_scene_node(nodes, "d", "b"));
    EXPECT_EQ(nodes[0].id, "a");
    EXPECT_EQ(nodes[1].id, "d");
    EXPECT_EQ(nodes[2].id, "b");
    EXPECT_EQ(nodes[3].id, "c");
    // Nesting is by parent_id, untouched by the array move.
    EXPECT_EQ(find_scene_node(nodes, "c")->parent_id, "a");

    // Move "a" to the end (empty before_id): [d, b, c, a].
    EXPECT_TRUE(reorder_scene_node(nodes, "a", {}));
    EXPECT_EQ(nodes[0].id, "d");
    EXPECT_EQ(nodes[3].id, "a");
}

TEST(SceneNodeList, ReorderNoOpAndInvalidReturnFalse)
{
    std::vector<SceneNodeAsset> nodes(3);
    nodes[0].id = "a";
    nodes[1].id = "b";
    nodes[2].id = "c";

    EXPECT_FALSE(reorder_scene_node(nodes, "a", "a"));       // before == id
    EXPECT_FALSE(reorder_scene_node(nodes, "missing", "b")); // id absent
    EXPECT_FALSE(reorder_scene_node(nodes, "a", "missing")); // target absent
    EXPECT_FALSE(reorder_scene_node(nodes, "a", "b"));       // already before b
    EXPECT_FALSE(reorder_scene_node(nodes, "c", {}));        // already last

    // The list is untouched by every rejected reorder.
    EXPECT_EQ(nodes[0].id, "a");
    EXPECT_EQ(nodes[1].id, "b");
    EXPECT_EQ(nodes[2].id, "c");
}

TEST(SceneNodeList, ReorderWithinLayerSurvivesRenderOrderResort)
{
    // The editor path is reorder + re-sort. A within-layer reorder must stick
    // (stable sort keeps the new array order among equal render_order), and the
    // coarse layers stay separated.
    std::vector<SceneNodeAsset> nodes(3);
    nodes[0].id = "sky";   nodes[0].render_order = render_layer::Sky;
    nodes[1].id = "x";     nodes[1].render_order = render_layer::World;
    nodes[2].id = "y";     nodes[2].render_order = render_layer::World;

    // Author "y" ahead of "x" within the World layer, then re-sort.
    EXPECT_TRUE(reorder_scene_node(nodes, "y", "x"));
    sort_scene_nodes_by_render_order(nodes);

    EXPECT_EQ(nodes[0].id, "sky");  // Sky still first
    EXPECT_EQ(nodes[1].id, "y");    // reorder within World survived the sort
    EXPECT_EQ(nodes[2].id, "x");
}

TEST(SceneJsonExport, RenderOrderRoundTripsAndDefaultIsOmitted)
{
    std::vector<SceneNodeAsset> nodes(2);
    nodes[0].id = "bg";    nodes[0].render_order = -100;
    nodes[1].id = "world"; nodes[1].render_order = 0;  // default

    auto parsed = wz::json::parse_json_string(
        "{ \"schema\": \"wozzits.scene.v0\", \"name\": \"t\", \"nodes\": [] }");
    ASSERT_TRUE(parsed.ok);
    set_scene_document_nodes(parsed.document, nodes);
    const std::string out = wz::json::serialize_json(parsed.document);

    // The non-default node emits the key; a default (0) node omits it.
    EXPECT_NE(out.find("\"render_order\""), std::string::npos);

    // Re-parse the emitted document: the value survives and the default stays 0.
    auto reparsed = wz::json::parse_json_string(out);
    ASSERT_TRUE(reparsed.ok);
    wz::Logger logger;
    const auto scene =
        wz::engine::assets::internal::parse_scene_data_from_json(
            reparsed.document, logger);
    ASSERT_TRUE(scene.has_value());
    const SceneNodeAsset* bg = find_scene_node(scene->nodes, "bg");
    const SceneNodeAsset* world = find_scene_node(scene->nodes, "world");
    ASSERT_NE(bg, nullptr);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(bg->render_order, -100);
    EXPECT_EQ(world->render_order, 0);
}

TEST(SceneNodeList, MotionFilterIsAnAddableOptionalComponent)
{
    std::vector<SceneNodeAsset> nodes(1);
    nodes[0].id = "cam";

    EXPECT_TRUE(is_optional_component_kind("motion_filter"));
    EXPECT_FALSE(node_has_optional_component(nodes, "cam", "motion_filter"));

    // Add default-constructs the component (all no-op fields).
    EXPECT_TRUE(add_node_optional_component(nodes, "cam", "motion_filter"));
    ASSERT_TRUE(find_scene_node(nodes, "cam")->motion_filter.has_value());
    EXPECT_TRUE(node_has_optional_component(nodes, "cam", "motion_filter"));

    // Remove resets it to nullopt.
    EXPECT_TRUE(remove_node_optional_component(nodes, "cam", "motion_filter"));
    EXPECT_FALSE(find_scene_node(nodes, "cam")->motion_filter.has_value());
    EXPECT_FALSE(node_has_optional_component(nodes, "cam", "motion_filter"));
}

TEST(SceneNodeList, AtmosphereIsAnAddableOptionalComponent)
{
    std::vector<SceneNodeAsset> nodes(1);
    nodes[0].id = "sky";

    EXPECT_TRUE(is_optional_component_kind("atmosphere"));
    EXPECT_FALSE(node_has_optional_component(nodes, "sky", "atmosphere"));

    // Add default-constructs the component (unbound ref, enabled by default).
    EXPECT_TRUE(add_node_optional_component(nodes, "sky", "atmosphere"));
    ASSERT_TRUE(find_scene_node(nodes, "sky")->atmosphere.has_value());
    EXPECT_TRUE(node_has_optional_component(nodes, "sky", "atmosphere"));

    // Remove resets it to nullopt.
    EXPECT_TRUE(remove_node_optional_component(nodes, "sky", "atmosphere"));
    EXPECT_FALSE(find_scene_node(nodes, "sky")->atmosphere.has_value());
    EXPECT_FALSE(node_has_optional_component(nodes, "sky", "atmosphere"));
}

TEST(SceneNodeList, EnvironmentIsAnAddableOptionalComponent)
{
    std::vector<SceneNodeAsset> nodes(1);
    nodes[0].id = "sky";

    EXPECT_TRUE(is_optional_component_kind("environment"));
    EXPECT_FALSE(node_has_optional_component(nodes, "sky", "environment"));

    // Add default-constructs the component (unbound ref, enabled by default).
    EXPECT_TRUE(add_node_optional_component(nodes, "sky", "environment"));
    ASSERT_TRUE(find_scene_node(nodes, "sky")->environment.has_value());
    EXPECT_TRUE(node_has_optional_component(nodes, "sky", "environment"));

    // Remove resets it to nullopt.
    EXPECT_TRUE(remove_node_optional_component(nodes, "sky", "environment"));
    EXPECT_FALSE(find_scene_node(nodes, "sky")->environment.has_value());
    EXPECT_FALSE(node_has_optional_component(nodes, "sky", "environment"));
}

TEST(SceneJsonExport, MotionFilterRoundTripsAllChannels)
{
    std::vector<SceneNodeAsset> nodes(1);
    nodes[0].id = "cam";
    SceneMotionFilterAsset f{};
    f.translation_smoothing[0] = 0.0f;   // X pass-through (keep bob)
    f.translation_smoothing[1] = 0.25f;  // Y damped
    f.translation_smoothing[2] = 0.0f;   // Z pass-through
    f.terrain_floor = true;
    f.terrain_floor_offset = 1.5f;
    f.roll.smoothing_time = 0.4f;        // damp roll about forward
    f.pitch.limit = true;                // clamp pitch, no smoothing
    f.pitch.limit_min_degrees = -80.0f;
    f.pitch.limit_max_degrees = 80.0f;
    f.yaw.level = true;                  // yaw eases to level (exercise the flag)
    f.enabled = true;
    nodes[0].motion_filter = f;

    auto parsed = wz::json::parse_json_string(
        "{ \"schema\": \"wozzits.scene.v0\", \"name\": \"t\", \"nodes\": [] }");
    ASSERT_TRUE(parsed.ok);
    set_scene_document_nodes(parsed.document, nodes);
    const std::string out = wz::json::serialize_json(parsed.document);
    EXPECT_NE(out.find("\"motion_filter\""), std::string::npos);

    auto reparsed = wz::json::parse_json_string(out);
    ASSERT_TRUE(reparsed.ok);
    wz::Logger logger;
    const auto scene =
        wz::engine::assets::internal::parse_scene_data_from_json(
            reparsed.document, logger);
    ASSERT_TRUE(scene.has_value());
    const SceneNodeAsset* cam = find_scene_node(scene->nodes, "cam");
    ASSERT_NE(cam, nullptr);
    ASSERT_TRUE(cam->motion_filter.has_value());
    const SceneMotionFilterAsset& g = *cam->motion_filter;

    EXPECT_FLOAT_EQ(g.translation_smoothing[0], 0.0f);
    EXPECT_FLOAT_EQ(g.translation_smoothing[1], 0.25f);
    EXPECT_FLOAT_EQ(g.translation_smoothing[2], 0.0f);
    EXPECT_TRUE(g.terrain_floor);
    EXPECT_FLOAT_EQ(g.terrain_floor_offset, 1.5f);
    EXPECT_FLOAT_EQ(g.roll.smoothing_time, 0.4f);
    EXPECT_FALSE(g.roll.limit);
    EXPECT_TRUE(g.pitch.limit);
    EXPECT_FLOAT_EQ(g.pitch.limit_min_degrees, -80.0f);
    EXPECT_FLOAT_EQ(g.pitch.limit_max_degrees, 80.0f);
    EXPECT_TRUE(g.yaw.level);
    EXPECT_TRUE(g.enabled);
}

TEST(SceneJsonExport, MotionFilterAbsentByDefault)
{
    // A node with no motion_filter must not emit the block, and re-parsing a
    // plain node must leave the optional empty -- existing scenes unchanged.
    std::vector<SceneNodeAsset> nodes(1);
    nodes[0].id = "plain";

    auto parsed = wz::json::parse_json_string(
        "{ \"schema\": \"wozzits.scene.v0\", \"name\": \"t\", \"nodes\": [] }");
    ASSERT_TRUE(parsed.ok);
    set_scene_document_nodes(parsed.document, nodes);
    const std::string out = wz::json::serialize_json(parsed.document);
    EXPECT_EQ(out.find("\"motion_filter\""), std::string::npos);

    auto reparsed = wz::json::parse_json_string(out);
    ASSERT_TRUE(reparsed.ok);
    wz::Logger logger;
    const auto scene =
        wz::engine::assets::internal::parse_scene_data_from_json(
            reparsed.document, logger);
    ASSERT_TRUE(scene.has_value());
    const SceneNodeAsset* plain = find_scene_node(scene->nodes, "plain");
    ASSERT_NE(plain, nullptr);
    EXPECT_FALSE(plain->motion_filter.has_value());
}

TEST(SceneJsonExport, SetDocumentNodesReplacesNodesPreservingOthers)
{
    // Persistence patch: replace only the "nodes" array, keep everything else.
    auto parsed = wz::json::parse_json_string(
        "{ \"schema\": \"wozzits.scene.v0\", \"name\": \"t\","
        " \"nodes\": [ { \"id\": \"old\" } ],"
        " \"lights\": [ { \"node_id\": \"sun\" } ] }");
    ASSERT_TRUE(parsed.ok);

    std::vector<SceneNodeAsset> nodes(1);
    nodes[0].id = "fresh";

    set_scene_document_nodes(parsed.document, nodes);
    const std::string out = wz::json::serialize_json(parsed.document);

    EXPECT_NE(out.find("\"fresh\""), std::string::npos);  // new node written
    EXPECT_NE(out.find("\"sun\""), std::string::npos);    // non-node data kept
    EXPECT_EQ(out.find("\"old\""), std::string::npos);    // old node replaced
}

TEST(SceneAuthoring, AssignAndClearRenderable)
{
    SceneAssetData scene;
    authoring::add_scene_node(scene, "n", std::nullopt);

    wz::asset::AssetKey key{};
    key.content_hash = { 0x1234u, 0 };
    EXPECT_TRUE(authoring::assign_node_renderable(scene, "n", 7u, key));

    const SceneNodeAsset* n = find_scene_node(scene, "n");
    ASSERT_TRUE(n->renderable_asset_node_id.has_value());
    EXPECT_EQ(*n->renderable_asset_node_id, 7u);
    EXPECT_TRUE(n->renderable_asset.has_value());

    EXPECT_TRUE(authoring::clear_node_renderable(scene, "n"));
    n = find_scene_node(scene, "n");
    EXPECT_FALSE(n->renderable_asset_node_id.has_value());
    EXPECT_FALSE(n->renderable_asset.has_value());
}

TEST(SceneAuthoring, AssignRenderableRejectsInvalidIdPreservingBinding)
{
    SceneAssetData scene;
    authoring::add_scene_node(scene, "n", std::nullopt);

    wz::asset::AssetKey key{};
    key.content_hash = { 0x55u, 0 };
    ASSERT_TRUE(authoring::assign_node_renderable(scene, "n", 9u, key));

    // Sentinel id rejected; the prior binding must be untouched.
    EXPECT_FALSE(authoring::assign_node_renderable(
        scene, "n", wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE));

    const SceneNodeAsset* n = find_scene_node(scene, "n");
    ASSERT_TRUE(n->renderable_asset_node_id.has_value());
    EXPECT_EQ(*n->renderable_asset_node_id, 9u);
    EXPECT_TRUE(n->renderable_asset.has_value());
}

TEST(SceneAuthoring, SetAndRemoveComponentGeneric)
{
    SceneAssetData scene;
    authoring::add_scene_node(scene, "n", std::nullopt);

    EXPECT_TRUE(authoring::set_node_component(
        scene, "n", &SceneNodeAsset::camera, SceneCameraAsset{}));
    EXPECT_TRUE(find_scene_node(scene, "n")->camera.has_value());

    EXPECT_TRUE(authoring::remove_node_component(
        scene, "n", &SceneNodeAsset::camera));
    EXPECT_FALSE(find_scene_node(scene, "n")->camera.has_value());

    EXPECT_FALSE(authoring::set_node_component(
        scene, "missing", &SceneNodeAsset::camera, SceneCameraAsset{}));
}
