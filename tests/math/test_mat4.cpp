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

// rigid_pose_from_matrix recovers the same translation + orientation as a clean
// TRS, dropping (uniform) scale -- the steady-state scene-camera case where the
// camera node sits under a scaled parent (e.g. the tank at scale 0.5).
TEST(Mat4, RigidPoseFromMatrixRecoversPoseDroppingUniformScale)
{
    Transform t;
    t.position = { 60.0f, 27.0f, 46.0f };
    t.rotation = normalize(Quaternion{ -0.0308f, 0.7064f, -0.0308f, -0.7064f });
    t.scale = { 0.5f, 0.5f, 0.5f };

    const Transform pose = rigid_pose_from_matrix(transform(t));

    EXPECT_NEAR(pose.position.x, t.position.x, EPS);
    EXPECT_NEAR(pose.position.y, t.position.y, EPS);
    EXPECT_NEAR(pose.position.z, t.position.z, EPS);

    // Scale is always discarded (cameras don't scale the view).
    EXPECT_NEAR(pose.scale.x, 1.0f, EPS);
    EXPECT_NEAR(pose.scale.y, 1.0f, EPS);
    EXPECT_NEAR(pose.scale.z, 1.0f, EPS);

    // Orientation matches: compare the recovered and original rotations by their
    // action on a probe vector (sidesteps quaternion double-cover sign).
    const Vec3 probe{ 1.0f, 2.0f, 3.0f };
    const Vec3 got = mul_vector(rotation(pose.rotation), probe);
    const Vec3 want = mul_vector(rotation(t.rotation), probe);
    EXPECT_NEAR(got.x, want.x, 1e-4f);
    EXPECT_NEAR(got.y, want.y, 1e-4f);
    EXPECT_NEAR(got.z, want.z, 1e-4f);
}

// REGRESSION (#219 scene-camera flip): a world matrix that decompose_trs REJECTS
// on its strict orthogonality gate -- as the tank's per-frame terrain-alignment
// rotation drift produces -- must STILL yield the node's translation and a valid
// unit-quaternion orientation. The old camera path fed such a matrix through
// decompose_trs, ignored the false return, and used the resulting identity pose,
// snapping the camera to the origin (the intermittent "flip to free-fly").
TEST(Mat4, RigidPoseFromMatrixPreservesPoseWhereDecomposeTrsRejects)
{
    // Rotation + uniform scale with a small shear added to one basis column,
    // standing in for accumulated floating-point drift, plus a non-origin
    // translation.
    Transform base;
    base.position = { 60.0f, 27.0f, 46.0f };
    base.rotation = normalize(Quaternion{ -0.0308f, 0.7064f, -0.0308f, -0.7064f });
    base.scale = { 0.5f, 0.5f, 0.5f };

    Mat4 m = transform(base);
    m.m[4] += 0.01f;   // perturb the Y basis column -> non-orthonormal basis

    // Precondition: decompose_trs rejects this matrix (the old failure path).
    Transform rejected{};
    ASSERT_FALSE(decompose_trs(m, rejected));

    // The robust extraction keeps the camera at the node, not the origin.
    const Transform pose = rigid_pose_from_matrix(m);
    EXPECT_NEAR(pose.position.x, 60.0f, EPS);
    EXPECT_NEAR(pose.position.y, 27.0f, EPS);
    EXPECT_NEAR(pose.position.z, 46.0f, EPS);

    // And produces a finite, unit-length orientation (no NaN, no collapse).
    const float qlen = std::sqrt(
        pose.rotation.x * pose.rotation.x + pose.rotation.y * pose.rotation.y
        + pose.rotation.z * pose.rotation.z + pose.rotation.w * pose.rotation.w);
    EXPECT_NEAR(qlen, 1.0f, 1e-4f);
}

// A genuinely degenerate basis column carries no orientation, so the rotation
// falls back to identity -- but the translation is still preserved (not origin).
TEST(Mat4, RigidPoseFromMatrixDegenerateColumnFallsBackToIdentityRotation)
{
    Mat4 m = Mat4::identity();
    m.m[0] = 0.0f; m.m[1] = 0.0f; m.m[2] = 0.0f;   // zero-length X column
    m.m[12] = 5.0f; m.m[13] = 6.0f; m.m[14] = 7.0f;

    const Transform pose = rigid_pose_from_matrix(m);
    EXPECT_NEAR(pose.position.x, 5.0f, EPS);
    EXPECT_NEAR(pose.position.y, 6.0f, EPS);
    EXPECT_NEAR(pose.position.z, 7.0f, EPS);

    EXPECT_NEAR(pose.rotation.x, 0.0f, EPS);
    EXPECT_NEAR(pose.rotation.y, 0.0f, EPS);
    EXPECT_NEAR(pose.rotation.z, 0.0f, EPS);
    EXPECT_NEAR(pose.rotation.w, 1.0f, EPS);
}


// ── Hostile-matrix table for decompose_trs (issue #314, C1-C1) ───────────────
//
// decompose_trs is the engine's designated "reject a matrix that must not be
// used" gate: twelve call sites are written as
//     if (!decompose_trs(...)) { keep the authored value; return; }
// Every one of its gates is a comparison, and every comparison against NaN is
// false -- the ACCEPT branch -- so before the finiteness check it returned TRUE
// for an all-NaN matrix with scale = (nan, 1, 1).
//
// Drive decompose_trs DIRECTLY, not a caller: the callers are the twelve places
// that trust it, and testing one of those tests the caller's guard instead.
TEST(Mat4, DecomposeTrsRejectsNonFiniteMatrices)
{
    const float nan_value = std::nanf("");
    const float inf_value = INFINITY;

    // Control: the same matrix with finite entries decomposes cleanly, so a
    // failure below means the finiteness check fired and nothing else.
    {
        Transform t;
        t.position = { 1.0f, 2.0f, 3.0f };
        t.rotation = Quaternion::identity();
        t.scale = { 2.0f, 3.0f, 4.0f };
        Transform decomposed{};
        ASSERT_TRUE(decompose_trs(transform(t), decomposed));
        EXPECT_NEAR(decomposed.scale.x, 2.0f, EPS);
    }

    // A non-finite entry in ANY cell must be refused -- basis, translation and
    // the projective row alike.
    for (int cell = 0; cell < 16; ++cell) {
        for (const float hostile : { nan_value, inf_value, -inf_value }) {
            Mat4 m = Mat4::identity();
            m.m[cell] = hostile;
            Transform decomposed{};
            EXPECT_FALSE(decompose_trs(m, decomposed))
                << "cell " << cell << " admitted a non-finite value";
        }
    }
}

// rigid_pose_from_matrix is deliberately the LENIENT twin -- it has no validity
// gates by design, so that a camera under a slightly non-orthonormal parent
// keeps its orientation instead of snapping to the origin. Pin what it actually
// does with a NaN basis so the behaviour is a decision and not an accident: it
// folds to identity rotation, because from_rotation_matrix's fallback branch
// computes sqrt(std::max(0.0f, NaN)) and std::max returns the FIRST argument
// there. Swap those two arguments and NaN propagates instead.
TEST(Mat4, RigidPoseFromMatrixFoldsNonFiniteBasisToIdentityRotation)
{
    Mat4 m = Mat4::identity();
    m.m[0] = std::nanf("");

    const Transform pose = rigid_pose_from_matrix(m);

    EXPECT_FLOAT_EQ(pose.rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(pose.rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(pose.rotation.z, 0.0f);
    EXPECT_FLOAT_EQ(pose.rotation.w, 1.0f);
}
