#include <engine/collision/collision_surface_sampling.h>

#include <math/vec3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace wz::engine::collision
{
    namespace
    {
        bool normalize_checked(wz::math::Vec3& v) noexcept
        {
            const float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
            if (len_sq <= 1e-12f || !std::isfinite(len_sq)) {
                return false;
            }
            v = wz::math::normalize(v);
            return true;
        }

        bool inverse_affine_point(
            const wz::math::Mat4& m,
            const wz::math::Vec3& p,
            wz::math::Vec3& out) noexcept
        {
            const float a00 = m.m[0];
            const float a01 = m.m[4];
            const float a02 = m.m[8];
            const float a10 = m.m[1];
            const float a11 = m.m[5];
            const float a12 = m.m[9];
            const float a20 = m.m[2];
            const float a21 = m.m[6];
            const float a22 = m.m[10];

            const float det =
                a00 * (a11 * a22 - a12 * a21)
                - a01 * (a10 * a22 - a12 * a20)
                + a02 * (a10 * a21 - a11 * a20);
            if (std::abs(det) <= 1e-8f) {
                return false;
            }

            const float inv_det = 1.0f / det;
            const float x = p.x - m.m[12];
            const float y = p.y - m.m[13];
            const float z = p.z - m.m[14];

            out.x =
                ((a11 * a22 - a12 * a21) * x
                    + (a02 * a21 - a01 * a22) * y
                    + (a01 * a12 - a02 * a11) * z)
                * inv_det;
            out.y =
                ((a12 * a20 - a10 * a22) * x
                    + (a00 * a22 - a02 * a20) * y
                    + (a02 * a10 - a00 * a12) * z)
                * inv_det;
            out.z =
                ((a10 * a21 - a11 * a20) * x
                    + (a01 * a20 - a00 * a21) * y
                    + (a00 * a11 - a01 * a10) * z)
                * inv_det;
            return true;
        }

        bool ray_triangle_hit(
            const wz::math::Vec3& origin,
            const wz::math::Vec3& direction,
            const wz::math::Vec3& a,
            const wz::math::Vec3& b,
            const wz::math::Vec3& c,
            float max_distance,
            float& out_distance,
            wz::math::Vec3& out_position,
            wz::math::Vec3& out_normal) noexcept
        {
            const wz::math::Vec3 edge1 = b - a;
            const wz::math::Vec3 edge2 = c - a;
            const wz::math::Vec3 pvec = wz::math::cross(direction, edge2);
            const float det = wz::math::dot(edge1, pvec);
            if (std::abs(det) <= 1e-8f) {
                return false;
            }

            const float inv_det = 1.0f / det;
            const wz::math::Vec3 tvec = origin - a;
            const float u = wz::math::dot(tvec, pvec) * inv_det;
            if (u < -1e-5f || u > 1.0f + 1e-5f) {
                return false;
            }

            const wz::math::Vec3 qvec = wz::math::cross(tvec, edge1);
            const float v = wz::math::dot(direction, qvec) * inv_det;
            if (v < -1e-5f || u + v > 1.0f + 1e-5f) {
                return false;
            }

            const float t = wz::math::dot(edge2, qvec) * inv_det;
            if (t < 0.0f || t > max_distance) {
                return false;
            }

            wz::math::Vec3 normal = wz::math::cross(edge1, edge2);
            if (!normalize_checked(normal)) {
                return false;
            }
            if (wz::math::dot(normal, direction) >= 0.0f) {
                normal.x = -normal.x;
                normal.y = -normal.y;
                normal.z = -normal.z;
            }

            out_distance = t;
            out_position = origin + direction * t;
            out_normal = normal;
            return true;
        }

        uint32_t surface_grid_cell_index(
            float value,
            float origin,
            float size,
            uint32_t count) noexcept
        {
            const float normalized = (value - origin) / size;
            const int raw = static_cast<int>(std::floor(normalized));
            return static_cast<uint32_t>(
                (std::clamp)(raw, 0, static_cast<int>(count) - 1));
        }

        wz::math::Vec3 collision_point_position(
            const wz::engine::assets::CollisionPoint& point) noexcept
        {
            return wz::math::Vec3{
                .x = point.position[0],
                .y = point.position[1],
                .z = point.position[2],
            };
        }

        float height_sample_at(
            const wz::engine::assets::CollisionAssetData& data,
            uint32_t x,
            uint32_t z) noexcept
        {
            const size_t index =
                static_cast<size_t>(z) * data.resolution_x + x;
            return index < data.height_samples.size()
                ? data.base_height
                    + data.height_samples[index] * data.vertical_scale
                : data.base_height;
        }

        float bilinear_height_sample(
            const wz::engine::assets::CollisionAssetData& data,
            float sample_x,
            float sample_z) noexcept
        {
            const uint32_t x0 =
                static_cast<uint32_t>(std::floor(sample_x));
            const uint32_t z0 =
                static_cast<uint32_t>(std::floor(sample_z));
            const uint32_t x1 =
                (std::min)(x0 + 1u, data.resolution_x - 1u);
            const uint32_t z1 =
                (std::min)(z0 + 1u, data.resolution_y - 1u);
            const float tx = sample_x - static_cast<float>(x0);
            const float tz = sample_z - static_cast<float>(z0);

            const float h00 = height_sample_at(data, x0, z0);
            const float h10 = height_sample_at(data, x1, z0);
            const float h01 = height_sample_at(data, x0, z1);
            const float h11 = height_sample_at(data, x1, z1);
            const float h0 = h00 + (h10 - h00) * tx;
            const float h1 = h01 + (h11 - h01) * tx;
            return h0 + (h1 - h0) * tz;
        }

        bool sample_height_field_surface(
            const CollisionWorldEntry& entry,
            float world_x,
            float world_z,
            CollisionSurfaceSample& out_sample) noexcept
        {
            const auto& data = *entry.resolved;
            if (data.resolution_x == 0u
                || data.resolution_y == 0u
                || data.size[0] <= 0.0f
                || data.size[1] <= 0.0f
                || data.height_samples.size()
                    != static_cast<size_t>(data.resolution_x)
                        * data.resolution_y)
            {
                return false;
            }

            wz::math::Vec3 local_probe{};
            if (!inverse_affine_point(
                    entry.world_from_local,
                    wz::math::Vec3{
                        .x = world_x,
                        .y = entry.world_from_local.m[13],
                        .z = world_z,
                    },
                    local_probe))
            {
                return false;
            }

            const float u = (local_probe.x - data.origin[0]) / data.size[0];
            const float v = (local_probe.z - data.origin[1]) / data.size[1];
            constexpr float k_bounds_epsilon = 1e-5f;
            if (u < -k_bounds_epsilon
                || u > 1.0f + k_bounds_epsilon
                || v < -k_bounds_epsilon
                || v > 1.0f + k_bounds_epsilon)
            {
                return false;
            }

            const float clamped_u = (std::clamp)(u, 0.0f, 1.0f);
            const float clamped_v = (std::clamp)(v, 0.0f, 1.0f);
            const float sample_x =
                data.resolution_x > 1u
                    ? clamped_u * static_cast<float>(data.resolution_x - 1u)
                    : 0.0f;
            const float sample_z =
                data.resolution_y > 1u
                    ? clamped_v * static_cast<float>(data.resolution_y - 1u)
                    : 0.0f;
            const float local_y =
                bilinear_height_sample(data, sample_x, sample_z);
            const wz::math::Vec3 local_position{
                .x = local_probe.x,
                .y = local_y,
                .z = local_probe.z,
            };

            const uint32_t nearest_x =
                static_cast<uint32_t>(
                    (std::clamp)(
                        static_cast<int>(std::round(sample_x)),
                        0,
                        static_cast<int>(data.resolution_x) - 1));
            const uint32_t nearest_z =
                static_cast<uint32_t>(
                    (std::clamp)(
                        static_cast<int>(std::round(sample_z)),
                        0,
                        static_cast<int>(data.resolution_y) - 1));
            const uint32_t left_x = nearest_x == 0u ? 0u : nearest_x - 1u;
            const uint32_t right_x =
                (std::min)(nearest_x + 1u, data.resolution_x - 1u);
            const uint32_t near_z = nearest_z == 0u ? 0u : nearest_z - 1u;
            const uint32_t far_z =
                (std::min)(nearest_z + 1u, data.resolution_y - 1u);
            const float step_x =
                data.resolution_x > 1u
                    ? data.size[0]
                        / static_cast<float>(data.resolution_x - 1u)
                    : data.size[0];
            const float step_z =
                data.resolution_y > 1u
                    ? data.size[1]
                        / static_cast<float>(data.resolution_y - 1u)
                    : data.size[1];
            const float denom_x =
                (std::max)(
                    step_x * static_cast<float>(right_x - left_x),
                    1e-6f);
            const float denom_z =
                (std::max)(
                    step_z * static_cast<float>(far_z - near_z),
                    1e-6f);
            const float d_height_dx =
                (height_sample_at(data, right_x, nearest_z)
                    - height_sample_at(data, left_x, nearest_z))
                / denom_x;
            const float d_height_dz =
                (height_sample_at(data, nearest_x, far_z)
                    - height_sample_at(data, nearest_x, near_z))
                / denom_z;

            const wz::math::Vec3 world_position =
                wz::math::mul_point(entry.world_from_local, local_position);
            const wz::math::Vec3 world_tangent_x =
                wz::math::mul_point(
                    entry.world_from_local,
                    local_position
                        + wz::math::Vec3{
                            .x = 1.0f,
                            .y = d_height_dx,
                            .z = 0.0f,
                        })
                - world_position;
            const wz::math::Vec3 world_tangent_z =
                wz::math::mul_point(
                    entry.world_from_local,
                    local_position
                        + wz::math::Vec3{
                            .x = 0.0f,
                            .y = d_height_dz,
                            .z = 1.0f,
                        })
                - world_position;
            wz::math::Vec3 world_normal =
                wz::math::cross(world_tangent_z, world_tangent_x);
            if (!normalize_checked(world_normal)) {
                world_normal = wz::math::Vec3{
                    .x = 0.0f,
                    .y = 1.0f,
                    .z = 0.0f,
                };
            }
            if (world_normal.y < 0.0f) {
                world_normal.x = -world_normal.x;
                world_normal.y = -world_normal.y;
                world_normal.z = -world_normal.z;
            }

            out_sample = CollisionSurfaceSample{
                .hit = true,
                .surface_entity = entry.entity,
                .position = world_position,
                .normal = world_normal,
            };
            return true;
        }

        bool test_mesh_surface_triangle(
            const CollisionWorldEntry& entry,
            uint32_t tri,
            const wz::math::Vec3& local_origin,
            const wz::math::Vec3& local_direction,
            float local_max_distance,
            float& best_distance,
            wz::math::Vec3& best_position,
            wz::math::Vec3& best_normal) noexcept
        {
            const auto& data = *entry.resolved;
            const uint32_t triangle_count =
                static_cast<uint32_t>(data.indices.size() / 3u);
            if (tri >= triangle_count) {
                return false;
            }

            const size_t index = static_cast<size_t>(tri) * 3u;
            const uint32_t ia = data.indices[index + 0u];
            const uint32_t ib = data.indices[index + 1u];
            const uint32_t ic = data.indices[index + 2u];
            if (ia >= data.points.size()
                || ib >= data.points.size()
                || ic >= data.points.size())
            {
                return false;
            }

            const wz::math::Vec3 a = collision_point_position(data.points[ia]);
            const wz::math::Vec3 b = collision_point_position(data.points[ib]);
            const wz::math::Vec3 c = collision_point_position(data.points[ic]);
            float distance = 0.0f;
            wz::math::Vec3 position{};
            wz::math::Vec3 normal{};
            if (!ray_triangle_hit(
                    local_origin,
                    local_direction,
                    a,
                    b,
                    c,
                    local_max_distance,
                    distance,
                    position,
                    normal)
                || distance >= best_distance)
            {
                return false;
            }

            const wz::math::Vec3 world_position =
                wz::math::mul_point(entry.world_from_local, position);
            const wz::math::Vec3 world_a =
                wz::math::mul_point(entry.world_from_local, a);
            const wz::math::Vec3 world_b =
                wz::math::mul_point(entry.world_from_local, b);
            const wz::math::Vec3 world_c =
                wz::math::mul_point(entry.world_from_local, c);
            wz::math::Vec3 world_normal =
                wz::math::cross(world_b - world_a, world_c - world_a);
            if (!normalize_checked(world_normal)) {
                return false;
            }
            if (world_normal.y < 0.0f) {
                world_normal.x = -world_normal.x;
                world_normal.y = -world_normal.y;
                world_normal.z = -world_normal.z;
            }

            best_distance = distance;
            best_position = world_position;
            best_normal = world_normal;
            return true;
        }

        bool sample_mesh_surface(
            const CollisionWorldEntry& entry,
            float world_x,
            float world_z,
            CollisionSurfaceSample& out_sample) noexcept
        {
            const auto& data = *entry.resolved;
            if (data.points.empty() || data.indices.size() < 3u) {
                return false;
            }

            wz::math::Vec3 local_probe{};
            if (!inverse_affine_point(
                    entry.world_from_local,
                    wz::math::Vec3{
                        .x = world_x,
                        .y = entry.world_from_local.m[13],
                        .z = world_z,
                    },
                    local_probe))
            {
                return false;
            }

            constexpr float k_margin = 1.0f;
            const float height_span =
                (std::max)(
                    data.bounds_max[1] - data.bounds_min[1],
                    0.0f);
            const wz::math::Vec3 local_origin{
                .x = local_probe.x,
                .y = data.bounds_max[1] + k_margin,
                .z = local_probe.z,
            };
            const wz::math::Vec3 local_direction{
                .x = 0.0f,
                .y = -1.0f,
                .z = 0.0f,
            };
            const float local_max_distance = height_span + k_margin * 2.0f;

            float best_distance = std::numeric_limits<float>::max();
            wz::math::Vec3 best_position{};
            wz::math::Vec3 best_normal{ .x = 0.0f, .y = 1.0f, .z = 0.0f };
            bool hit = false;

            const auto& grid = data.surface_grid;
            const size_t cell_count =
                static_cast<size_t>(grid.cells_x) * grid.cells_z;
            const bool grid_available =
                grid.cells_x != 0u
                && grid.cells_z != 0u
                && grid.cell_offsets.size() == cell_count + 1u
                && grid.cell_bounds.size() == cell_count;
            if (grid_available) {
                const float grid_max_x =
                    grid.origin_x + grid.cell_size_x * grid.cells_x;
                const float grid_max_z =
                    grid.origin_z + grid.cell_size_z * grid.cells_z;
                if (local_probe.x < grid.origin_x
                    || local_probe.z < grid.origin_z
                    || local_probe.x > grid_max_x
                    || local_probe.z > grid_max_z)
                {
                    return false;
                }

                const uint32_t cell_x = surface_grid_cell_index(
                    local_probe.x,
                    grid.origin_x,
                    grid.cell_size_x,
                    grid.cells_x);
                const uint32_t cell_z = surface_grid_cell_index(
                    local_probe.z,
                    grid.origin_z,
                    grid.cell_size_z,
                    grid.cells_z);
                const size_t cell =
                    static_cast<size_t>(cell_z) * grid.cells_x + cell_x;
                const uint32_t begin = grid.cell_offsets[cell];
                const uint32_t end = grid.cell_offsets[cell + 1u];
                for (uint32_t i = begin; i < end; ++i) {
                    if (i >= grid.cell_triangle_indices.size()) {
                        continue;
                    }
                    hit = test_mesh_surface_triangle(
                            entry,
                            grid.cell_triangle_indices[i],
                            local_origin,
                            local_direction,
                            local_max_distance,
                            best_distance,
                            best_position,
                            best_normal)
                        || hit;
                }
            }
            else {
                const uint32_t triangle_count =
                    static_cast<uint32_t>(data.indices.size() / 3u);
                for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                    hit = test_mesh_surface_triangle(
                            entry,
                            tri,
                            local_origin,
                            local_direction,
                            local_max_distance,
                            best_distance,
                            best_position,
                            best_normal)
                        || hit;
                }
            }

            if (!hit) {
                return false;
            }

            out_sample = CollisionSurfaceSample{
                .hit = true,
                .surface_entity = entry.entity,
                .position = best_position,
                .normal = best_normal,
            };
            return true;
        }
    }

    bool sample_terrain_surface(
        const CollisionWorldEntry& entry,
        float world_x,
        float world_z,
        CollisionSurfaceSample& out_sample) noexcept
    {
        out_sample = CollisionSurfaceSample{};
        if (!entry.enabled
            || !entry.resolved
            || !std::isfinite(world_x)
            || !std::isfinite(world_z))
        {
            return false;
        }

        switch (entry.resolved->shape_kind) {
        case wz::engine::assets::CollisionShapeKind::TerrainHeightField:
            return sample_height_field_surface(
                entry,
                world_x,
                world_z,
                out_sample);

        case wz::engine::assets::CollisionShapeKind::TerrainMeshSurface:
            return sample_mesh_surface(
                entry,
                world_x,
                world_z,
                out_sample);

        default:
            return false;
        }
    }
}
