#pragma once
// behavior/tank_drive.h
//
// Shared differential-drive model for the tanks (player + quantum agent).
// Header-only (static inline), so each plugin DLL includes it directly -- there is
// no shared .cpp to link. Both the player tank (tread speeds from the sticks) and
// the agent tank (tread speeds from its decision) funnel through the SAME
// conversion, so an NPC drives with the exact same feel as the player.

#include <engine/behavior/behavior_module_api.h>

#include <math.h>   // atan2f

namespace tank_drive
{
    // Shared tuning.
    inline constexpr float kMoveSpeed = 6.0f;   // world units/sec at full throttle
    inline constexpr float kTurnSpeed = 1.8f;   // yaw rad/sec at full turn
    // 90 deg either side of forward: the turret's frontal arc.
    inline constexpr float kTurretHalfArc = 1.57079633f;  // PI/2

    // Yaw (rad) of the hull's TRUE FORWARD relative to its local +X drive column.
    // bearing_to / drive_heading_speed treat +X as forward, but a model whose nose
    // (and turret rest, and gun) is built facing the other way is forward = +X + PI
    // -- which reads in-game as "pursues the player but drives backward, gun
    // trailing". This ONE knob rotates facing (face_yaw_rate), driving
    // (drive_facing) and the turret's zero (face_bearing_to) together, so retune it
    // -- 0, +/-PI/2, or PI -- until the tank leads with its nose. tank1.glb faces
    // the -X way, so: PI. (The player tank uses tread-drive and ignores this.)
    inline constexpr float kBodyForward = 3.14159265358979f;

    // Body + turret state shared by every tank. FORWARD is the hull's local +X
    // -- the axis seek_yaw_rate turns and drive_heading_speed drives. Author the
    // hull mesh nose along +X, and the turret as a CHILD node whose barrel points
    // along the turret's own +X at rest (so turret_yaw == 0 means "dead ahead").
    struct Chassis {
        WzBehaviorEntityId turret = WZ_INVALID_BEHAVIOR_ENTITY;  // child node
        float turret_yaw = 0.0f;   // rad, RELATIVE to hull forward; 0 = ahead
    };

    // A normalized drive command: throttle and turn each in roughly [-1, 1].
    struct Drive
    {
        float throttle = 0.0f;
        float turn = 0.0f;
    };

    // Signed planar angle (rad) from the hull's forward (+X) to `target`, in the
    // XZ plane. This is the one number the body and the turret both consume.
    // Returns 0 (and success=false via out-param if you prefer) when unreadable.
    inline float bearing_to(const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        WzBehaviorEntityId target)
    {
        WzMat4 self{}; WzVec3 tp{};
        if (!wz_self_world_transform(facts, event, &self)
            || !wz_read_world_position(facts, target, &tp)) return 0.0f;
        const float fa = atan2f(-self.m[2], self.m[0]);        // forward angle
        const float ta = atan2f(-(tp.z - self.m[14]),          // target angle
            (tp.x - self.m[12]));
        float e = ta - fa;
        constexpr float kPi = 3.14159265358979f;
        while (e > kPi) e -= 2 * kPi;
        while (e < -kPi) e += 2 * kPi;
        return e;
    }

    inline float clampf(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline float wrap_pi(float a)
    {
        constexpr float kPi = 3.14159265358979f;
        while (a > kPi) { a -= 2.0f * kPi; }
        while (a < -kPi) { a += 2.0f * kPi; }
        return a;
    }

    // Signed angle (rad) from the hull's TRUE FORWARD (kBodyForward) to `target`,
    // XZ plane; 0 = the nose points straight at the target. Same reads + short-way
    // wrap as bearing_to, just measured from the model's real forward instead of
    // the raw +X drive column. The body AND the turret aim from this, so they stay
    // consistent when kBodyForward is retuned.
    inline float face_bearing_to(const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        WzBehaviorEntityId target)
    {
        return wrap_pi(bearing_to(facts, event, target) - kBodyForward);
    }

    // "Turn and face `target`": yaw rate (rad/sec) that turns the hull's NOSE to
    // point at the target entity. Full 360-deg convergence, turning the short way
    // -- NOT limited to 180. Unlike a raw +X seek it respects kBodyForward, so the
    // tank leads with its nose rather than its tail. Gate SPEED separately.
    inline float face_yaw_rate(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        WzBehaviorEntityId target,
        float gain,
        float max_rate)
    {
        return clampf(
            gain * face_bearing_to(facts, event, target),
            -max_rate,
            max_rate);
    }

    // Point the turret CHILD at a relative yaw (0 = dead ahead), by writing its
    // LOCAL rotation as a yaw about +Y. SET_LOCAL_ROTATION replaces rotation and
    // preserves the node's translation + scale, which is exactly a turret ring.
    inline void aim_turret(const WzBehaviorFrameFacts* facts,
        WzBehaviorEntityId turret, float yaw)
    {
        const float h = yaw * 0.5f;
        WzQuaternion q{ 0.0f, sinf(h), 0.0f, cosf(h) };   // yaw about +Y
        wz_write_set_local_rotation(facts, turret, q);
    }

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

    // Drive along the hull's TRUE FORWARD (kBodyForward) at `speed`, yaw `heading`.
    // drive_heading_speed hardwires the local +X drive column; this rotates the
    // drive vector by kBodyForward so "forward" is the model's nose (for PI it is
    // simply local -X). Pair with face_yaw_rate so steering + driving agree.
    inline void drive_facing(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        float heading,
        float speed)
    {
        // Local direction at angle kBodyForward from +X, in the atan2(-z,x)
        // convention bearing_to uses: (cos, 0, -sin).
        const float vx = speed * cosf(kBodyForward);
        const float vz = -speed * sinf(kBodyForward);
        wz_self_set_motion_space(facts, event, WZ_BEHAVIOR_MOTION_SPACE_LOCAL);
        wz_self_set_linear_velocity(facts, event, vx, 0.0f, vz);
        wz_self_set_angular_velocity(facts, event, 0.0f, heading, 0.0f);
    }

    // Yaw rate (rad/sec) that turns the entity's forward -- its LOCAL +X, the
    // axis drive_heading_speed drives along -- toward `target`, in the horizontal
    // XZ plane (Y is up). Reads self's world transform for the current facing +
    // position and the target's world position; returns 0 (no steer) if either
    // read fails, the target handle is invalid, or the target is right on top of
    // us. `gain` scales the heading error into a rate; the result is clamped to
    // +/- max_rate. This is a pure "seek" -- gate the forward SPEED elsewhere.
    inline float seek_yaw_rate(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        WzBehaviorEntityId target,
        float gain,
        float max_rate)
    {
        WzMat4 self_world{};
        WzVec3 target_pos{};
        if (!wz_self_world_transform(facts, event, &self_world)
            || !wz_read_world_position(facts, target, &target_pos))
        {
            return 0.0f;
        }

        // Column-major: forward (local +X) is column 0; our position is column 3.
        const float fx = self_world.m[0];
        const float fz = self_world.m[2];
        const float dx = target_pos.x - self_world.m[12];
        const float dz = target_pos.z - self_world.m[14];

        if ((dx * dx + dz * dz) <= 0.000001f) {
            return 0.0f;   // target is under/over us -- no meaningful heading.
        }

        // Heading angle of a planar vector under the engine's +Y rotation
        // convention: rotating forward by +theta about +Y sends (vx,vz) so that
        // its angle atan2(-vz, vx) increases, so a POSITIVE yaw rate closes a
        // POSITIVE (target - forward) error. No sign guessing.
        const float forward_angle = atan2f(-fz, fx);
        const float target_angle = atan2f(-dz, dx);

        float error = target_angle - forward_angle;
        constexpr float kPi = 3.14159265358979f;
        while (error > kPi) { error -= 2.0f * kPi; }   // turn the short way
        while (error < -kPi) { error += 2.0f * kPi; }

        float rate = gain * error;
        if (rate > max_rate) { rate = max_rate; }
        if (rate < -max_rate) { rate = -max_rate; }
        return rate;
    }

    // Signed angle (rad) between where the turret is CURRENTLY aimed and the
    // direction to `target`, in the horizontal plane. 0 = gun ON target; nonzero =
    // how far off it is -- because the target is outside the frontal arc (turret
    // pinned at +/-90), or the turret is still slewing. `turret_yaw` is the turret's
    // actual relative yaw (Chassis::turret_yaw). This is the readout the hull uses
    // to know it must reposition, and what a future "fire?" gate would test.
    inline float aim_error(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        WzBehaviorEntityId target,
        float turret_yaw)
    {
        return wrap_pi(face_bearing_to(facts, event, target) - turret_yaw);
    }

    // Yaw rate (rad/sec) that steers the hull to ORBIT `target` at roughly
    // `standoff` world units: aim the nose ~tangent to the circle around the
    // target (90 deg off the target bearing), with a radius correction that spirals
    // IN when too far and OUT when too close, so the tank settles onto the ring and
    // circles it. Pair with a forward drive (drive_facing). Counter-clockwise.
    inline float orbit_yaw_rate(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        WzBehaviorEntityId target,
        float standoff,
        float gain,
        float max_rate)
    {
        WzMat4 self{}; WzVec3 tp{};
        if (!wz_self_world_transform(facts, event, &self)
            || !wz_read_world_position(facts, target, &tp))
        {
            return 0.0f;
        }
        const float dx = tp.x - self.m[12];
        const float dz = tp.z - self.m[14];
        const float dist = sqrtf(dx * dx + dz * dz);

        constexpr float kHalfPi = 1.57079633f;
        // How far off the standoff ring we are, in [-1, 1] (clamped): + = too far.
        const float radius_error = standoff > 0.0f
            ? clampf((dist - standoff) / standoff, -1.0f, 1.0f)
            : 0.0f;
        // Tangent (-90 deg) plus up to 45 deg of inward/outward correction.
        const float aim = -kHalfPi + radius_error * (kHalfPi * 0.5f);
        // face_bearing_to = angle from the nose to the target; add the tangent aim.
        const float desired =
            wrap_pi(face_bearing_to(facts, event, target) + aim);
        return clampf(gain * desired, -max_rate, max_rate);
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
