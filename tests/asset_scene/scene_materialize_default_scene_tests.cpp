#include "scene_authoring_materialize_test_support.h"

TEST(SceneAuthoringMaterialize, DefaultSceneHelperCreatesRootCameraScene)
{
    using namespace wz::engine::assets;

    SceneAssetData scene = make_default_scene_authoring_scene();

    ASSERT_EQ(scene.nodes.size(), 2u);
    EXPECT_EQ(scene.name, "scene_editor_scene");
    EXPECT_EQ(scene.nodes[0].id, "root");
    EXPECT_EQ(scene.nodes[1].id, "camera_01");
    ASSERT_TRUE(scene.nodes[1].parent_id.has_value());
    EXPECT_EQ(*scene.nodes[1].parent_id, "root");
    EXPECT_TRUE(scene.nodes[1].camera.has_value());
    ASSERT_TRUE(scene.defaults.active_camera_node.has_value());
    EXPECT_EQ(*scene.defaults.active_camera_node, "camera_01");
}

