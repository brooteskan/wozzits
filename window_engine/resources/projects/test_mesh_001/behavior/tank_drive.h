#pragma once
// behavior/tank_drive.h
//
// Shared differential-drive model for the tanks (player + quantum agent).
// Header-only (static inline), so each plugin DLL includes it directly -- there is
// no shared .cpp to link. Both the player tank (tread speeds from the sticks) and
// the agent tank (tread speeds from its decision) funnel through the SAME
// conversion, so an NPC drives with the exact same feel as the player.

#include <engine/behavior/behavior_module_api.h>

namespace tank_drive
{
    // Shared tuning.
    inline constexpr float kMoveSpeed = 6.0f;   // world units/sec at full throttle
    inline constexpr float kTurnSpeed = 1.8f;   // yaw rad/sec at full turn

    // A normalized drive command: throttle and turn each in roughly [-1, 1].
    struct Drive
    {
        float throttle = 0.0f;
        float turn = 0.0f;
    };

    // Differential steering: two tread speeds -> (throttle, turn). Matches the
    // original tank mapping -- the SUM of the treads drives forward, their
    // DIFFERENCE yaws, and the -0.2 folds the input range into the drive range.
    inline Drive treads_to_drive(float left_tread, float right_tread)
    {
        return Drive{
            .throttle = -0.2f * (left_tread + right_tread),
            .turn = -0.2f * (right_tread - left_tread),
        };
    }

    // Drive the entity in its LOCAL frame: forward speed along +X, yaw rate `heading`.
    inline void drive_heading_speed(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        float heading,
        float speed)
    {
        wz_self_set_motion_space(facts, event, WZ_BEHAVIOR_MOTION_SPACE_LOCAL);
        wz_self_set_linear_velocity(facts, event, speed, 0.0f, 0.0f);
        wz_self_set_angular_velocity(facts, event, 0.0f, heading, 0.0f);
    }

    // Apply a normalized drive command, scaled by the shared tuning.
    inline void apply(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        Drive drive)
    {
        drive_heading_speed(
            facts,
            event,
            drive.turn * kTurnSpeed,
            drive.throttle * kMoveSpeed);
    }

    // Convenience: tread speeds straight through to motion (treads_to_drive + apply).
    inline void drive_treads(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        float left_tread,
        float right_tread)
    {
        apply(facts, event, treads_to_drive(left_tread, right_tread));
    }
}
