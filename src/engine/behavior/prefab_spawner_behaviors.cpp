#include <engine/behavior/prefab_spawner_behaviors.h>

#include <engine/behavior/behavior_module_api.h>

namespace wz::engine::behavior
{
    namespace
    {
        // Per-frame: spawn the configured prefab on the configured key's press
        // edge. wz_key_pressed is single-frame (true only on the frame the key
        // goes down), so a held key spawns exactly once — no extra cooldown state
        // is needed for "one spawn per trigger". The offset is applied in the
        // spawner's local frame by the host (T = spawner world × offset).
        void prefab_spawner(
            const WzBehaviorFrameFacts* facts,
            WzBehaviorEntityId entity,
            void*)
        {
            if (!facts || !facts->write_command) {
                return;
            }

            char prefab_name[256] = {};
            uint32_t required = 0;
            if (!wz_config_string(
                    facts,
                    kPrefabSpawnerNameConfigKey,
                    prefab_name,
                    static_cast<uint32_t>(sizeof(prefab_name)),
                    &required)
                || prefab_name[0] == '\0')
            {
                return;  // no prefab configured: nothing to spawn
            }

            // Trigger key defaults to SPACE; the config value is a virtual-key
            // code (the keyboard_* arrays index by VK; see WZ_KEY_* in the ABI).
            float trigger_key = static_cast<float>(WZ_KEY_SPACE);
            (void)wz_config_float(
                facts, kPrefabSpawnerTriggerKeyConfigKey, &trigger_key);
            if (!wz_key_pressed(facts, static_cast<uint32_t>(trigger_key))) {
                return;
            }

            // Offset ahead of the spawner by default (its local +Z * 10).
            float offset_x = 0.0f;
            float offset_y = 0.0f;
            float offset_z = 10.0f;
            (void)wz_config_float(
                facts, kPrefabSpawnerOffsetXConfigKey, &offset_x);
            (void)wz_config_float(
                facts, kPrefabSpawnerOffsetYConfigKey, &offset_y);
            (void)wz_config_float(
                facts, kPrefabSpawnerOffsetZConfigKey, &offset_z);

            // Hash the prefab name the same FNV-1a/32 the host's register_prefab
            // uses (wz_prefab_hash is that hash), so the command resolves to the
            // registered scenelet. The spawner entity is the spawn anchor.
            wz_write_spawn_prefab(
                facts,
                entity,
                prefab_name,
                offset_x,
                offset_y,
                offset_z);
        }

        constexpr WzBehaviorParamDesc kPrefabSpawnerParams[] = {
            {
                kPrefabSpawnerNameConfigKey,
                "Prefab name",
                WZ_BEHAVIOR_PARAM_STRING,
                0.0,
                nullptr,
            },
            {
                kPrefabSpawnerTriggerKeyConfigKey,
                "Trigger key (VK code)",
                WZ_BEHAVIOR_PARAM_FLOAT,
                static_cast<double>(WZ_KEY_SPACE),
                nullptr,
            },
            {
                kPrefabSpawnerOffsetXConfigKey,
                "Offset X",
                WZ_BEHAVIOR_PARAM_FLOAT,
                0.0,
                nullptr,
            },
            {
                kPrefabSpawnerOffsetYConfigKey,
                "Offset Y",
                WZ_BEHAVIOR_PARAM_FLOAT,
                0.0,
                nullptr,
            },
            {
                kPrefabSpawnerOffsetZConfigKey,
                "Offset Z",
                WZ_BEHAVIOR_PARAM_FLOAT,
                10.0,
                nullptr,
            },
        };
    }

    uint8_t register_prefab_spawner_behaviors(WzBehaviorPluginApi* api)
    {
        return wz_register_behavior_with_params(
            api,
            kPrefabSpawnerModule,
            kPrefabSpawnerBehavior,
            prefab_spawner,
            kPrefabSpawnerParams,
            static_cast<uint32_t>(
                sizeof(kPrefabSpawnerParams)
                / sizeof(kPrefabSpawnerParams[0])));
    }
}
