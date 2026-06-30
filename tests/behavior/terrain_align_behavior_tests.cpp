#include "behavior_test_support.h"

#include <engine/behavior/terrain_align_behaviors.h>

namespace
{
    using wz::engine::assets::SceneBehaviorConfigValue;
    using wz::engine::assets::SceneBehaviorConfigValueKind;

    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

    // A 1-node scene whose node (entity 0) carries the terrain_align MODULE,
    // subscribed to the one-shot self.start lifecycle event, optionally
    // configured with an alignment_rate. A stable binding_id is set so the
    // once-per-binding dedup has a key.
    SceneInstance scene_with_terrain_align(bool with_config, double rate_deg)
    {
        SceneInstance scene{};
        scene.runtime_names = { "actor" };
        scene.runtime_to_authored = { "actor" };
        BehaviorComponent component{
            .binding_id = "actor/behavior/0",
            .module = kTerrainAlignModule,
            .name = kTerrainAlignBehavior,
            .enabled = true,
            .events = { "self.start" },
            .channel_mask =
                wz::engine::behavior::channel_mask_for_token("self.start"),
        };
        if (with_config) {
            component.config = {
                SceneBehaviorConfigValue{
                    .key = kTerrainAlignRateConfigKey,
                    .kind = SceneBehaviorConfigValueKind::Number,
                    .number_value = rate_deg,
                },
            };
        }
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = RuntimeEntityId{ 0u },
            .component = std::move(component),
        });
        return scene;
    }

    // Run one self.start dispatch pass; returns the produced command buffer via
    // the supplied FrameStorage.
    void run_self_start(
        SceneInstance& scene,
        BehaviorRegistry& registry,
        wz::engine::FrameStorage& frame_storage)
    {
        BehaviorFrameContext context{
            .scene = &scene,
            .behavior_state = &scene.behavior_state,
            .commands = &frame_storage.behavior_commands,
        };
        wz::engine::behavior::dispatch_self_start(scene, registry, context);
    }
}

// On self.start, terrain_align pushes the configured alignment rate (deg/sec ->
// rad/sec) onto its own entity's Motion via SetTerrainAlignmentRate -- once.
TEST(TerrainAlignBehavior, EmitsConfiguredAlignmentRateOnSelfStart)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_align_behaviors));

    SceneInstance scene = scene_with_terrain_align(true, 90.0);

    wz::engine::FrameStorage frame_storage{};
    run_self_start(scene, registry, frame_storage);

    const auto& commands = frame_storage.behavior_commands.commands;
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].kind, BehaviorCommandKind::SetTerrainAlignmentRate);
    EXPECT_EQ(commands[0].entity, 0u);
    EXPECT_NEAR(commands[0].values[0], 90.0f * kDegToRad, 1e-6f);
}

// Without config the behavior asserts its default rate.
TEST(TerrainAlignBehavior, UsesDefaultRateWithoutConfig)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_align_behaviors));

    SceneInstance scene = scene_with_terrain_align(false, 0.0);

    wz::engine::FrameStorage frame_storage{};
    run_self_start(scene, registry, frame_storage);

    const auto& commands = frame_storage.behavior_commands.commands;
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].kind, BehaviorCommandKind::SetTerrainAlignmentRate);
    EXPECT_NEAR(
        commands[0].values[0],
        kTerrainAlignDefaultRateDeg * kDegToRad,
        1e-6f);
}

// self.start is one-shot per binding: a second dispatch (e.g. after a later
// rebuild) does NOT re-fire for a binding that already started.
TEST(TerrainAlignBehavior, FiresOncePerBinding)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_terrain_align_behaviors));

    SceneInstance scene = scene_with_terrain_align(true, 90.0);

    wz::engine::FrameStorage first{};
    run_self_start(scene, registry, first);
    ASSERT_EQ(first.behavior_commands.commands.size(), 1u);

    // Second pass on the same scene: the binding is already in started_bindings.
    wz::engine::FrameStorage second{};
    run_self_start(scene, registry, second);
    EXPECT_TRUE(second.behavior_commands.commands.empty());
}
