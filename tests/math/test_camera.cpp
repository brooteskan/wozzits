#include <gtest/gtest.h>
#include <math/vec3.h>
#include <math/quaternion.h>
#include <math/mat4.h>

#include <math/camera.h>
#include <math/projection.h>
#include <math/frustum.h>

using namespace wz::math;

TEST(CoordinateTransform, ObjectInFrontOfCamera)
{
    Transform camera;
    camera.position = { 0, 0, 0 };
    camera.rotation = Quaternion::identity();
    camera.scale = { 1,1,1 };

    Transform obj;
    obj.position = { 0, 0, -5 };
    obj.rotation = Quaternion::identity();
    obj.scale = { 1,1,1 };

    Mat4 V = view_matrix(camera);
    Mat4 W = transform(obj);

    Vec3 p = mul_point(mul(V, W), { 0,0,0 });

    EXPECT_LT(p.z, 0.0f); // in front in view space
}

TEST(CoordinateTransform, CameraLooksDownNegativeZ)
{
    Transform camera;
    camera.position = { 0,0,0 };
    camera.rotation = Quaternion::identity();

    Mat4 V = view_matrix(camera);

    Vec3 forward_world = { 0,0,-1 };

    Vec3 forward_view = mul_vector(V, forward_world);

    EXPECT_NEAR(forward_view.z, -1.0f, 1e-5f);
}

TEST(CoordinateTransform, ProjectionDepthSign)
{
    Mat4 P = projection_perspective(1.0f, 1.0f, 0.1f, 100.0f);

    Vec4 front = vec4_mul_point(P, { 0, 0, -1 });
    Vec4 back = vec4_mul_point(P, { 0, 0,  1 });

    // perspective divide to compare depth meaningfully
    float front_ndc_z = front.z / front.w;
    float back_ndc_z = back.z / back.w;

    EXPECT_LT(front_ndc_z, back_ndc_z);
}

TEST(ProjectionDx, DepthRangeZeroToOne)
{
    constexpr float near_z = 0.1f;
    constexpr float far_z  = 100.0f;

    Mat4 P = projection_perspective_dx(1.0f, 1.0f, near_z, far_z);

    Vec4 at_near = vec4_mul_point(P, { 0, 0, near_z });
    Vec4 at_far  = vec4_mul_point(P, { 0, 0, far_z  });

    EXPECT_NEAR(at_near.z / at_near.w, 0.0f, 1e-5f);
    EXPECT_NEAR(at_far.z  / at_far.w,  1.0f, 1e-5f);
}

TEST(ProjectionDx, LeftHandedPositiveZForward)
{
    // m[11] = +1 encodes left-handed clip: w_clip = +z (unlike OpenGL's -z)
    Mat4 P = projection_perspective_dx(1.0f, 1.0f, 0.1f, 100.0f);

    EXPECT_FLOAT_EQ(P.m[11], 1.0f);
}

TEST(Frustum, PointInFrontIsInside)
{
    Mat4 V = view_matrix({ {0,0,0}, Quaternion::identity(), {1,1,1} });
    Mat4 P = projection_perspective(1.0f, 1.0f, 0.1f, 100.0f);

    Frustum f = frustum_from_view_projection(mul(P, V));

    EXPECT_TRUE(contains_point(f, { 0, 0, -5 }));
}

TEST(Frustum, PointBehindIsOutside)
{
    Mat4 V = view_matrix({ {0,0,0}, Quaternion::identity(), {1,1,1} });
    Mat4 P = projection_perspective(1.0f, 1.0f, 0.1f, 100.0f);

    Frustum f = frustum_from_view_projection(mul(P, V));

    EXPECT_FALSE(contains_point(f, { 0, 0, 5 }));
}

TEST(Frustum, FarPlaneRejects)
{
    Mat4 V = view_matrix({ {0,0,0}, Quaternion::identity(), {1,1,1} });
    Mat4 P = projection_perspective(1.0f, 1.0f, 0.1f, 10.0f);

    Frustum f = frustum_from_view_projection(mul(P, V));

    EXPECT_FALSE(contains_point(f, { 0, 0, -50 }));
}

TEST(Frustum, SphereInside)
{
    Camera cam;
    cam.transform = { {0,0,0}, Quaternion::identity(), {1,1,1} };
    cam.fov_y = 1.0f;
    cam.aspect = 1.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = 100.0f;

    Mat4 V = view_matrix(cam);
    Mat4 P = projection_perspective(cam);

    Frustum f = frustum_from_view_projection(mul(P, V));

    EXPECT_TRUE(intersects_sphere(f, { 0,0,-5 }, 1.0f));
}

TEST(Frustum, SphereOutside)
{
    Camera cam;
    cam.transform = { {0,0,0}, Quaternion::identity(), {1,1,1} };
    cam.fov_y = 1.0f;
    cam.aspect = 1.0f;
    cam.near_plane = 0.1f;
    cam.far_plane = 100.0f;

    Mat4 V = view_matrix(cam);
    Mat4 P = projection_perspective(cam);

    Frustum f = frustum_from_view_projection(mul(P, V));

    EXPECT_FALSE(intersects_sphere(f, { 0,0,5 }, 1.0f));
}
// ── view_matrix composition order (issue #314, C1-C7) ───────────────────────
//
// A view matrix must send the camera's own world position to the origin. The
// composition was mul(R, T) with T already carrying a qinv-rotated translation,
// so the translation was rotated TWICE. Measured before the fix: a camera at
// (10,0,0) rotated 90 degrees about Y mapped its own position to (10,0,10).
//
// LOAD-BEARING: the camera must be BOTH translated AND rotated. Every existing
// test in this file places it at the origin or at identity rotation, which are
// exactly the two configurations where mul(R,T) and mul(T,R) agree -- that is
// why a wrong view matrix sat here green.
TEST(CoordinateTransform, ViewMatrixSendsARotatedCameraPositionToTheOrigin)
{
    Transform camera;
    camera.position = { 10.0f, -4.0f, 7.0f };
    camera.rotation = from_axis_angle({ 0.0f, 1.0f, 0.0f }, 1.5707963f);
    camera.scale = { 1, 1, 1 };

    const Vec3 origin = mul_point(view_matrix(camera), camera.position);

    EXPECT_NEAR(origin.x, 0.0f, 1e-4f);
    EXPECT_NEAR(origin.y, 0.0f, 1e-4f);
    EXPECT_NEAR(origin.z, 0.0f, 1e-4f);
}

// The view of an arbitrary point must equal rotate(qinv, p - eye) -- the same
// claim stated positively, so a future rewrite has the definition to hand.
TEST(CoordinateTransform, ViewMatrixMatchesRotateOfTheRelativePosition)
{
    Transform camera;
    camera.position = { 3.0f, 2.0f, -6.0f };
    camera.rotation = normalize(Quaternion{ 0.2f, 0.5f, -0.1f, 0.8f });
    camera.scale = { 1, 1, 1 };

    const Quaternion qinv{
        -camera.rotation.x, -camera.rotation.y, -camera.rotation.z,
         camera.rotation.w };
    const Vec3 world{ 11.0f, -3.0f, 4.0f };

    const Vec3 via_matrix = mul_point(view_matrix(camera), world);
    const Vec3 expected = rotate(qinv, world - camera.position);

    EXPECT_NEAR(via_matrix.x, expected.x, 1e-4f);
    EXPECT_NEAR(via_matrix.y, expected.y, 1e-4f);
    EXPECT_NEAR(via_matrix.z, expected.z, 1e-4f);
}

// ── look_at_dx at the poles (issue #314, C1-C8) ─────────────────────────────
//
// cross(up, forward) is zero when the two are parallel, and normalize() of that
// is zeros, so a straight-up or straight-down view produced a basis whose x and
// z columns were both (0,0,0). Measured: looking down from (0,10,0) with up
// +Y gave basis = [0 0 0 | 0 0 -1 | 0 0 0]. Reached from the wozzits-imgui
// tools, which call look_at_dx at five sites -- a top-down view is an ordinary
// thing to ask a mesh or splat tool for.
TEST(Camera, LookAtDxStaysAWellFormedBasisAtThePoles)
{
    const auto expect_orthonormal = [](const Mat4& m, const char* what) {
        const Vec3 col0{ m.m[0], m.m[4], m.m[8] };
        const Vec3 col1{ m.m[1], m.m[5], m.m[9] };
        const Vec3 col2{ m.m[2], m.m[6], m.m[10] };
        EXPECT_NEAR(length(col0), 1.0f, 1e-4f) << what << " x";
        EXPECT_NEAR(length(col1), 1.0f, 1e-4f) << what << " y";
        EXPECT_NEAR(length(col2), 1.0f, 1e-4f) << what << " z";
        EXPECT_NEAR(dot(col0, col1), 0.0f, 1e-4f) << what << " x.y";
        EXPECT_NEAR(dot(col0, col2), 0.0f, 1e-4f) << what << " x.z";
        EXPECT_NEAR(dot(col1, col2), 0.0f, 1e-4f) << what << " y.z";
    };

    // Control: an ordinary view, which was always fine.
    expect_orthonormal(
        look_at_dx({ 0, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }), "control");

    expect_orthonormal(
        look_at_dx({ 0, 10, 0 }, { 0, 0, 0 }, { 0, 1, 0 }), "straight down");
    expect_orthonormal(
        look_at_dx({ 0, 0, 0 }, { 0, 10, 0 }, { 0, 1, 0 }), "straight up");
}
