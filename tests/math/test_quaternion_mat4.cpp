#include <gtest/gtest.h>

#include "math/vec3.h"
#include "math/mat4.h"
#include "math/quaternion.h"

#include <cmath>

using namespace wz::math;

static constexpr float EPS = 1e-5f;
static constexpr float PI = 3.14159265358979323846f;

TEST(Rotation, QuaternionMatchesMatrix)
{
    Vec3 v{ 1, 0, 0 };

    Quaternion q = from_axis_angle({ 0, 0, 1 }, PI * 0.5f);

    Vec3 qv = rotate(q, v);

    Mat4 R = rotation(q);
    Vec3 mv = mul_vector(R, v);

    EXPECT_NEAR(qv.x, mv.x, EPS);
    EXPECT_NEAR(qv.y, mv.y, EPS);
    EXPECT_NEAR(qv.z, mv.z, EPS);
}

TEST(Rotation, CompositionConsistency)
{
    Vec3 v{ 1, 0, 0 };

    Quaternion q1 = from_axis_angle({ 0, 0, 1 }, PI * 0.5f);
    Quaternion q2 = from_axis_angle({ 0, 1, 0 }, PI * 0.5f);

    Quaternion q_combined = mul(q2, q1); // apply q1 then q2

    Vec3 vq = rotate(q_combined, v);

    Mat4 R1 = rotation(q1);
    Mat4 R2 = rotation(q2);

    Mat4 M = mul(R2, R1);

    Vec3 vm = mul_vector(M, v);

    EXPECT_NEAR(vq.x, vm.x, EPS);
    EXPECT_NEAR(vq.y, vm.y, EPS);
    EXPECT_NEAR(vq.z, vm.z, EPS);
}

TEST(Rotation, LengthPreserved)
{
    Vec3 v{ 3, 4, 5 };

    Quaternion q = from_axis_angle({ 1, 1, 1 }, 1.0f);

    Vec3 r = rotate(q, v);

    EXPECT_NEAR(length(v), length(r), EPS);
}

TEST(Rotation, AxisInvariant)
{
    Vec3 axis = normalize(Vec3{ 1, 2, 3 });
    Vec3 v = axis; // same direction

    Quaternion q = from_axis_angle(axis, 1.234f);

    Vec3 r = rotate(q, v);

    EXPECT_NEAR(v.x, r.x, EPS);
    EXPECT_NEAR(v.y, r.y, EPS);
    EXPECT_NEAR(v.z, r.z, EPS);
}

TEST(Rotation, DoubleRotation)
{
    Vec3 v{ 1, 0, 0 };

    Quaternion q = from_axis_angle({ 0, 0, 1 }, PI * 0.5f);

    Vec3 r1 = rotate(q, v);
    Vec3 r2 = rotate(q, r1);

    EXPECT_NEAR(r2.x, -1.0f, EPS);
    EXPECT_NEAR(r2.y, 0.0f, EPS);
    EXPECT_NEAR(r2.z, 0.0f, EPS);
}

TEST(Transform, MatchesManualComposition)
{
    Transform t;
    t.position = { 10, 0, 0 };
    t.scale = { 2, 2, 2 };
    t.rotation = Quaternion::identity();

    Vec3 v{ 1, 0, 0 };

    // Using transform()
    Mat4 M = transform(t);
    Vec3 r1 = mul_point(M, v);

    // Manual
    Vec3 r2 = v;
    r2 = r2 * 2.0f;           // scale
    r2 = rotate(t.rotation, r2); // rotate
    r2 = r2 + t.position;     // translate

    EXPECT_NEAR(r1.x, r2.x, EPS);
    EXPECT_NEAR(r1.y, r2.y, EPS);
    EXPECT_NEAR(r1.z, r2.z, EPS);
}

TEST(Rotation, NonNormalizedQuaternion)
{
    Vec3 v{ 1, 0, 0 };

    Quaternion q = from_axis_angle({ 0,0,1 }, PI * 0.5f);

    // scale it (break normalization)
    q.x *= 5.0f;
    q.y *= 5.0f;
    q.z *= 5.0f;
    q.w *= 5.0f;

    Vec3 r = rotate(q, v);

    EXPECT_NEAR(r.x, 0.0f, EPS);
    EXPECT_NEAR(r.y, 1.0f, EPS);
}

TEST(Rotation, QuaternionFromRotationMatrixRoundTrips)
{
    const Quaternion inputs[] = {
        Quaternion::identity(),
        from_axis_angle(normalize(Vec3{ 1, 2, 3 }), 0.001f),
        from_axis_angle({ 1, 0, 0 }, PI),
        from_axis_angle({ 0, 1, 0 }, PI),
        from_axis_angle({ 0, 0, 1 }, PI),
        from_axis_angle(normalize(Vec3{ 1, 2, 3 }), 1.234f),
    };

    for (const Quaternion& input : inputs) {
        const Quaternion expected = normalize(input);
        const Quaternion actual = from_rotation_matrix(rotation(input));
        EXPECT_NEAR(std::abs(dot(expected, actual)), 1.0f, EPS);
    }
}

TEST(Rotation, QuaternionFromRotationMatrixIgnoresTranslation)
{
    const Quaternion expected =
        from_axis_angle({ 0.0f, 1.0f, 0.0f }, PI * 0.25f);
    Mat4 m = rotation(expected);
    m.m[12] = 100.0f;
    m.m[13] = -20.0f;
    m.m[14] = 5.0f;

    const Quaternion actual = from_rotation_matrix(m);

    EXPECT_NEAR(std::abs(dot(normalize(expected), actual)), 1.0f, EPS);
}
