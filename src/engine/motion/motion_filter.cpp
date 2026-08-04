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
            // Written as !(x > 0), not (x <= 0). The two differ for NaN: every
            // comparison against NaN is false, so `smoothing_time <= 0.0f` took
            // the SMOOTHING branch for a NaN parameter and drove the state to
            // NaN, while the translation caller's own `smoothing_time > 0.0f`
            // test forty lines below snapped safely. Two guards over one
            // parameter with opposite NaN behaviour (#314, C1-C2); this is the
            // safe one, so both now snap.
            if (!(smoothing_time > 0.0f) || !(dt > 0.0f)) {
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

        // Inverse of a unit quaternion = its conjugate.
        Quaternion conjugate(const Quaternion& q)
        {
            return Quaternion{ -q.x, -q.y, -q.z, q.w };
        }

        // Log of a unit quaternion -> the rotation vector (axis * angle,
        // radians). Picks the shortest arc (w >= 0 so angle in [0, pi]), so the
        // components are the small per-local-axis roll/pitch/yaw needed to reach
        // the rotation. Near identity it is ~0. This is the gimbal-free stand-in
        // for a euler decomposition: valid for any orientation, no asin fold.
        Vec3 quat_log(Quaternion q)
        {
            if (q.w < 0.0f) {           // q and -q are the same rotation
                q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w;
            }
            const float v = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
            if (v < 1e-6f) {
                return { 0.0f, 0.0f, 0.0f };
            }
            const float angle = 2.0f * std::atan2(v, q.w);   // [0, pi]
            const float s = angle / v;
            return { q.x * s, q.y * s, q.z * s };
        }

        // exp of a rotation vector (radians) -> unit quaternion.
        Quaternion quat_exp(const Vec3& w)
        {
            const float angle = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z);
            if (angle < 1e-6f) {
                return Quaternion::identity();
            }
            const Vec3 axis{ w.x / angle, w.y / angle, w.z / angle };
            return wz::math::from_axis_angle(axis, angle);
        }

        bool all_finite(const Vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool all_finite(const Quaternion& q)
        {
            return std::isfinite(q.x) && std::isfinite(q.y)
                && std::isfinite(q.z) && std::isfinite(q.w);
        }

        // Shape one axis of the ABSOLUTE target angle: level pins it to the world
        // horizon (0), limit clamps it. Smoothing is NOT done here -- it is
        // applied later, gimbal-free, to the whole orientation.
        float level_and_limit_angle(
            const wz::engine::assets::SceneMotionFilterRotationAxis& cfg,
            float target_angle)
        {
            float a = cfg.level ? 0.0f : target_angle;
            if (cfg.limit) {
                const float lo =
                    std::min(cfg.limit_min_degrees, cfg.limit_max_degrees);
                const float hi =
                    std::max(cfg.limit_min_degrees, cfg.limit_max_degrees);
                a = std::clamp(a, lo, hi);
            }
            return a;
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
        // Target orientation. Default is the rigid target itself, so the pure
        // smoothing case (the camera) takes the fully gimbal-free path below.
        // Only when an axis is leveled or limited do we go through euler to shape
        // the absolute target angles -- safe there because level/limit act near
        // the horizon / their bounds, not at the turn gimbal.
        Quaternion target_rot = pose.rotation;
        if (filter.roll.level || filter.pitch.level || filter.yaw.level
            || filter.roll.limit || filter.pitch.limit || filter.yaw.limit)
        {
            float te[3];
            euler_degrees_from_quat(pose.rotation, te);
            // te[0]/[1]/[2] are rotations about X/Y/Z (quaternion_from_euler_
            // degrees is qz*qy*qx over x/y/z). This engine is z-forward /
            // x-right / y-up (DirectX; camera.cpp), and the channels are
            // documented (scene_asset_data.h) as roll=about-forward,
            // pitch=about-right, yaw=about-up -- so X=pitch, Y=yaw, Z=roll. The
            // old {roll,pitch,yaw} order clamped each channel on the WRONG axis
            // (e.g. a "pitch limit" clamped the heading). (C1-C57, #314)
            te[0] = level_and_limit_angle(filter.pitch, te[0]);
            te[1] = level_and_limit_angle(filter.yaw, te[1]);
            te[2] = level_and_limit_angle(filter.roll, te[2]);
            target_rot =
                wz::math::quaternion_from_euler_degrees(te[0], te[1], te[2]);
        }

        // Disabled: pass the RIGID target straight through and re-seed on
        // re-enable so resuming doesn't spike.
        if (!filter.enabled) {
            state.initialized = false;
            return Transform{ target_pos, pose.rotation, scale };
        }

        // A single non-finite frame used to poison this filter FOREVER. The state
        // is persistent per node id (it survives scene rebuilds, by design), and
        // smooth_damp reads `current` back out every frame, so once NaN is in
        // there no amount of clean input recovers it -- measured: one NaN frame,
        // then 109 clean frames, still NaN (#314, C1-C2). The only cure was
        // toggling the component off and on.
        //
        // A transient fault must stay transient, so:
        //   * a non-finite TARGET is not folded in -- hold the last good filtered
        //     pose for that frame (and if there is no good pose yet, pass the
        //     target through exactly as a disabled filter does, leaving the state
        //     uninitialised so the next finite frame seeds cleanly);
        //   * a state that is non-finite anyway re-seeds, which is what the
        //     !initialized path below already does correctly on first sight.
        const bool target_is_finite =
            all_finite(target_pos) && all_finite(target_rot);
        if (!target_is_finite) {
            if (!state.initialized) {
                return Transform{ target_pos, pose.rotation, scale };
            }
            return Transform{ state.position, state.rotation, scale };
        }

        const bool state_is_finite =
            all_finite(state.position)
            && all_finite(state.position_velocity)
            && all_finite(state.rotation)
            && std::isfinite(state.rotation_velocity[0])
            && std::isfinite(state.rotation_velocity[1])
            && std::isfinite(state.rotation_velocity[2]);

        // Seed to the target on first sight so a filtered node starts in place.
        if (!state.initialized || !state_is_finite) {
            state.position = target_pos;
            state.position_velocity = { 0.0f, 0.0f, 0.0f };
            state.rotation = target_rot;
            for (int i = 0; i < 3; ++i) {
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

        // ── Rotation: gimbal-free, per node-local axis (roll / pitch / yaw) ──
        // The relative rotation state -> target, expressed in the state's LOCAL
        // frame; its log is the per-axis error in radians. Because that relative
        // rotation is small frame to frame it never approaches the euler asin
        // fold, so a tilted node turning through any heading stays continuous --
        // no dip at the two headings 180 deg apart the euler smoother caught on.
        const Vec3 err =
            quat_log(wz::math::mul(conjugate(state.rotation), target_rot));
        // err.x/y/z are rotations about the node-LOCAL X/Y/Z axes; with
        // z-forward / x-right / y-up (camera.cpp) and roll=forward / pitch=right /
        // yaw=up (scene_asset_data.h), roll drives err.z, pitch drives err.x, yaw
        // drives err.y. The old {roll,pitch,yaw} order put each channel on the
        // WRONG axis. (C1-C57, #314)
        const float smoothing[3] = {
            filter.pitch.smoothing_time,  // err.x = local X = right = pitch
            filter.yaw.smoothing_time,    // err.y = local Y = up    = yaw
            filter.roll.smoothing_time,   // err.z = local Z = fwd   = roll
        };
        const float err_axis[3] = { err.x, err.y, err.z };
        float step[3];
        for (int i = 0; i < 3; ++i) {
            // Critically-damped chase of the (moving) target: current starts at 0
            // -- measured from the state we are about to rotate -- and the
            // velocity carries the spring's momentum across frames. smoothing_time
            // 0 snaps (returns the full error), so an un-damped axis passes
            // straight through.
            float cur = 0.0f;
            smooth_damp(cur, err_axis[i], state.rotation_velocity[i],
                smoothing[i], dt);
            step[i] = cur;
        }
        state.rotation = wz::math::normalize(wz::math::mul(
            state.rotation, quat_exp(Vec3{ step[0], step[1], step[2] })));

        Transform out;
        out.position = state.position;
        out.rotation = state.rotation;
        out.scale = scale;
        return out;
    }
}
