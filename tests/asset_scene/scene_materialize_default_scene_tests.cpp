#include "scene_authoring_materialize_test_support.h"

#include <engine/assets/scene/scene_instance.h>

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

TEST(SceneAuthoringMaterialize, EventTriggerMaterializesAsAuthoringPassThrough)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_event_trigger_materialize_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "event_trigger_materialize";

    SceneNodeAsset node{};
    node.id = "trigger";
    node.event_trigger = SceneEventTriggerAsset{
        .event = "gpu.compute.request",
    };
    scene.nodes.push_back(std::move(node));

    ASSERT_TRUE(has_asset_authoring_recipes(scene.nodes[0]));
    EXPECT_FALSE(has_runtime_relevant_components(scene.nodes[0]));

    const auto report = materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;

    ASSERT_EQ(scene.nodes.size(), 1u);
    ASSERT_TRUE(scene.nodes[0].event_trigger.has_value());
    EXPECT_EQ(scene.nodes[0].event_trigger->event, "gpu.compute.request");
    EXPECT_TRUE(report.renderables_to_realize.empty());

    auto instance_result = instantiate_scene(scene);
    ASSERT_TRUE(instance_result.ok()) << instance_result.error_detail;
    const auto runtime_summary =
        summarize_scene_instance_components(instance_result.instance);
    EXPECT_EQ(runtime_summary.event_listeners, 0u);
}

