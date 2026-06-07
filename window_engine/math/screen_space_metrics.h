#pragma once

#include <math/math_types.h>

#include <cstdint>

namespace wz::scene
{
    struct AABB;
}

namespace wz::math
{
    enum class AABBFrustumResult : uint8_t
    {
        Outside,
        Intersecting,
        Inside,
    };

    enum class ProjectionStatus : uint8_t
    {
        FullyOutside,
        FullyVisible,
        PartiallyVisible,
        IntersectsNearPlane,
        CameraInside,
        Degenerate,
    };

    struct ScreenRect
    {
        float min_x = 0.0f;
        float min_y = 0.0f;
        float max_x = 0.0f;
        float max_y = 0.0f;
    };

    struct ProjectionResult
    {
        ProjectionStatus status = ProjectionStatus::FullyOutside;
        ScreenRect conservative_screen_rect{};
        float projected_area_px = 0.0f;
        float nearest_depth = 0.0f;
        float farthest_depth = 0.0f;
    };

    struct LodPriorityWeights
    {
        float benefit = 1.0f;
        float cost = 1.0f;
    };

    AABBFrustumResult frustum_test_aabb(
        const Frustum& frustum,
        const wz::scene::AABB& aabb) noexcept;

    bool intersects_aabb(
        const Frustum& frustum,
        const wz::scene::AABB& aabb) noexcept;

    ProjectionResult project_aabb_to_screen_rect(
        const wz::scene::AABB& aabb,
        const Mat4& view_projection,
        float viewport_width,
        float viewport_height) noexcept;

    float world_error_to_pixels(
        float world_error,
        float nearest_depth,
        float viewport_height,
        float fov_y_radians) noexcept;

    float chunk_projected_area_pixels(const ProjectionResult& projection) noexcept;

    float lod_priority(
        float error_pixels,
        float projected_area_pixels,
        float render_cost,
        LodPriorityWeights weights = {}) noexcept;
}
