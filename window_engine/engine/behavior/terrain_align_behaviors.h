#pragma once

// engine/behavior/terrain_align_behaviors.h

#include <engine/behavior/behavior_plugin_abi.h>

namespace wz::engine::behavior
{
    // Built-in "terrain_align" behavior. A MODULE subscribed to the one-shot
    // "self.start" lifecycle event: when an actor first materializes (authored at
    // load, or runtime-spawned), it reads its alignment_rate config and pushes
    // that rate (deg/sec -> rad/sec) onto its own MotionComponent ONCE via
    // SetTerrainAlignmentRate. The terrain-constraint pass then rate-limits how
    // fast the actor's up-axis swings toward the surface normal -- so a tank
    // crossing a steep edge eases onto the new slope instead of snapping upright.
    //
    // It is a module (not a function behavior) on purpose: function behaviors run
    // every frame regardless of subscription, whereas modules are event-gated, so
    // this fires exactly once per actor with zero per-frame cost. The host
    // preserves the runtime rate across scene rebuilds, so it survives spawns
    // without re-firing. Self-referential only (clean under spawn id-remapping);
    // it does not move the actor -- pair it with drive_forward / a controller.
    inline constexpr const char* kTerrainAlignModule = "terrain_align";
    inline constexpr const char* kTerrainAlignBehavior = "terrain_align";

    // Config key read on self.start:
    //   alignment_rate (number) - slew rate in DEGREES/sec (default 120). <= 0
    //                             restores the instantaneous strength alignment.
    inline constexpr const char* kTerrainAlignRateConfigKey = "alignment_rate";

    inline constexpr float kTerrainAlignDefaultRateDeg = 120.0f;

    uint8_t register_terrain_align_behaviors(WzBehaviorPluginApi* api);
}
