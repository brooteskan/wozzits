#include <gtest/gtest.h>

#include <engine/assets/authoring/scene_authoring.h>

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
