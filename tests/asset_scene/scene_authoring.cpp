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
