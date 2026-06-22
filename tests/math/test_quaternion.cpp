#include <gtest/gtest.h>
#include <math/vec3.h>
#include <math/quaternion.h>
#include <math/compare.h>

using namespace wz::math;

TEST(Quaternion, Identity)
{
    Vec3 v{ 1,2,3 };
    Vec3 r = rotate(Quaternion::identity(), v);

    EXPECT_TRUE(near_equal(v, r));
}

TEST(Quaternion, Rotate_Z_90)
{
    Vec3 v{ 1,0,0 };

    Quaternion q = from_axis_angle({ 0,0,1 }, 3.14159265f * 0.5f);

    Vec3 r = rotate(q, v);

    EXPECT_NEAR(r.x, 0.0f, 1e-5f);
    EXPECT_NEAR(r.y, 1.0f, 1e-5f);
    EXPECT_NEAR(r.z, 0.0f, 1e-5f);
}

TEST(Quaternion, LengthPreserved)
{
    Vec3 v{ 3,4,0 };

    Quaternion q = from_axis_angle({ 0,1,0 }, 1.0f);

    Vec3 r = rotate(q, v);

    EXPECT_NEAR(length(v), length(r), 1e-5f);
}

TEST(Quaternion, FromEulerDegrees_Identity)
{
    Vec3 v{ 1, 2, 3 };
    Vec3 r = rotate(quaternion_from_euler_degrees(0.f, 0.f, 0.f), v);
    EXPECT_TRUE(near_equal(v, r));
}

TEST(Quaternion, FromEulerDegrees_Z90_RotatesXToY)
{
    Vec3 r = rotate(quaternion_from_euler_degrees(0.f, 0.f, 90.f), { 1, 0, 0 });
    EXPECT_NEAR(r.x, 0.0f, 1e-5f);
    EXPECT_NEAR(r.y, 1.0f, 1e-5f);
    EXPECT_NEAR(r.z, 0.0f, 1e-5f);
}

TEST(Quaternion, FromEulerDegrees_X90_RotatesYToZ)
{
    Vec3 r = rotate(quaternion_from_euler_degrees(90.f, 0.f, 0.f), { 0, 1, 0 });
    EXPECT_NEAR(r.x, 0.0f, 1e-5f);
    EXPECT_NEAR(r.y, 0.0f, 1e-5f);
    EXPECT_NEAR(r.z, 1.0f, 1e-5f);
}

TEST(Quaternion, FromEulerDegrees_Y90_RotatesZToX)
{
    Vec3 r = rotate(quaternion_from_euler_degrees(0.f, 90.f, 0.f), { 0, 0, 1 });
    EXPECT_NEAR(r.x, 1.0f, 1e-5f);
    EXPECT_NEAR(r.y, 0.0f, 1e-5f);
    EXPECT_NEAR(r.z, 0.0f, 1e-5f);
}

