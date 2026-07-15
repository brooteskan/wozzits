#pragma once
// behavior/enemy_tank_v1/tank_drive.h
//
// enemy_tank_v1's OWN differential-drive core -- a fresh, chart-driven tank built up
// from scratch. This is a decoupled copy of the shared behavior/tank_drive.h, trimmed
// to the DRIVING essentials (face a target, steer, drive), so v1 can evolve its body
// independently of the existing quantum_tank_agent without disturbing it.
//
// Deliberately SIMPLE for now. When v1 grows a turret + guns, port these from the
// shared header as needed: struct Chassis, aim_turret, elevate_gun, aim_error,
// hull_aim_error, elevation_to, local_elevation_to, orbit_yaw_rate, kTurretHalfArc.
//
// Header-only (static inline): the plugin DLL includes it directly, no shared .cpp.

#include <engine/behavior/behavior_module_api.h>

#include <math.h>   // atan2f, cosf, sinf, sqrtf

namespace tank_drive
{
    // Shared tuning.
    inline constexpr float kMoveSpeed = 6.0f;   // world units/sec at full throttle
    inline constexpr float kTurnSpeed = 1.8f;   // yaw rad/sec at full turn

    // Yaw (rad) of the hull's TRUE FORWARD relative to its local +X drive column.
    // bearing_to / drive_heading_speed treat +X as forward, but a model whose nose is
    // built facing the other way is forward = +X + PI -- which reads in-game as
    // "pursues but drives backward, gun trailing". This ONE knob rotates facing
    // (face_yaw_rate) and driving (drive_facing) together, so retune it -- 0, +/-PI/2,
    // or PI -- until the tank leads with its nose. tank1.glb faces -X, so: PI.
    inline constexpr float kBodyForward = 3.14159265358979f;

    // A normalized drive command: throttle and turn each in roughly [-1, 1].
    struct Drive
    {
        float throttle = 0.0f;
        float turn = 0.0f;
    };

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

    // Signed planar angle (rad) from the hull's forward (+X) to `target`, in the XZ
    // plane. The one number the body (and, later, the turret) consumes. 0 when
    // unreadable.
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
        return wrap_pi(ta - fa);
    }

    // Signed angle (rad) from the hull's TRUE FORWARD (kBodyForward) to `target`, XZ
    // plane; 0 = the nose points straight at the target. Measured from the model's
    // real forward instead of the raw +X drive column, so it stays consistent when
    // kBodyForward is retuned.
    inline float face_bearing_to(const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        WzBehaviorEntityId target)
    {
        return wrap_pi(bearing_to(facts, event, target) - kBodyForward);
    }

    // "Turn and face `target`": yaw rate (rad/sec) that turns the hull's NOSE toward
    // the target -- full 360-deg convergence, turning the short way, respecting
    // kBodyForward so the tank leads with its nose. Gate SPEED separately.
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

    // Yaw rate (rad/sec) that turns the entity's LOCAL +X (the axis
    // drive_heading_speed drives along) toward `target`, XZ plane. A pure "seek" that
    // ignores kBodyForward -- pair with drive_heading_speed, or use face_yaw_rate +
    // drive_facing for a nose-first model. 0 if unreadable or the target is on top of
    // us.
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
        const float fx = self_world.m[0];
        const float fz = self_world.m[2];
        const float dx = target_pos.x - self_world.m[12];
        const float dz = target_pos.z - self_world.m[14];
        if ((dx * dx + dz * dz) <= 0.000001f) {
            return 0.0f;
        }
        const float forward_angle = atan2f(-fz, fx);
        const float target_angle = atan2f(-dz, dx);
        return clampf(gain * wrap_pi(target_angle - forward_angle), -max_rate, max_rate);
    }

    // Differential steering: two tread speeds -> (throttle, turn). The SUM of the
    // treads drives forward, their DIFFERENCE yaws.
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
    // Rotates the drive vector by kBodyForward so "forward" is the model's nose (for
    // PI it is simply local -X). Pair with face_yaw_rate so steering + driving agree.
    inline void drive_facing(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        float heading,
        float speed)
    {
        const float vx = speed * cosf(kBodyForward);
        const float vz = -speed * sinf(kBodyForward);
        wz_self_set_motion_space(facts, event, WZ_BEHAVIOR_MOTION_SPACE_LOCAL);
        wz_self_set_linear_velocity(facts, event, vx, 0.0f, vz);
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
