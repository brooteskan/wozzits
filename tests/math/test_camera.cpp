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