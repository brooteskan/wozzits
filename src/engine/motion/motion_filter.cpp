// src/engine/motion/motion_filter.cpp

#include <engine/motion/motion_filter.h>

#include <math/mat4.h>
#include <math/quaternion.h>
#include <math/vec3.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::motion
{
    using wz::math::Mat4;
    using wz::math::Quaternion;
    using wz::math::Transform;
    using wz::math::Vec3;

    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kRadToDeg = 180.0f / kPi;

        // Critically-damped smoothing (Thomas Lowe, Game Programming Gems 4):
        // ease `current` toward `target` carrying `velocity`. No overshoot,
        // stable under variable dt. smoothing_time <= 0 (or dt <= 0) snaps.
        void smooth_damp(
            float& current, float target, float& velocity,
            float smoothing_time, float dt)
        {
            if (smoothing_time <= 0.0f || dt <= 0.0f) {
                current = target;
                velocity = 0.0f;
                return;
            }
            const float omega = 2.0f / smoothing_time;
            const float x = omega * dt;
            const float exp_ =
                1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            const float change = current - target;
            const float temp = (velocity + omega * change) * dt;
            velocity = (velocity - omega * temp) * exp_;
            current = target + (change + temp) * exp_;
        }

        // Wrap an angle in degrees to (-180, 180].
        float wrap_degrees(float a)
        {
            a = std::fmod(a, 360.0f);
            if (a > 180.0f) {
                a -= 360.0f;
            }
            if (a < -180.0f) {
                a += 360.0f;
            }
            return a;
        }

        // Roll(X) / pitch(Y) / yaw(Z) degrees from a quaternion -- the inverse of
        // math::quaternion_from_euler_degrees (same convention as the editor
        // snapshot's extraction), so a channel matches the inspector's rotation
        // X/Y/Z and recompose round-trips.
        void euler_degrees_from_quat(const Quaternion& q, float out[3])
        {
            float x = q.x, y = q.y, z = q.z, w = q.w;
            const float len = std::sqrt(x * x + y * y + z * z + w * w);
            if (len > 1e-8f) {
                x /= len;
                y /= len;
                z /= len;
                w /= len;
            }

            const float sinr_cosp = 2.0f * (w * x + y * z);
            const float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
            const float roll = std::atan2(sinr_cosp, cosr_cosp);

            const float sinp = 2.0f * (w * y - z * x);
            const float pitch = std::abs(sinp) >= 1.0f
                ? std::copysign(kPi * 0.5f, sinp)
                : std::asin(sinp);

            const float siny_cosp = 2.0f * (w * z + x * y);
            const float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
            const float yaw = std::atan2(siny_cosp, cosy_cosp);

            out[0] = roll * kRadToDeg;
            out[1] = pitch * kRadToDeg;
            out[2] = yaw * kRadToDeg;
        }

        Vec3 basis_scale(const Mat4& m)
        {
            const Vec3 c0{ m.m[0], m.m[1], m.m[2] };
            const Vec3 c1{ m.m[4], m.m[5], m.m[6] };
            const Vec3 c2{ m.m[8], m.m[9], m.m[10] };
            return {
                wz::math::length(c0),
                wz::math::length(c1),
                wz::math::length(c2),
            };
        }

        void filter_rotation_axis(
            float& current, float& velocity,
            const wz::engine::assets::SceneMotionFilterRotationAxis& cfg,
            float target_angle, float dt)
        {
            // Level eases toward world-level for this axis (roll/pitch flat);
            // otherwise follow the driven target angle.
            const float target = cfg.level ? 0.0f : target_angle;
            if (cfg.smoothing_time > 0.0f) {
                // Take the shortest angular path: chase a target unwrapped
                // relative to the current angle, then re-wrap.
                const float unwrapped = current + wrap_degrees(target - current);
                smooth_damp(current, unwrapped, velocity, cfg.smoothing_time, dt);
                current = wrap_degrees(current);
            }
            else {
                current = wrap_degrees(target);
                velocity = 0.0f;
            }
            if (cfg.limit) {
                const float lo =
                    std::min(cfg.limit_min_degrees, cfg.limit_max_degrees);
                const float hi =
                    std::max(cfg.limit_min_degrees, cfg.limit_max_degrees);
                const float clamped = std::clamp(current, lo, hi);
                if (clamped != current) {
                    current = clamped;
                    velocity = 0.0f;  // no windup past the limit
                }
            }
        }
    }

    Transform apply_motion_filter(
        const Mat4& target_world,
        const wz::engine::assets::SceneMotionFilterAsset& filter,
        MotionFilterState& state,
        float dt,
        const TerrainFloorSampler& terrain)
    {
        // Rigid pose (position + normalized rotation) robustly, scale separately;
        // rigid_pose_from_matrix drops any parent scale without decompose_trs's
        // strict gates (the target may carry a scaled parent, e.g. a 0.5 tank).
        const Transform pose = wz::math::rigid_pose_from_matrix(target_world);
        const Vec3 target_pos = pose.position;
        const Vec3 scale = basis_scale(target_world);
        float target_euler[3];
        euler_degrees_from_quat(pose.rotation, target_euler);

        // Disabled: pass the target straight through and re-seed on re-enable so
        // resuming doesn't spike.
        if (!filter.enabled) {
            state.initialized = false;
            return Transform{ target_pos, pose.rotation, scale };
        }

        // Seed to the target on first sight so a filtered node starts in place.
        if (!state.initialized) {
            state.position = target_pos;
            state.position_velocity = { 0.0f, 0.0f, 0.0f };
            for (int i = 0; i < 3; ++i) {
                state.rotation_euler[i] = target_euler[i];
                state.rotation_velocity[i] = 0.0f;
            }
            state.initialized = true;
        }

        // ── Translation: per world axis ───────────────────────────────────
        const auto smooth_axis =
            [&](float& p, float& v, float t, float smoothing_time) {
                if (smoothing_time > 0.0f) {
                    smooth_damp(p, t, v, smoothing_time, dt);
                }
                else {
                    p = t;
                    v = 0.0f;
                }
            };
        smooth_axis(state.position.x, state.position_velocity.x,
            target_pos.x, filter.translation_smoothing[0]);
        smooth_axis(state.position.y, state.position_velocity.y,
            target_pos.y, filter.translation_smoothing[1]);
        smooth_axis(state.position.z, state.position_velocity.z,
            target_pos.z, filter.translation_smoothing[2]);

        // Terrain floor (world Y): one-sided clamp (free to rise, blocked from
        // sinking below terrain + offset), sampled under the filtered position.
        if (filter.terrain_floor && terrain) {
            if (const std::optional<float> floor =
                    terrain(state.position.x, state.position.z))
            {
                const float min_y = *floor + filter.terrain_floor_offset;
                if (state.position.y < min_y) {
                    state.position.y = min_y;
                    state.position_velocity.y = 0.0f;
                }
            }
        }

        // ── Rotation: per node-local axis (roll / pitch / yaw) ────────────
        filter_rotation_axis(state.rotation_euler[0], state.rotation_velocity[0],
            filter.roll, target_euler[0], dt);
        filter_rotation_axis(state.rotation_euler[1], state.rotation_velocity[1],
            filter.pitch, target_euler[1], dt);
        filter_rotation_axis(state.rotation_euler[2], state.rotation_velocity[2],
            filter.yaw, target_euler[2], dt);

        Transform out;
        out.position = state.position;
        out.rotation = wz::math::quaternion_from_euler_degrees(
            state.rotation_euler[0],
            state.rotation_euler[1],
            state.rotation_euler[2]);
        out.scale = scale;
        return out;
    }
}
