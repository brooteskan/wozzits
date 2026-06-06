#include <gtest/gtest.h>
#include <math/mat4.h>
#include <math/quaternion.h>
#include <math/vec3.h>

#include <cmath>

using namespace wz::math;

namespace
{
    constexpr float EPS = 1e-5f;
    constexpr float PI = 3.14159265358979323846f;

    void expect_mat4_near(const Mat4& a, const Mat4& b, float eps = EPS)
    {
        for (int i = 0; i < 16; ++i) {
            EXPECT_NEAR(a.m[i], b.m[i], eps) << "index " << i;
        }
    }
}

TEST(Mat4, Identity_PreservesPoint)
{
    Mat4 I = Mat4::identity();

    Vec3 p{ 1, 2, 3 };

    Vec3 r = mul_point(I, p);

    EXPECT_FLOAT_EQ(r.x, 1);
    EXPECT_FLOAT_EQ(r.y, 2);
    EXPECT_FLOAT_EQ(r.z, 3);
}

TEST(Mat4, Translation_ShiftsPoint)
{
    Mat4 T = translation({ 10, 0, 0 });

    Vec3 p{ 1, 2, 3 };

    Vec3 r = mul_point(T, p);

    EXPECT_FLOAT_EQ(r.x, 11);
    EXPECT_FLOAT_EQ(r.y, 2);
    EXPECT_FLOAT_EQ(r.z, 3);
}

TEST(Mat4, Scale_ScalesPoint)
{
    Mat4 S = scale({ 2, 2, 2 });

    Vec3 p{ 1, 2, 3 };

    Vec3 r = mul_point(S, p);

    EXPECT_FLOAT_EQ(r.x, 2);
    EXPECT_FLOAT_EQ(r.y, 4);
    EXPECT_FLOAT_EQ(r.z, 6);
}

TEST(Mat4, RotationMatchesQuaternion)
{
    Vec3 v{ 1,0,0 };

    Quaternion q = from_axis_angle({ 0,0,1 }, PI * 0.5f);

    Vec3 v1 = rotate(q, v);

    Mat4 R = rotation(q);
    Vec3 v2 = mul_vector(R, v);

    EXPECT_NEAR(v1.x, v2.x, 1e-5f);
    EXPECT_NEAR(v1.y, v2.y, 1e-5f);
    EXPECT_NEAR(v1.z, v2.z, 1e-5f);
}

TEST(Mat4, TransformComposition)
{
    Transform t;
    t.position = { 10,0,0 };
    t.scale = { 2,2,2 };
    t.rotation = Quaternion::identity(); // quaternion identity

    Mat4 M = transform(t);

    Vec3 v{ 1,0,0 };
    Vec3 r = mul_point(M, v);

    // scale then translate → (1*2)+10 = 12
    EXPECT_FLOAT_EQ(r.x, 12);
}

TEST(Mat4, RotationZ90)
{
    Quaternion q = from_axis_angle({ 0,0,1 }, PI * 0.5f);

    Mat4 R = rotation(q);

    Vec3 v{ 1,0,0 };
    Vec3 r = mul_vector(R, v);

    EXPECT_NEAR(r.x, 0.0f, 1e-5f);
    EXPECT_NEAR(r.y, 1.0f, 1e-5f);
}

TEST(Mat4, DecomposeTrsRoundTripsPositiveNonUniformScale)
{
    Transform t;
    t.position = { 3.0f, -2.0f, 5.0f };
    t.rotation = from_axis_angle(normalize(Vec3{ 1.0f, 2.0f, 3.0f }), 1.1f);
    t.scale = { 2.0f, 3.0f, 4.0f };

    Transform decomposed{};
    ASSERT_TRUE(decompose_trs(transform(t), decomposed));

    EXPECT_NEAR(decomposed.position.x, t.position.x, EPS);
    EXPECT_NEAR(decomposed.position.y, t.position.y, EPS);
    EXPECT_NEAR(decomposed.position.z, t.position.z, EPS);
    EXPECT_NEAR(std::abs(dot(normalize(t.rotation), decomposed.rotation)),
        1.0f,
        EPS);
    EXPECT_NEAR(decomposed.scale.x, t.scale.x, EPS);
    EXPECT_NEAR(decomposed.scale.y, t.scale.y, EPS);
    EXPECT_NEAR(decomposed.scale.z, t.scale.z, EPS);
    expect_mat4_near(transform(decomposed), transform(t));
}

TEST(Mat4, DecomposeTrsIdentity)
{
    Transform decomposed{};
    ASSERT_TRUE(decompose_trs(Mat4::identity(), decomposed));

    EXPECT_FLOAT_EQ(decomposed.position.x, 0.0f);
    EXPECT_FLOAT_EQ(decomposed.position.y, 0.0f);
    EXPECT_FLOAT_EQ(decomposed.position.z, 0.0f);
    EXPECT_NEAR(
        std::abs(dot(Quaternion::identity(), decomposed.rotation)),
        1.0f,
        EPS);
    EXPECT_FLOAT_EQ(decomposed.scale.x, 1.0f);
    EXPECT_FLOAT_EQ(decomposed.scale.y, 1.0f);
    EXPECT_FLOAT_EQ(decomposed.scale.z, 1.0f);
}

TEST(Mat4, DecomposeTrsRoundTripsUniformScale)
{
    Transform t;
    t.position = { -4.0f, 6.0f, 2.0f };
    t.rotation = from_axis_angle({ 0.0f, 1.0f, 0.0f }, PI * 0.25f);
    t.scale = { 5.0f, 5.0f, 5.0f };

    Transform decomposed{};
    ASSERT_TRUE(decompose_trs(transform(t), decomposed));

    EXPECT_NEAR(std::abs(dot(normalize(t.rotation), decomposed.rotation)),
        1.0f,
        EPS);
    EXPECT_NEAR(decomposed.scale.x, 5.0f, EPS);
    EXPECT_NEAR(decomposed.scale.y, 5.0f, EPS);
    EXPECT_NEAR(decomposed.scale.z, 5.0f, EPS);
    expect_mat4_near(transform(decomposed), transform(t));
}

TEST(Mat4, DecomposeTrsRepeatedRoundTripIsStable)
{
    Transform t;
    t.position = { 1.0f, 2.0f, 3.0f };
    t.rotation = from_axis_angle(normalize(Vec3{ 2.0f, -1.0f, 4.0f }), 0.9f);
    t.scale = { 1.5f, 2.5f, 3.5f };

    Transform first{};
    ASSERT_TRUE(decompose_trs(transform(t), first));

    Transform second{};
    ASSERT_TRUE(decompose_trs(transform(first), second));

    EXPECT_NEAR(first.position.x, second.position.x, EPS);
    EXPECT_NEAR(first.position.y, second.position.y, EPS);
    EXPECT_NEAR(first.position.z, second.position.z, EPS);
    EXPECT_NEAR(std::abs(dot(first.rotation, second.rotation)), 1.0f, EPS);
    EXPECT_NEAR(first.scale.x, second.scale.x, EPS);
    EXPECT_NEAR(first.scale.y, second.scale.y, EPS);
    EXPECT_NEAR(first.scale.z, second.scale.z, EPS);
    expect_mat4_near(transform(first), transform(second));
}

TEST(Mat4, DecomposeTrsRejectsDegenerateScale)
{
    Transform t;
    t.position = { 0.0f, 0.0f, 0.0f };
    t.rotation = Quaternion::identity();
    t.scale = { 0.0f, 1.0f, 1.0f };

    Transform decomposed{};
    EXPECT_FALSE(decompose_trs(transform(t), decomposed));
}

TEST(Mat4, DecomposeTrsRejectsShear)
{
    Mat4 m = Mat4::identity();
    m.m[4] = 0.25f;

    Transform decomposed{};
    EXPECT_FALSE(decompose_trs(m, decomposed));
}

TEST(Mat4, DecomposeTrsRejectsReflection)
{
    Transform t;
    t.position = { 0.0f, 0.0f, 0.0f };
    t.rotation = Quaternion::identity();
    t.scale = { -1.0f, 1.0f, 1.0f };

    Transform decomposed{};
    EXPECT_FALSE(decompose_trs(transform(t), decomposed));
}

