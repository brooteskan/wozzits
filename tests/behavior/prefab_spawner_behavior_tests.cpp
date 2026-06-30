#include "behavior_test_support.h"

#include <engine/behavior/prefab_spawner_behaviors.h>

#include <cstring>

namespace
{
    using wz::engine::assets::SceneBehaviorConfigValue;
    using wz::engine::assets::SceneBehaviorConfigValueKind;

    // FNV-1a/32 over a prefab name, matching the host's register_prefab and the
    // ABI's wz_prefab_hash bit-for-bit. The behavior carries the hash in a
    // SPAWN_PREFAB command's values[0] as a float bit pattern.
    uint32_t prefab_name_hash(const char* name)
    {
        uint32_t h = 2166136261u;
        for (const char* p = name; p && *p; ++p) {
            h ^= static_cast<uint32_t>(static_cast<unsigned char>(*p));
            h *= 16777619u;
        }
        return h;
    }

    // A 1-node scene whose node (entity 0) carries the prefab_spawner behavior,
    // subscribed to frame.update and configured to spawn `prefab` on `trigger_key`
    // at the given offset.
    SceneInstance scene_with_prefab_spawner(
        const std::string& prefab,
        double trigger_key,
        double offset_x,
        double offset_y,
        double offset_z)
    {
        SceneInstance scene{};
        scene.runtime_names = { "spawner" };
        scene.runtime_to_authored = { "spawner" };
        scene.behaviors.push_back(SceneComponentRecord<BehaviorComponent>{
            .node = RuntimeEntityId{ 0u },
            .component = BehaviorComponent{
                .module = kPrefabSpawnerModule,
                .name = kPrefabSpawnerBehavior,
                .enabled = true,
                .events = { "frame.update" },
                .channel_mask =
                    wz::engine::behavior::channel_mask_for_token(
                        "frame.update"),
                .config = {
                    SceneBehaviorConfigValue{
                        .key = kPrefabSpawnerNameConfigKey,
                        .kind = SceneBehaviorConfigValueKind::String,
                        .string_value = prefab,
                    },
                    SceneBehaviorConfigValue{
                        .key = kPrefabSpawnerTriggerKeyConfigKey,
                        .kind = SceneBehaviorConfigValueKind::Number,
                        .number_value = trigger_key,
                    },
                    SceneBehaviorConfigValue{
                        .key = kPrefabSpawnerOffsetXConfigKey,
                        .kind = SceneBehaviorConfigValueKind::Number,
                        .number_value = offset_x,
                    },
                    SceneBehaviorConfigValue{
                        .key = kPrefabSpawnerOffsetYConfigKey,
                        .kind = SceneBehaviorConfigValueKind::Number,
                        .number_value = offset_y,
                    },
                    SceneBehaviorConfigValue{
                        .key = kPrefabSpawnerOffsetZConfigKey,
                        .kind = SceneBehaviorConfigValueKind::Number,
                        .number_value = offset_z,
                    },
                },
            },
        });
        return scene;
    }

    // Drive one frame.update dispatch with the given key held-down as a press
    // edge. Input reaches the behavior through frame_context->input.
    void dispatch_one_frame(
        SceneInstance& scene,
        BehaviorRegistry& registry,
        wz::engine::FrameStorage& frame_storage,
        bool press_trigger_key,
        uint32_t trigger_key)
    {
        wz::engine::FrameContext frame_context{};
        if (press_trigger_key && trigger_key < 256u) {
            frame_context.input.keyboard.pressed[trigger_key] = true;
            frame_context.input.keyboard.down[trigger_key] = true;
        }

        BehaviorFrameContext context{
            .frame_context = &frame_context,
            .frame_storage = &frame_storage,
            .scene = &scene,
            .commands = &frame_storage.behavior_commands,
        };
        dispatch_behaviors(scene, registry, context);
    }
}

// On the trigger key's press edge, prefab_spawner emits exactly one SPAWN_PREFAB
// command whose values carry the prefab name's FNV-1a/32 hash (in values[0] as a
// float bit pattern) and the configured offset (values[1..3]).
TEST(PrefabSpawnerBehavior, EmitsSpawnCommandOnTriggerKeyPress)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_prefab_spawner_behaviors));

    // Spawn "enemy_tank" on SPACE at offset (1, 2, 3).
    SceneInstance scene = scene_with_prefab_spawner(
        "enemy_tank", static_cast<double>(WZ_KEY_SPACE), 1.0, 2.0, 3.0);

    wz::engine::FrameStorage frame_storage{};
    dispatch_one_frame(
        scene, registry, frame_storage, /*press*/ true, WZ_KEY_SPACE);

    ASSERT_EQ(frame_storage.behavior_commands.commands.size(), 1u);
    const auto& command = frame_storage.behavior_commands.commands[0];
    EXPECT_EQ(command.kind, BehaviorCommandKind::SpawnPrefab);
    EXPECT_EQ(command.entity, 0u);

    uint32_t hash_bits = 0u;
    std::memcpy(&hash_bits, &command.values[0], sizeof(uint32_t));
    EXPECT_EQ(hash_bits, prefab_name_hash("enemy_tank"));
    EXPECT_FLOAT_EQ(command.values[1], 1.0f);
    EXPECT_FLOAT_EQ(command.values[2], 2.0f);
    EXPECT_FLOAT_EQ(command.values[3], 3.0f);
}

// No press -> no command (the trigger is the key's press edge, so a held key that
// was not pressed this frame does not spawn).
TEST(PrefabSpawnerBehavior, NoCommandWithoutTriggerKeyPress)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_prefab_spawner_behaviors));

    SceneInstance scene = scene_with_prefab_spawner(
        "enemy_tank", static_cast<double>(WZ_KEY_SPACE), 0.0, 0.0, 10.0);

    wz::engine::FrameStorage frame_storage{};
    dispatch_one_frame(
        scene, registry, frame_storage, /*press*/ false, WZ_KEY_SPACE);

    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());
}

// A press of a DIFFERENT key than the configured trigger does not spawn.
TEST(PrefabSpawnerBehavior, IgnoresNonTriggerKeyPress)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_prefab_spawner_behaviors));

    // Configured to trigger on W, but we press SPACE.
    SceneInstance scene = scene_with_prefab_spawner(
        "enemy_tank", static_cast<double>(WZ_KEY_W), 0.0, 0.0, 10.0);

    wz::engine::FrameStorage frame_storage{};
    dispatch_one_frame(
        scene, registry, frame_storage, /*press*/ true, WZ_KEY_SPACE);

    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());
}

// With no prefab_name configured, the behavior spawns nothing even on a press.
TEST(PrefabSpawnerBehavior, NoCommandWithoutConfiguredPrefab)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_prefab_spawner_behaviors));

    SceneInstance scene = scene_with_prefab_spawner(
        "", static_cast<double>(WZ_KEY_SPACE), 0.0, 0.0, 10.0);

    wz::engine::FrameStorage frame_storage{};
    dispatch_one_frame(
        scene, registry, frame_storage, /*press*/ true, WZ_KEY_SPACE);

    EXPECT_TRUE(frame_storage.behavior_commands.commands.empty());
}
