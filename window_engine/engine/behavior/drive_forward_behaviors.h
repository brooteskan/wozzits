#pragma once

// engine/behavior/drive_forward_behaviors.h

#include <engine/behavior/behavior_plugin_abi.h>

namespace wz::engine::behavior
{
    // Built-in "drive_forward" behavior (the self-driving half of the runtime
    // prefab-spawn demo). Bind it to a single node and, each frame, it drives that
    // node along its OWN local forward (+Z) at the configured speed — so a spawned
    // instance visibly drives off without referencing any other node (clean under
    // spawn id-remapping). It works WITH a terrain-constrained motion component:
    // it only sets the node's motion velocity (local space), the integrator moves
    // it horizontally, and the terrain constraint keeps it on the surface.
    inline constexpr const char* kDriveForwardModule = "drive_forward";
    inline constexpr const char* kDriveForwardBehavior = "drive_forward";

    // Config keys read each frame:
    //   speed      (number) — forward units/sec along local +Z (default 6).
    //   turn_rate  (number) — yaw degrees/sec about local +Y (default 0 = straight;
    //                         a small value makes it roam rather than run to
    //                         infinity).
    inline constexpr const char* kDriveForwardSpeedConfigKey = "speed";
    inline constexpr const char* kDriveForwardTurnRateConfigKey = "turn_rate";

    inline constexpr float kDriveForwardDefaultSpeed = 6.0f;

    uint8_t register_drive_forward_behaviors(WzBehaviorPluginApi* api);
}
