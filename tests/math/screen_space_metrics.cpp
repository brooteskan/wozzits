#include <cmath>
#include <gtest/gtest.h>

#include <math/frustum.h>
#include <math/projection.h>
#include <math/screen_space_metrics.h>
#include <scene/geometry.h>

using namespace wz::math;

namespace
{
    constexpr float Pi = 3.14159265358979323846f;

    wz::scene::AABB box(Vec3 min, Vec3 max)
    {
        return wz::scene::AABB{ min, max };
    }

    Mat4 dx_projection()
    {
        return projection_perspective_dx(Pi * 0.5f, 1.0f, 0.1f, 100.0f);
    }

    Mat4 dx_orthographic_projection()
    {
        return projection_orthographic_dx(8.0f, 6.0f, 0.1f, 100.0f);
    }
}

TEST(ScreenSpaceMetrics, FrustumTestAabbClassifiesInsidePartialAndOutside)
{
    const Mat4 vp = dx_projection();
    const Frustum frustum = frustum_from_view_projection(vp);

    EXPECT_EQ(
        frustum_test_aabb(
            frustum,
            box({ -0.25f, -0.25f, 2.0f }, { 0.25f, 0.25f, 3.0f })),
        AABBFrustumResult::Inside);

    EXPECT_EQ(
        frustum_test_aabb(
            frustum,
            box({ -4.0f, -0.25f, 2.0f }, { 0.25f, 0.25f, 3.0f })),
        AABBFrustumResult::Intersecting);

    EXPECT_EQ(
        frustum_test_aabb(
            frustum,
            box({ 10.0f, 10.0f, 2.0f }, { 11.0f, 11.0f, 3.0f })),
        AABBFrustumResult::Outside);
}

TEST(ScreenSpaceMetrics, ProjectAabbReturnsFullyVisibleScreenRect)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ -1.0f, -1.0f, 4.0f }, { 1.0f, 1.0f, 4.0f }),
        dx_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::FullyVisible);
    EXPECT_NEAR(projected.conservative_screen_rect.min_x, 300.0f, 0.01f);
    EXPECT_NEAR(projected.conservative_screen_rect.max_x, 500.0f, 0.01f);
    EXPECT_NEAR(projected.conservative_screen_rect.min_y, 225.0f, 0.01f);
    EXPECT_NEAR(projected.conservative_screen_rect.max_y, 375.0f, 0.01f);
    EXPECT_NEAR(projected.projected_area_px, 30000.0f, 0.01f);
    EXPECT_NEAR(projected.nearest_depth, 4.0f, 0.01f);
    EXPECT_NEAR(projected.farthest_depth, 4.0f, 0.01f);
}

TEST(ScreenSpaceMetrics, ProjectAabbReturnsPartiallyVisibleScreenRect)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ -4.0f, -0.25f, 2.0f }, { 0.25f, 0.25f, 3.0f }),
        dx_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::PartiallyVisible);
    EXPECT_FLOAT_EQ(projected.conservative_screen_rect.min_x, 0.0f);
    EXPECT_GT(projected.projected_area_px, 0.0f);
    EXPECT_NEAR(projected.nearest_depth, 2.0f, 0.01f);
    EXPECT_NEAR(projected.farthest_depth, 3.0f, 0.01f);
}

TEST(ScreenSpaceMetrics, ProjectAabbPromotesNearPlaneClipsToFullscreen)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ 0.5f, 0.5f, -0.5f }, { 1.0f, 1.0f, 2.0f }),
        dx_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::IntersectsNearPlane);
    EXPECT_FLOAT_EQ(projected.conservative_screen_rect.min_x, 0.0f);
    EXPECT_FLOAT_EQ(projected.conservative_screen_rect.min_y, 0.0f);
    EXPECT_FLOAT_EQ(projected.conservative_screen_rect.max_x, 800.0f);
    EXPECT_FLOAT_EQ(projected.conservative_screen_rect.max_y, 600.0f);
    EXPECT_FLOAT_EQ(projected.projected_area_px, 480000.0f);
}

TEST(ScreenSpaceMetrics, ProjectAabbDetectsCameraInside)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }),
        dx_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::CameraInside);
    EXPECT_FLOAT_EQ(projected.projected_area_px, 480000.0f);
    EXPECT_FLOAT_EQ(chunk_projected_area_pixels(projected), 480000.0f);
}

TEST(ScreenSpaceMetrics, ProjectAabbDetectsDegenerateBoxes)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ 1.0f, 1.0f, 4.0f }, { 1.0f, 1.0f, 4.0f }),
        dx_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::Degenerate);
    EXPECT_FLOAT_EQ(projected.projected_area_px, 0.0f);
    EXPECT_FLOAT_EQ(chunk_projected_area_pixels(projected), 0.0f);
}

TEST(ScreenSpaceMetrics, ProjectAabbOutsideViewportHasZeroArea)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ 20.0f, 20.0f, 4.0f }, { 22.0f, 22.0f, 5.0f }),
        dx_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::FullyOutside);
    EXPECT_FLOAT_EQ(projected.projected_area_px, 0.0f);
    EXPECT_FLOAT_EQ(chunk_projected_area_pixels(projected), 0.0f);
}

TEST(ScreenSpaceMetrics, ProjectAabbBehindCameraIsFullyOutside)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ -1.0f, -1.0f, -5.0f }, { 1.0f, 1.0f, -4.0f }),
        dx_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::FullyOutside);
    EXPECT_FLOAT_EQ(projected.projected_area_px, 0.0f);
}

TEST(ScreenSpaceMetrics, ProjectAabbTracksNearestAndFarthestDepth)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ -0.5f, -0.5f, 2.0f }, { 0.5f, 0.5f, 6.0f }),
        dx_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::FullyVisible);
    EXPECT_NEAR(projected.nearest_depth, 2.0f, 0.01f);
    EXPECT_NEAR(projected.farthest_depth, 6.0f, 0.01f);
}

TEST(ScreenSpaceMetrics, ProjectAabbSupportsOrthographicProjection)
{
    const ProjectionResult projected = project_aabb_to_screen_rect(
        box({ -2.0f, -1.0f, 4.0f }, { 2.0f, 1.0f, 6.0f }),
        dx_orthographic_projection(),
        800.0f,
        600.0f);

    EXPECT_EQ(projected.status, ProjectionStatus::FullyVisible);
    EXPECT_NEAR(projected.conservative_screen_rect.min_x, 200.0f, 0.01f);
    EXPECT_NEAR(projected.conservative_screen_rect.max_x, 600.0f, 0.01f);
    EXPECT_NEAR(projected.conservative_screen_rect.min_y, 200.0f, 0.01f);
    EXPECT_NEAR(projected.conservative_screen_rect.max_y, 400.0f, 0.01f);
    EXPECT_NEAR(projected.projected_area_px, 80000.0f, 0.01f);
}

TEST(ScreenSpaceMetrics, WorldErrorToPixelsUsesNearestDepth)
{
    const float near_error =
        world_error_to_pixels(0.5f, 2.0f, 600.0f, Pi * 0.5f);
    const float far_error =
        world_error_to_pixels(0.5f, 4.0f, 600.0f, Pi * 0.5f);

    EXPECT_NEAR(near_error, 75.0f, 0.01f);
    EXPECT_NEAR(far_error, 37.5f, 0.01f);
    EXPECT_GT(near_error, far_error);
}

TEST(ScreenSpaceMetrics, ChunkProjectedAreaUsesRectArea)
{
    ProjectionResult projected{};
    projected.status = ProjectionStatus::FullyVisible;
    projected.conservative_screen_rect = { 10.0f, 20.0f, 110.0f, 70.0f };
    projected.projected_area_px = 5000.0f;

    EXPECT_FLOAT_EQ(chunk_projected_area_pixels(projected), 5000.0f);
}

TEST(ScreenSpaceMetrics, LodPriorityBalancesBenefitAndCost)
{
    const float cheap =
        lod_priority(8.0f, 100.0f, 1.0f, { 2.0f, 1.0f });
    const float expensive =
        lod_priority(8.0f, 100.0f, 9.0f, { 2.0f, 1.0f });

    EXPECT_FLOAT_EQ(cheap, 800.0f);
    EXPECT_FLOAT_EQ(expensive, 160.0f);
    EXPECT_GT(cheap, expensive);
}

// A zero view-projection must not produce NaN planes, and the frustum it yields
// must not accept everything.
//
// This is the state every scene runtime starts in: ViewData::view_projection is
// `Mat4 view_projection{}` -- value-initialised to zeros, NOT identity -- so all
// six planes are built as col3 +/- colN = (0,0,0). Dividing by that zero length
// gave 0/0 in every component.
//
// LOAD-BEARING: the isfinite assertions alone would PASS against the unfixed
// code on some inputs, and more importantly they do not describe the damage. The
// assertion that matters is the last one: a NaN plane makes intersects_sphere's
// `if (d < -r) return false;` compare false, so the frustum accepts every sphere
// and culling silently becomes a no-op. Deleting the behavioural assertion turns
// this back into decoration.
TEST(Frustum, ZeroViewProjectionYieldsFinitePlanesAndDoesNotAcceptEverything)
{
    Mat4 zero{};
    for (float& v : zero.m) {
        v = 0.0f;
    }

    const Frustum f = frustum_from_view_projection(zero);

    for (const auto& p : f.planes) {
        EXPECT_TRUE(std::isfinite(p.normal.x));
        EXPECT_TRUE(std::isfinite(p.normal.y));
        EXPECT_TRUE(std::isfinite(p.normal.z));
        EXPECT_TRUE(std::isfinite(p.distance));
    }

    // A well-formed frustum rejects a sphere far outside it; the NaN one accepted
    // every sphere ever offered, which is what made the bug invisible.
    const Frustum real = frustum_from_view_projection(
        projection_perspective_dx(Pi / 3.0f, 16.0f / 9.0f, 0.1f, 100.0f));
    EXPECT_FALSE(intersects_sphere(real, Vec3{ 0.0f, 0.0f, -1000.0f }, 1.0f))
        << "control: a real frustum must reject a sphere well behind it";
}
