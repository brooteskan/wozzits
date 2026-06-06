#include <gtest/gtest.h>
#include <math/vec3.h>
#include <math/compare.h>

using namespace wz::math;

TEST(Vec3, Add)
{
    Vec3 a{ 1, 2, 3 };
    Vec3 b{ 4, 5, 6 };

    Vec3 c = a + b;

    EXPECT_FLOAT_EQ(c.x, 5);
    EXPECT_FLOAT_EQ(c.y, 7);
    EXPECT_FLOAT_EQ(c.z, 9);
}

TEST(Vec3, Dot)
{
    Vec3 a{ 1, 0, 0 };
    Vec3 b{ 0, 1, 0 };

    EXPECT_FLOAT_EQ(dot(a, b), 0.0f);
}

TEST(Vec3, Cross)
{
    Vec3 a{ 1, 0, 0 };
    Vec3 b{ 0, 1, 0 };

    Vec3 c = cross(a, b);

    EXPECT_FLOAT_EQ(c.x, 0);
    EXPECT_FLOAT_EQ(c.y, 0);
    EXPECT_FLOAT_EQ(c.z, 1);
}

TEST(Vec3, Normalize)
{
    Vec3 v{ 0, 3, 0 };
    Vec3 n = normalize(v);

    EXPECT_NEAR(length(n), 1.0f, 1e-5f);
}

TEST(Vec3, NearEqual)
{
    Vec3 a{ 1.0f, 2.0f, 3.0f };
    Vec3 b{ 1.0f + 1e-7f, 2.0f, 3.0f };

    EXPECT_TRUE(near_equal(a, b));
}