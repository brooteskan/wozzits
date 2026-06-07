#include <math/screen_space_metrics.h>
#include <math/frustum.h>
#include <scene/geometry.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wz::math
{
    namespace
    {
        struct ClipPoint
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
        };

        ClipPoint transform_clip_point(
            const Mat4& m,
            const Vec3& p) noexcept
        {
            return ClipPoint{
                m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
                m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
                m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14],
                m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15],
            };
        }

        bool aabb_is_degenerate(const wz::scene::AABB& aabb) noexcept
        {
            return aabb.min.x == aabb.max.x
                && aabb.min.y == aabb.max.y
                && aabb.min.z == aabb.max.z;
        }

        bool aabb_contains_origin(const wz::scene::AABB& aabb) noexcept
        {
            return aabb.min.x <= 0.0f && aabb.max.x >= 0.0f
                && aabb.min.y <= 0.0f && aabb.max.y >= 0.0f
                && aabb.min.z <= 0.0f && aabb.max.z >= 0.0f;
        }

        float rect_area(const ScreenRect& rect) noexcept
        {
            return (std::max)(0.0f, rect.max_x - rect.min_x)
                * (std::max)(0.0f, rect.max_y - rect.min_y);
        }
    }

    AABBFrustumResult frustum_test_aabb(
        const Frustum& frustum,
        const wz::scene::AABB& aabb) noexcept
    {
        bool intersects = false;

        for (const Plane& plane : frustum.planes) {
            const Vec3 positive{
                plane.normal.x >= 0.0f ? aabb.max.x : aabb.min.x,
                plane.normal.y >= 0.0f ? aabb.max.y : aabb.min.y,
                plane.normal.z >= 0.0f ? aabb.max.z : aabb.min.z,
            };
            if (plane.normal.x * positive.x
                + plane.normal.y * positive.y
                + plane.normal.z * positive.z
                + plane.distance < 0.0f)
            {
                return AABBFrustumResult::Outside;
            }

            const Vec3 negative{
                plane.normal.x >= 0.0f ? aabb.min.x : aabb.max.x,
                plane.normal.y >= 0.0f ? aabb.min.y : aabb.max.y,
                plane.normal.z >= 0.0f ? aabb.min.z : aabb.max.z,
            };
            if (plane.normal.x * negative.x
                + plane.normal.y * negative.y
                + plane.normal.z * negative.z
                + plane.distance < 0.0f)
            {
                intersects = true;
            }
        }

        return intersects
            ? AABBFrustumResult::Intersecting
            : AABBFrustumResult::Inside;
    }

    bool intersects_aabb(
        const Frustum& frustum,
        const wz::scene::AABB& aabb) noexcept
    {
        return frustum_test_aabb(frustum, aabb)
            != AABBFrustumResult::Outside;
    }

    ProjectionResult project_aabb_to_screen_rect(
        const wz::scene::AABB& aabb,
        const Mat4& view_projection,
        float viewport_width,
        float viewport_height) noexcept
    {
        if (viewport_width <= 0.0f || viewport_height <= 0.0f)
            return {};

        if (aabb_is_degenerate(aabb)) {
            return ProjectionResult{
                .status = ProjectionStatus::Degenerate,
                .nearest_depth = 0.0f,
                .farthest_depth = 0.0f,
            };
        }

        const Frustum frustum = frustum_from_view_projection(view_projection);
        const AABBFrustumResult frustum_result =
            frustum_test_aabb(frustum, aabb);
        if (frustum_result == AABBFrustumResult::Outside) {
            return ProjectionResult{
                .status = ProjectionStatus::FullyOutside,
            };
        }

        if (aabb_contains_origin(aabb)) {
            const ScreenRect rect{ 0.0f, 0.0f, viewport_width, viewport_height };
            return ProjectionResult{
                .status = ProjectionStatus::CameraInside,
                .conservative_screen_rect = rect,
                .projected_area_px = rect_area(rect),
                .nearest_depth = 0.0f,
                .farthest_depth = 0.0f,
            };
        }

        ScreenRect rect{
            viewport_width,
            viewport_height,
            0.0f,
            0.0f,
        };
        float nearest_depth = std::numeric_limits<float>::max();
        float farthest_depth = 0.0f;
        bool has_projected_corner = false;
        bool clipped_by_near = false;

        for (int x_bit = 0; x_bit < 2; ++x_bit) {
            for (int y_bit = 0; y_bit < 2; ++y_bit) {
                for (int z_bit = 0; z_bit < 2; ++z_bit) {
                    const Vec3 corner{
                        x_bit != 0 ? aabb.max.x : aabb.min.x,
                        y_bit != 0 ? aabb.max.y : aabb.min.y,
                        z_bit != 0 ? aabb.max.z : aabb.min.z,
                    };
                    const ClipPoint p =
                        transform_clip_point(view_projection, corner);

                    if (p.w <= 0.0f) {
                        clipped_by_near = true;
                        continue;
                    }

                    const float ndc_x = p.x / p.w;
                    const float ndc_y = p.y / p.w;
                    const float ndc_z = p.z / p.w;
                    nearest_depth = (std::min)(nearest_depth, p.w);
                    farthest_depth = (std::max)(farthest_depth, p.w);
                    if (ndc_z < 0.0f || ndc_z > 1.0f) {
                        clipped_by_near = clipped_by_near || ndc_z < 0.0f;
                    }

                    const float px = (ndc_x * 0.5f + 0.5f) * viewport_width;
                    const float py = (0.5f - ndc_y * 0.5f) * viewport_height;
                    rect.min_x = (std::min)(rect.min_x, px);
                    rect.min_y = (std::min)(rect.min_y, py);
                    rect.max_x = (std::max)(rect.max_x, px);
                    rect.max_y = (std::max)(rect.max_y, py);
                    has_projected_corner = true;
                }
            }
        }

        if (!has_projected_corner)
            return {};

        rect.min_x = (std::clamp)(rect.min_x, 0.0f, viewport_width);
        rect.max_x = (std::clamp)(rect.max_x, 0.0f, viewport_width);
        rect.min_y = (std::clamp)(rect.min_y, 0.0f, viewport_height);
        rect.max_y = (std::clamp)(rect.max_y, 0.0f, viewport_height);

        if (rect.max_x <= rect.min_x || rect.max_y <= rect.min_y) {
            return ProjectionResult{
                .status = ProjectionStatus::FullyOutside,
                .conservative_screen_rect = rect,
                .nearest_depth = nearest_depth,
                .farthest_depth = farthest_depth,
            };
        }

        if (clipped_by_near) {
            const ScreenRect full_rect{
                0.0f,
                0.0f,
                viewport_width,
                viewport_height,
            };
            return ProjectionResult{
                .status = ProjectionStatus::IntersectsNearPlane,
                .conservative_screen_rect = full_rect,
                .projected_area_px = rect_area(full_rect),
                .nearest_depth = (std::max)(0.0f, nearest_depth),
                .farthest_depth = farthest_depth,
            };
        }

        return ProjectionResult{
            .status = frustum_result == AABBFrustumResult::Inside
                ? ProjectionStatus::FullyVisible
                : ProjectionStatus::PartiallyVisible,
            .conservative_screen_rect = rect,
            .projected_area_px = rect_area(rect),
            .nearest_depth = nearest_depth,
            .farthest_depth = farthest_depth,
        };
    }

    float world_error_to_pixels(
        float world_error,
        float nearest_depth,
        float viewport_height,
        float fov_y_radians) noexcept
    {
        if (world_error <= 0.0f
            || nearest_depth <= 0.0f
            || viewport_height <= 0.0f
            || fov_y_radians <= 0.0f)
        {
            return 0.0f;
        }

        const float pixels_per_world_unit =
            viewport_height
            / (2.0f * nearest_depth * std::tan(fov_y_radians * 0.5f));
        return world_error * pixels_per_world_unit;
    }

    float chunk_projected_area_pixels(
        const ProjectionResult& projection) noexcept
    {
        if (projection.status == ProjectionStatus::FullyOutside
            || projection.status == ProjectionStatus::Degenerate)
        {
            return 0.0f;
        }

        return projection.projected_area_px;
    }

    float lod_priority(
        float error_pixels,
        float projected_area_pixels,
        float render_cost,
        LodPriorityWeights weights) noexcept
    {
        const float benefit =
            (std::max)(0.0f, error_pixels)
            * (std::max)(0.0f, projected_area_pixels)
            * (std::max)(0.0f, weights.benefit);
        const float cost =
            (std::max)(0.0f, render_cost)
            * (std::max)(0.0f, weights.cost);
        return benefit / (1.0f + cost);
    }
}
