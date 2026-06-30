#include "behavior_test_support.h"

#include <engine/behavior/drive_forward_behaviors.h>

namespace
{
    using wz::engine::assets::SceneBehaviorConfigValue;
    using wz::engine::assets::SceneBehaviorConfigValueKind;

    // A 1-node scene whose node (entity 0) carries the drive_forward behavior,
    // subscribed to frame.update and configured with the given speed/turn_rate.
    SceneInstance scene_with_drive_forward(double speed, double turn_rate)
    {
        SceneInstance scene{};
        scene.runtime_names = { "driver" };
        scene.runtime_to_authored = { "driver" };
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = RuntimeEntityId{ 0u },
            .component = BehaviorComponent{
                .module = kDriveForwardModule,
                .name = kDriveForwardBehavior,
                .enabled = true,
                .events = { "frame.update" },
                .channel_mask =
                    wz::engine::behavior::channel_mask_for_token(
                        "frame.update"),
                .config = {
                    SceneBehaviorConfigValue{
                        .key = kDriveForwardSpeedConfigKey,
                        .kind = SceneBehaviorConfigValueKind::Number,
                        .number_value = speed,
                    },
                    SceneBehaviorConfigValue{
                        .key = kDriveForwardTurnRateConfigKey,
                        .kind = SceneBehaviorConfigValueKind::Number,
                        .number_value = turn_rate,
                    },
                },
            },
        });
        return scene;
    }

    // A 1-node scene with no drive_forward config at all (defaults exercised).
    SceneInstance scene_with_drive_forward_defaults()
    {
        SceneInstance scene{};
        scene.runtime_names = { "driver" };
        scene.runtime_to_authored = { "driver" };
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = RuntimeEntityId{ 0u },
            .component = BehaviorComponent{
                .module = kDriveForwardModule,
                .name = kDriveForwardBehavior,
                .enabled = true,
                .events = { "frame.update" },
                .channel_mask =
                    wz::engine::behavior::channel_mask_for_token(
                        "frame.update"),
            },
        });
        return scene;
    }

    void dispatch_one_frame(
        SceneInstance& scene,
        BehaviorRegistry& registry,
        wz::engine::FrameStorage& frame_storage)
    {
        wz::engine::FrameContext frame_context{};
        BehaviorFrameContext context{
            .frame_context = &frame_context,
            .frame_storage = &frame_storage,
            .scene = &scene,
            .commands = &frame_storage.behavior_commands,
        };
        dispatch_behaviors(scene, registry, context);
    }
}

// Each frame, drive_forward emits a local-space motion velocity along +Z at the
// configured speed (plus a yaw from turn_rate), so the bound entity is set up to
// drive itself forward.
TEST(DriveForwardBehavior, EmitsLocalForwardVelocityOnFrameUpdate)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_drive_forward_behaviors));

    SceneInstance scene = scene_with_drive_forward(8.0, 12.0);

    wz::engine::FrameStorage frame_storage{};
    dispatch_one_frame(scene, registry, frame_storage);

    const auto& commands = frame_storage.behavior_commands.commands;
    ASSERT_EQ(commands.size(), 3u);

    // Motion space -> Local.
    EXPECT_EQ(commands[0].kind, BehaviorCommandKind::SetMotionSpace);
    EXPECT_EQ(commands[0].entity, 0u);
    EXPECT_FLOAT_EQ(
        commands[0].values[0],
        static_cast<float>(WZ_BEHAVIOR_MOTION_SPACE_LOCAL));

    // Linear velocity along local +Z at speed.
    EXPECT_EQ(commands[1].kind, BehaviorCommandKind::SetLinearVelocity);
    EXPECT_EQ(commands[1].entity, 0u);
    EXPECT_FLOAT_EQ(commands[1].values[0], 0.0f);
    EXPECT_FLOAT_EQ(commands[1].values[1], 0.0f);
    EXPECT_FLOAT_EQ(commands[1].values[2], 8.0f);

    // Yaw about local +Y, 12 deg/sec in radians.
    EXPECT_EQ(commands[2].kind, BehaviorCommandKind::SetAngularVelocity);
    EXPECT_EQ(commands[2].entity, 0u);
    EXPECT_FLOAT_EQ(commands[2].values[0], 0.0f);
    EXPECT_NEAR(
        commands[2].values[1],
        12.0f * 3.14159265358979323846f / 180.0f,
        1e-6f);
    EXPECT_FLOAT_EQ(commands[2].values[2], 0.0f);
}

// With no config the behavior still drives at its default speed and goes straight
// (turn_rate default 0 -> zero yaw).
TEST(DriveForwardBehavior, UsesDefaultSpeedAndDrivesStraightWithoutConfig)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_drive_forward_behaviors));

    SceneInstance scene = scene_with_drive_forward_defaults();

    wz::engine::FrameStorage frame_storage{};
    dispatch_one_frame(scene, registry, frame_storage);

    const auto& commands = frame_storage.behavior_commands.commands;
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[1].kind, BehaviorCommandKind::SetLinearVelocity);
    EXPECT_FLOAT_EQ(commands[1].values[2], kDriveForwardDefaultSpeed);
    EXPECT_EQ(commands[2].kind, BehaviorCommandKind::SetAngularVelocity);
    EXPECT_FLOAT_EQ(commands[2].values[1], 0.0f);  // straight
}

// End to end through the shared command + motion paths: dispatch drive_forward,
// apply its velocity commands, integrate, and observe the bound node's position
// advance along its facing direction over successive ticks.
TEST(DriveForwardBehavior, AdvancesBoundEntityPositionOverTicks)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_drive_forward_behaviors));

    // A real scene instance so the bound node has a transform to advance.
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "drive_forward_scene";
    wz::engine::assets::SceneNodeAsset node{};
    node.id = "driver";
    asset.nodes.push_back(std::move(node));

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    const RuntimeEntityId driver =
        result.instance.authored_to_runtime["driver"];

    // Drive straight (turn_rate 0) at speed 8 so we can predict the path: local
    // +Z with an identity rotation advances world +Z.
    result.instance.behaviors.push_back(
        SceneComponentRecord<BehaviorComponent>{
            .node = driver,
            .component = BehaviorComponent{
                .module = kDriveForwardModule,
                .name = kDriveForwardBehavior,
                .enabled = true,
                .events = { "frame.update" },
                .channel_mask =
                    wz::engine::behavior::channel_mask_for_token(
                        "frame.update"),
                .config = {
                    SceneBehaviorConfigValue{
                        .key = kDriveForwardSpeedConfigKey,
                        .kind = SceneBehaviorConfigValueKind::Number,
                        .number_value = 8.0,
                    },
                    SceneBehaviorConfigValue{
                        .key = kDriveForwardTurnRateConfigKey,
                        .kind = SceneBehaviorConfigValueKind::Number,
                        .number_value = 0.0,
                    },
                },
            },
        });

    const float dt = 0.5f;
    float last_z = 0.0f;
    for (uint32_t tick = 0; tick < 3u; ++tick) {
        wz::engine::FrameStorage frame_storage{};
        dispatch_one_frame(result.instance, registry, frame_storage);
        std::vector<RuntimeEntityId> changed;
        apply_behavior_commands(
            result.instance,
            frame_storage.behavior_commands.commands,
            &changed);
        integrate_motion(result.instance, dt, &changed);

        const auto& driver_node = wz::core::graph::node_data(
            result.instance.storage.polytree,
            driver);
        const float z = driver_node.world.m[14];
        EXPECT_GT(z, last_z)
            << "drive_forward did not advance the entity on tick " << tick;
        last_z = z;
    }

    // After 3 ticks at speed 8 * dt 0.5 the node has driven 12 units along +Z.
    EXPECT_NEAR(last_z, 8.0f * dt * 3.0f, 1e-4f);
}
