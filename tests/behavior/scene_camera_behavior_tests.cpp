#include "behavior_test_support.h"

#include <engine/behavior/scene_camera_behaviors.h>

namespace
{
    using wz::engine::assets::SceneBehaviorConfigValue;
    using wz::engine::assets::SceneBehaviorConfigValueKind;

    // A scene with a root node (entity 0) carrying the scene_camera behavior,
    // and a camera node (entity 1) named `camera_name`. The behavior is
    // subscribed to scene.loaded and configured to select that camera.
    SceneInstance scene_with_scene_camera(
        const std::string& camera_node_name,
        const std::string& configured_name)
    {
        SceneInstance scene{};
        scene.runtime_names = { "root", camera_node_name };
        scene.runtime_to_authored = { "root", "1" };
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = RuntimeEntityId{ 0u },
            .component = BehaviorComponent{
                .module = kSceneCameraModule,
                .name = "",
                .enabled = true,
                .events = { "scene.loaded" },
                .channel_mask =
                    wz::engine::behavior::channel_mask_for_token(
                        "scene.loaded"),
                .config = {
                    SceneBehaviorConfigValue{
                        .key = kSceneCameraNameConfigKey,
                        .kind = SceneBehaviorConfigValueKind::String,
                        .string_value = configured_name,
                    },
                },
            },
        });
        return scene;
    }
}

TEST(SceneCameraBehavior, SelectsConfiguredCameraOnSceneLoaded)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_scene_camera_behaviors));

    SceneInstance scene = scene_with_scene_camera("camera", "camera");

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_scene_loaded(scene, registry, context);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    EXPECT_EQ(
        frame_storage.behavior_commands.commands[0].kind,
        BehaviorCommandKind::SetActiveCamera);
    // The configured camera node is entity 1 (find_entity_by_name's index).
    EXPECT_EQ(frame_storage.behavior_commands.commands[0].entity, 1u);
}

TEST(SceneCameraBehavior, NoCommandWhenConfiguredCameraMissing)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_scene_camera_behaviors));

    // The scene's camera node is named "camera" but the behavior is configured
    // to look up "does_not_exist".
    SceneInstance scene = scene_with_scene_camera("camera", "does_not_exist");

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_scene_loaded(scene, registry, context);

    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());
}

TEST(SceneCameraBehavior, DoesNotFireOnFrameUpdate)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_scene_camera_behaviors));

    SceneInstance scene = scene_with_scene_camera("camera", "camera");

    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    // The per-frame dispatch (frame.update, input, collision, ...) must not
    // trigger the scene.loaded-only behavior.
    dispatch_behaviors(scene, registry, context);

    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());
}
