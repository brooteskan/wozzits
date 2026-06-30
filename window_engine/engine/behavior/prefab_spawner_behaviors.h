#pragma once

// engine/behavior/prefab_spawner_behaviors.h

#include <engine/behavior/behavior_plugin_abi.h>

namespace wz::engine::behavior
{
    // Built-in "prefab_spawner" behavior (the runtime-spawning demo). Bind it to a
    // node and, on a configured key press, it emits a SPAWN_PREFAB command for the
    // prefab named in its config — the host grafts that prefab (a registered
    // scenelet) at this node's transform plus the configured offset. One spawn per
    // key press (the press edge is single-frame), so a held key does not spam.
    inline constexpr const char* kPrefabSpawnerModule = "prefab_spawner";
    inline constexpr const char* kPrefabSpawnerBehavior = "prefab_spawner";

    // Config keys read each trigger:
    //   prefab_name  (string)  — the scenelet/prefab to spawn (required).
    //   trigger_key  (number)  — virtual-key code to spawn on (default SPACE).
    //   offset_x/y/z (numbers) — spawn offset in the spawner's frame
    //                            (default (0, 0, 10) = ahead of the spawner).
    inline constexpr const char* kPrefabSpawnerNameConfigKey = "prefab_name";
    inline constexpr const char* kPrefabSpawnerTriggerKeyConfigKey =
        "trigger_key";
    inline constexpr const char* kPrefabSpawnerOffsetXConfigKey = "offset_x";
    inline constexpr const char* kPrefabSpawnerOffsetYConfigKey = "offset_y";
    inline constexpr const char* kPrefabSpawnerOffsetZConfigKey = "offset_z";

    uint8_t register_prefab_spawner_behaviors(WzBehaviorPluginApi* api);
}
