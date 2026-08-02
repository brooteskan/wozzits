#include <gtest/gtest.h>

#include <engine/motion/motion_filter.h>

#include <math/mat4.h>
#include <math/quaternion.h>

#include <cmath>
#include <optional>

using wz::engine::assets::SceneMotionFilterAsset;
using wz::engine::motion::apply_motion_filter;
using wz::engine::motion::MotionFilterState;
using wz::math::Mat4;
using wz::math::Quaternion;
using wz::math::Transform;
using wz::math::Vec3;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kRadToDeg = 180.0f / kPi;

    Mat4 make_target(
        Vec3 pos, float roll, float pitch, float yaw, Vec3 scale = { 1, 1, 1 })
    {
        Transform t;
        t.position = pos;
        t.rotation = wz::math::quaternion_from_euler_degrees(roll, pitch, yaw);
        t.scale = scale;
        return wz::math::transform(t);
    }

    // Test-local euler extraction, matching the module's convention, for
    // asserting per-axis rotation behavior on the returned quaternion.
    void euler_of(const Quaternion& q, float out[3])
    {
        float x = q.x, y = q.y, z = q.z, w = q.w;
        const float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len > 1e-8f) { x /= len; y /= len; z /= len; w /= len; }
        out[0] = std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
            * kRadToDeg;
        const float sinp = 2 * (w * y - z * x);
        out[1] = (std::abs(sinp) >= 1.0f
            ? std::copysign(kPi * 0.5f, sinp)
            : std::asin(sinp)) * kRadToDeg;
        out[2] = std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
            * kRadToDeg;
    }

    constexpr float kDt = 1.0f / 60.0f;
}

TEST(MotionFilter, NoFilterReturnsTarget)
{
    SceneMotionFilterAsset f{};  // all defaults => no-op
    MotionFilterState s{};
    const Transform out =
        apply_motion_filter(make_target({ 1, 2, 3 }, 10, 20, 30), f, s, kDt);
    EXPECT_NEAR(out.position.x, 1.0f, 1e-3f);
    EXPECT_NEAR(out.position.y, 2.0f, 1e-3f);
    EXPECT_NEAR(out.position.z, 3.0f, 1e-3f);
    float e[3];
    euler_of(out.rotation, e);
    EXPECT_NEAR(e[0], 10.0f, 1e-2f);
    EXPECT_NEAR(e[1], 20.0f, 1e-2f);
    EXPECT_NEAR(e[2], 30.0f, 1e-2f);
}

TEST(MotionFilter, EulerConventionRoundTrips)
{
    // The rotation path is only correct if quaternion_from_euler_degrees and the
    // extraction are inverses. A no-op filter must return the target orientation.
    SceneMotionFilterAsset f{};
    const float cases[3][3] = {
        { 10.f, 20.f, 30.f }, { -45.f, 15.f, 170.f }, { 5.f, -60.f, -120.f },
    };
    for (const auto& c : cases) {
        MotionFilterState s{};
        const Transform out =
            apply_motion_filter(make_target({ 0, 0, 0 }, c[0], c[1], c[2]),
                f, s, kDt);
        float e[3];
        euler_of(out.rotation, e);
        EXPECT_NEAR(e[0], c[0], 0.1f);
        EXPECT_NEAR(e[1], c[1], 0.1f);
        EXPECT_NEAR(e[2], c[2], 0.1f);
    }
}

TEST(MotionFilter, FirstApplySeedsToTargetNoLagSpike)
{
    SceneMotionFilterAsset f{};
    f.translation_smoothing[1] = 0.5f;
    f.roll.smoothing_time = 0.5f;
    MotionFilterState s{};
    // First application seeds at the target -- no lag up from y=0 / roll=0.
    const Transform out =
        apply_motion_filter(make_target({ 0, 100, 0 }, 45, 0, 0), f, s, kDt);
    EXPECT_NEAR(out.position.y, 100.0f, 1e-3f);
    float e[3];
    euler_of(out.rotation, e);
    EXPECT_NEAR(e[0], 45.0f, 1e-2f);
}

TEST(MotionFilter, PositionSmoothingLagsThenConverges)
{
    SceneMotionFilterAsset f{};
    f.translation_smoothing[1] = 0.3f;
    MotionFilterState s{};
    apply_motion_filter(make_target({ 0, 0, 0 }, 0, 0, 0), f, s, kDt);  // seed
    Transform out =
        apply_motion_filter(make_target({ 0, 10, 0 }, 0, 0, 0), f, s, kDt);
    EXPECT_GT(out.position.y, 0.0f);   // moved
    EXPECT_LT(out.position.y, 10.0f);  // but lagging
    for (int i = 0; i < 400; ++i) {
        out = apply_motion_filter(make_target({ 0, 10, 0 }, 0, 0, 0), f, s, kDt);
    }
    EXPECT_NEAR(out.position.y, 10.0f, 1e-2f);  // steady state == rigid target
}

TEST(MotionFilter, UnsmoothedAxisPassesThroughInstantly)
{
    SceneMotionFilterAsset f{};
    f.translation_smoothing[1] = 0.3f;  // Y damped; X/Z pass through
    MotionFilterState s{};
    apply_motion_filter(make_target({ 0, 0, 0 }, 0, 0, 0), f, s, kDt);
    const Transform out =
        apply_motion_filter(make_target({ 5, 10, 7 }, 0, 0, 0), f, s, kDt);
    EXPECT_FLOAT_EQ(out.position.x, 5.0f);
    EXPECT_FLOAT_EQ(out.position.z, 7.0f);
    EXPECT_LT(out.position.y, 10.0f);
}

TEST(MotionFilter, TerrainFloorClampsBelowLeavesAbove)
{
    SceneMotionFilterAsset f{};
    f.terrain_floor = true;
    f.terrain_floor_offset = 1.5f;
    const auto sampler =
        [](float, float) -> std::optional<float> { return 2.0f; };
    {
        MotionFilterState s{};
        const Transform out = apply_motion_filter(
            make_target({ 0, 0, 0 }, 0, 0, 0), f, s, kDt, sampler);
        EXPECT_NEAR(out.position.y, 3.5f, 1e-4f);  // clamped to floor + offset
    }
    {
        MotionFilterState s{};
        const Transform out = apply_motion_filter(
            make_target({ 0, 10, 0 }, 0, 0, 0), f, s, kDt, sampler);
        EXPECT_NEAR(out.position.y, 10.0f, 1e-4f);  // above floor: untouched
    }
}

TEST(MotionFilter, RollDampedWhilePitchYawPass)
{
    // Roll damped, pitch/yaw un-damped. The whole orientation converges to the
    // rigid target; while it does, the damped roll clearly lags. (Smoothing is
    // gimbal-free on the relative rotation's log, so for one large combined step
    // the un-damped axes are approximate rather than exact -- what holds is that
    // roll lags and everything converges.)
    SceneMotionFilterAsset f{};
    f.roll.smoothing_time = 0.4f;  // roll damped; pitch/yaw snap
    MotionFilterState s{};
    apply_motion_filter(make_target({ 0, 0, 0 }, 0, 0, 0), f, s, kDt);  // seed

    Transform out;
    for (int i = 0; i < 5; ++i) {
        out = apply_motion_filter(make_target({ 0, 0, 0 }, 60, 20, 30), f, s, kDt);
    }
    float e[3];
    euler_of(out.rotation, e);
    EXPECT_GT(e[0], 0.0f);   // roll advancing toward 60
    EXPECT_LT(e[0], 50.0f);  // but the damped axis clearly lags

    for (int i = 0; i < 400; ++i) {
        out = apply_motion_filter(make_target({ 0, 0, 0 }, 60, 20, 30), f, s, kDt);
    }
    euler_of(out.rotation, e);   // converges to the exact rigid target
    EXPECT_NEAR(e[0], 60.0f, 0.5f);
    EXPECT_NEAR(e[1], 20.0f, 0.5f);
    EXPECT_NEAR(e[2], 30.0f, 0.5f);
}

TEST(MotionFilter, TiltedNodeYawSweepDoesNotDipAtTheGimbal)
{
    // Regression for the reported camera dip: the old euler-per-axis smoother
    // decomposed the world orientation into roll/pitch/yaw and smoothed each, so
    // at the two headings 180 deg apart where pitch folds through +/-90 the
    // roll/yaw representation jumped 180 deg and the independent smoothing could
    // not track it -- dipping the camera, worse the more the node was tilted (on
    // a slope). Sweep a TILTED node through a full turn about the euler gimbal
    // axis (world Y) and require the gimbal-free smoother to keep tracking the
    // rigid target the whole way.
    const float d2r = kPi / 180.0f;
    const auto quat_target = [&](float heading_deg) {
        // A 25 deg slope tilt about X, then a turn about world up (Y).
        const Quaternion tilt =
            wz::math::from_axis_angle(Vec3{ 1, 0, 0 }, 25.0f * d2r);
        const Quaternion yaw =
            wz::math::from_axis_angle(Vec3{ 0, 1, 0 }, heading_deg * d2r);
        return wz::math::mul(yaw, tilt);
    };
    const auto mat_of = [&](const Quaternion& q) {
        Transform t;
        t.position = { 0, 0, 0 };
        t.rotation = q;
        t.scale = { 1, 1, 1 };
        return wz::math::transform(t);
    };

    SceneMotionFilterAsset f{};   // the tank camera's config: smooth all 3 axes
    f.roll.smoothing_time = 0.2f;
    f.pitch.smoothing_time = 0.2f;
    f.yaw.smoothing_time = 0.1f;

    MotionFilterState s{};
    apply_motion_filter(mat_of(quat_target(0.0f)), f, s, kDt);  // seed at heading 0

    float max_err_deg = 0.0f;
    for (int i = 1; i <= 1000; ++i) {           // 0.4 deg/step -> 400 deg, past 90 & 270
        const float heading = 0.4f * static_cast<float>(i);
        const Quaternion target = quat_target(heading);
        const Transform out = apply_motion_filter(mat_of(target), f, s, kDt);
        // Angular distance between the smoothed output and the rigid target.
        const float c =
            std::min(1.0f, std::abs(wz::math::dot(out.rotation, target)));
        const float err_deg = 2.0f * std::acos(c) * kRadToDeg;
        max_err_deg = std::max(max_err_deg, err_deg);
    }
    // Only the smoothing lag (a few degrees) remains; the euler fold used to
    // spike this into the tens of degrees at heading 90 and 270.
    EXPECT_LT(max_err_deg, 12.0f);
}

TEST(MotionFilter, RotationLimitClampsPitch)
{
    SceneMotionFilterAsset f{};
    f.pitch.limit = true;
    f.pitch.limit_min_degrees = -80.0f;
    f.pitch.limit_max_degrees = 80.0f;
    MotionFilterState s{};
    const Transform out =
        apply_motion_filter(make_target({ 0, 0, 0 }, 0, 85, 0), f, s, kDt);
    float e[3];
    euler_of(out.rotation, e);
    EXPECT_NEAR(e[1], 80.0f, 0.5f);  // clamped from 85
}

TEST(MotionFilter, LevelRollTargetsZero)
{
    SceneMotionFilterAsset f{};
    f.roll.level = true;  // no smoothing => snap to level (0)
    MotionFilterState s{};
    const Transform out =
        apply_motion_filter(make_target({ 0, 0, 0 }, 60, 0, 0), f, s, kDt);
    float e[3];
    euler_of(out.rotation, e);
    EXPECT_NEAR(e[0], 0.0f, 0.5f);  // leveled despite target roll 60
}

TEST(MotionFilter, ScalePassesThrough)
{
    SceneMotionFilterAsset f{};
    MotionFilterState s{};
    const Transform out = apply_motion_filter(
        make_target({ 0, 0, 0 }, 0, 0, 0, { 2, 3, 4 }), f, s, kDt);
    EXPECT_NEAR(out.scale.x, 2.0f, 1e-3f);
    EXPECT_NEAR(out.scale.y, 3.0f, 1e-3f);
    EXPECT_NEAR(out.scale.z, 4.0f, 1e-3f);
}

TEST(MotionFilter, DisabledPassesTargetThrough)
{
    SceneMotionFilterAsset f{};
    f.enabled = false;
    f.translation_smoothing[1] = 0.5f;  // would smooth if enabled
    MotionFilterState s{};
    apply_motion_filter(make_target({ 0, 0, 0 }, 0, 0, 0), f, s, kDt);
    const Transform out =
        apply_motion_filter(make_target({ 0, 10, 0 }, 0, 0, 0), f, s, kDt);
    EXPECT_NEAR(out.position.y, 10.0f, 1e-4f);  // no lag when disabled
}

TEST(MotionFilter, Deterministic)
{
    SceneMotionFilterAsset f{};
    f.roll.smoothing_time = 0.3f;
    f.translation_smoothing[1] = 0.3f;
    const Mat4 seed = make_target({ 0, 0, 0 }, 0, 0, 0);
    const Mat4 tgt = make_target({ 0, 10, 0 }, 45, 0, 0);
    const auto run = [&] {
        MotionFilterState s{};
        apply_motion_filter(seed, f, s, kDt);
        Transform o{};
        for (int i = 0; i < 50; ++i) {
            o = apply_motion_filter(tgt, f, s, kDt);
        }
        return o;
    };
    const Transform a = run();
    const Transform b = run();
    EXPECT_FLOAT_EQ(a.position.y, b.position.y);
    EXPECT_FLOAT_EQ(a.rotation.x, b.rotation.x);
    EXPECT_FLOAT_EQ(a.rotation.w, b.rotation.w);
}

// ── Non-finite input must not LATCH (issue #314, C1-C2) ─────────────────────
//
// MotionFilterState is persistent per node id and survives scene rebuilds, and
// smooth_damp reads `current` back out every frame -- so a single NaN frame used
// to kill the node for the whole session. Measured before the fix: one NaN
// frame, then 109 clean frames, still NaN; the only cure was toggling the
// component off and on.
//
// LOAD-BEARING: the NaN must arrive MID-RUN, after the state is initialised.
// A NaN on the very first frame takes the uninitialised path, which was never
// the broken one.
TEST(MotionFilter, RecoversAfterOneNonFiniteFrame)
{
    SceneMotionFilterAsset f{};
    f.translation_smoothing[0] = 0.2f;
    f.translation_smoothing[1] = 0.2f;
    f.translation_smoothing[2] = 0.2f;
    f.roll.smoothing_time = 0.2f;
    f.pitch.smoothing_time = 0.2f;
    f.yaw.smoothing_time = 0.2f;

    MotionFilterState state{};
    Transform out{};
    for (int frame = 0; frame < 120; ++frame) {
        const float x = (frame == 10)
            ? std::nanf("")
            : static_cast<float>(frame);
        out = apply_motion_filter(
            make_target({ x, 0.0f, 0.0f }, 0, 0, 0), f, state, kDt);
    }

    EXPECT_TRUE(std::isfinite(out.position.x));
    EXPECT_TRUE(std::isfinite(out.rotation.w));
    // And it must have tracked the clean input, not merely stayed finite.
    EXPECT_GT(out.position.x, 50.0f);
}

// The bad frame itself holds the last good pose rather than emitting the fault.
TEST(MotionFilter, NonFiniteTargetHoldsTheLastGoodPose)
{
    SceneMotionFilterAsset f{};
    f.translation_smoothing[0] = 0.2f;

    MotionFilterState state{};
    for (int frame = 0; frame < 30; ++frame) {
        apply_motion_filter(
            make_target({ 5.0f, 0.0f, 0.0f }, 0, 0, 0), f, state, kDt);
    }
    const Transform good = apply_motion_filter(
        make_target({ 5.0f, 0.0f, 0.0f }, 0, 0, 0), f, state, kDt);

    const Transform held = apply_motion_filter(
        make_target({ std::nanf(""), 0.0f, 0.0f }, 0, 0, 0), f, state, kDt);

    EXPECT_FLOAT_EQ(held.position.x, good.position.x);
}

// smooth_damp's guard and its translation-side caller both have to treat a NaN
// smoothing_time the same way. They did not: `smoothing_time <= 0.0f` took the
// smoothing branch for NaN (driving the ROTATION state to NaN) while the
// caller's `smoothing_time > 0.0f` snapped. Rotation is the half that broke.
TEST(MotionFilter, NonFiniteSmoothingTimeSnapsInsteadOfPoisoningRotation)
{
    SceneMotionFilterAsset f{};
    const float nan_value = std::nanf("");
    f.translation_smoothing[0] = nan_value;
    f.roll.smoothing_time = nan_value;
    f.pitch.smoothing_time = nan_value;
    f.yaw.smoothing_time = nan_value;

    MotionFilterState state{};
    Transform out{};
    for (int frame = 0; frame < 60; ++frame) {
        out = apply_motion_filter(
            make_target({ static_cast<float>(frame), 0.0f, 0.0f }, 0, 30, 0),
            f, state, kDt);
    }

    EXPECT_TRUE(std::isfinite(out.rotation.x));
    EXPECT_TRUE(std::isfinite(out.rotation.y));
    EXPECT_TRUE(std::isfinite(out.rotation.z));
    EXPECT_TRUE(std::isfinite(out.rotation.w));
    EXPECT_TRUE(std::isfinite(out.position.x));
}
