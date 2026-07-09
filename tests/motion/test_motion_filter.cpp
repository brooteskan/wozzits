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
    SceneMotionFilterAsset f{};
    f.roll.smoothing_time = 0.4f;  // roll damped; pitch/yaw snap
    MotionFilterState s{};
    apply_motion_filter(make_target({ 0, 0, 0 }, 0, 0, 0), f, s, kDt);  // level
    const Transform out =
        apply_motion_filter(make_target({ 0, 0, 0 }, 60, 20, 30), f, s, kDt);
    float e[3];
    euler_of(out.rotation, e);
    EXPECT_NEAR(e[1], 20.0f, 0.5f);  // pitch passed through
    EXPECT_NEAR(e[2], 30.0f, 0.5f);  // yaw passed through
    EXPECT_GT(e[0], 0.0f);           // roll moving toward 60
    EXPECT_LT(e[0], 60.0f);          // but lagging
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
