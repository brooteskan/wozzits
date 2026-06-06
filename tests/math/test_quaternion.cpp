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

