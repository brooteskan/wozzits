#include <engine/collision/collision_surface_sampling.h>

#include <math/vec3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

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

        bool barycentric_xz(
            const wz::math::Vec3& p,
            const wz::math::Vec3& a,
            const wz::math::Vec3& b,
            const wz::math::Vec3& c,
            float& wa,
            float& wb,
            float& wc) noexcept
        {
            const float v0x = b.x - a.x;
            const float v0z = b.z - a.z;
            const float v1x = c.x - a.x;
            const float v1z = c.z - a.z;
            const float v2x = p.x - a.x;
            const float v2z = p.z - a.z;
            const float denom = v0x * v1z - v1x * v0z;
            if (std::abs(denom) <= 1e-10f || !std::isfinite(denom)) {
                return false;
            }

            wb = (v2x * v1z - v1x * v2z) / denom;
            wc = (v0x * v2z - v2x * v0z) / denom;
            wa = 1.0f - wb - wc;
            return std::isfinite(wa)
                && std::isfinite(wb)
                && std::isfinite(wc);
        }

        float closest_segment_xz(
            const wz::math::Vec3& p,
            const wz::math::Vec3& a,
            const wz::math::Vec3& b) noexcept
        {
            const float abx = b.x - a.x;
            const float abz = b.z - a.z;
            const float denom = abx * abx + abz * abz;
            if (denom <= 1e-12f || !std::isfinite(denom)) {
                return 0.0f;
            }
            const float apx = p.x - a.x;
            const float apz = p.z - a.z;
            return (std::clamp)((apx * abx + apz * abz) / denom, 0.0f, 1.0f);
        }

        void closest_triangle_point_xz(
            const wz::math::Vec3& p,
            const wz::math::Vec3& a,
            const wz::math::Vec3& b,
            const wz::math::Vec3& c,
            float& out_wa,
            float& out_wb,
            float& out_wc,
            float& out_distance_sq) noexcept
        {
            constexpr float k_inside_epsilon = 1e-5f;
            float wa = 0.0f;
            float wb = 0.0f;
            float wc = 0.0f;
            if (barycentric_xz(p, a, b, c, wa, wb, wc)
                && wa >= -k_inside_epsilon
                && wb >= -k_inside_epsilon
                && wc >= -k_inside_epsilon)
            {
                out_wa = (std::clamp)(wa, 0.0f, 1.0f);
                out_wb = (std::clamp)(wb, 0.0f, 1.0f);
                out_wc = (std::clamp)(wc, 0.0f, 1.0f);
                const float sum = out_wa + out_wb + out_wc;
                if (sum > 0.0f) {
                    out_wa /= sum;
                    out_wb /= sum;
                    out_wc /= sum;
                }
                out_distance_sq = 0.0f;
                return;
            }

            auto choose = [&](
                float ca,
                float cb,
                float cc)
            {
                const float x = a.x * ca + b.x * cb + c.x * cc;
                const float z = a.z * ca + b.z * cb + c.z * cc;
                const float dx = p.x - x;
                const float dz = p.z - z;
                const float distance_sq = dx * dx + dz * dz;
                if (distance_sq < out_distance_sq) {
                    out_wa = ca;
                    out_wb = cb;
                    out_wc = cc;
                    out_distance_sq = distance_sq;
                }
            };

            out_wa = 1.0f;
            out_wb = 0.0f;
            out_wc = 0.0f;
            const float dax = p.x - a.x;
            const float daz = p.z - a.z;
            out_distance_sq = dax * dax + daz * daz;

            const float ab = closest_segment_xz(p, a, b);
            choose(1.0f - ab, ab, 0.0f);
            const float bc = closest_segment_xz(p, b, c);
            choose(0.0f, 1.0f - bc, bc);
            const float ca = closest_segment_xz(p, c, a);
            choose(ca, 0.0f, 1.0f - ca);
        }

        bool triangle_plane_height_at_xz(
            const wz::math::Vec3& a,
            const wz::math::Vec3& b,
            const wz::math::Vec3& c,
            float x,
            float z,
            float& out_y) noexcept
        {
            const wz::math::Vec3 normal =
                wz::math::cross(b - a, c - a);
            if (std::abs(normal.y) <= 1e-6f || !std::isfinite(normal.y)) {
                return false;
            }

            const float d =
                -(normal.x * a.x + normal.y * a.y + normal.z * a.z);
            out_y = -(normal.x * x + normal.z * z + d) / normal.y;
            return std::isfinite(out_y);
        }

        struct MeshSurfaceBlend
        {
            float weight_sum = 0.0f;
            float local_y_sum = 0.0f;
            wz::math::Vec3 world_normal_sum{};
            uint32_t samples = 0u;

            void add(
                float local_y,
                const wz::math::Vec3& world_normal,
                float weight) noexcept
            {
                if (weight <= 0.0f || !std::isfinite(weight)) {
                    return;
                }
                weight_sum += weight;
                local_y_sum += local_y * weight;
                world_normal_sum.x += world_normal.x * weight;
                world_normal_sum.y += world_normal.y * weight;
                world_normal_sum.z += world_normal.z * weight;
                ++samples;
            }
        };

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

        bool test_mesh_surface_grid_cell(
            const CollisionWorldEntry& entry,
            const wz::engine::assets::CollisionSurfaceGrid& grid,
            size_t cell,
            const wz::math::Vec3& local_origin,
            const wz::math::Vec3& local_direction,
            float local_max_distance,
            float& best_distance,
            wz::math::Vec3& best_position,
            wz::math::Vec3& best_normal) noexcept
        {
            const uint32_t begin = grid.cell_offsets[cell];
            const uint32_t end = grid.cell_offsets[cell + 1u];
            bool hit = false;
            for (uint32_t i = begin; i < end; ++i) {
                if (i >= grid.cell_triangle_indices.size()) {
                    continue;
                }
                const uint32_t tri = grid.cell_triangle_indices[i];
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
            return hit;
        }

        bool append_unique_triangle(
            std::vector<uint32_t>& triangles,
            uint32_t triangle)
        {
            if (std::find(triangles.begin(), triangles.end(), triangle)
                != triangles.end())
            {
                return false;
            }
            triangles.push_back(triangle);
            return true;
        }

        bool accumulate_mesh_surface_triangle_sample(
            const CollisionWorldEntry& entry,
            uint32_t tri,
            const wz::math::Vec3& local_probe,
            float max_distance_sq,
            MeshSurfaceBlend& blend) noexcept
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
            float wa = 0.0f;
            float wb = 0.0f;
            float wc = 0.0f;
            float distance_sq = std::numeric_limits<float>::max();
            closest_triangle_point_xz(
                local_probe,
                a,
                b,
                c,
                wa,
                wb,
                wc,
                distance_sq);
            if (distance_sq > max_distance_sq) {
                return false;
            }

            float local_y = 0.0f;
            if (!triangle_plane_height_at_xz(
                    a,
                    b,
                    c,
                    local_probe.x,
                    local_probe.z,
                    local_y))
            {
                return false;
            }

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

            const float epsilon =
                (std::max)(max_distance_sq * 1e-4f, 1e-8f);
            blend.add(local_y, world_normal, 1.0f / (distance_sq + epsilon));
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
                hit = test_mesh_surface_grid_cell(
                    entry,
                    grid,
                    cell,
                    local_origin,
                    local_direction,
                    local_max_distance,
                    best_distance,
                    best_position,
                    best_normal);

                if (!hit) {
                    const uint32_t min_x = cell_x == 0u ? 0u : cell_x - 1u;
                    const uint32_t max_x =
                        (std::min)(cell_x + 1u, grid.cells_x - 1u);
                    const uint32_t min_z = cell_z == 0u ? 0u : cell_z - 1u;
                    const uint32_t max_z =
                        (std::min)(cell_z + 1u, grid.cells_z - 1u);
                    for (uint32_t z = min_z; z <= max_z; ++z) {
                        for (uint32_t x = min_x; x <= max_x; ++x) {
                            const size_t neighbor =
                                static_cast<size_t>(z) * grid.cells_x + x;
                            if (neighbor == cell) {
                                continue;
                            }
                            hit = test_mesh_surface_grid_cell(
                                entry,
                                grid,
                                neighbor,
                                local_origin,
                                local_direction,
                                local_max_distance,
                                best_distance,
                                best_position,
                                best_normal)
                                || hit;
                        }
                    }
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

        bool sample_nearest_mesh_surface(
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

            MeshSurfaceBlend blend{};

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
                constexpr uint32_t k_search_ring = 3u;
                const float max_distance =
                    (std::max)(grid.cell_size_x, grid.cell_size_z)
                    * static_cast<float>(k_search_ring + 1u);
                const float max_distance_sq = max_distance * max_distance;
                const uint32_t min_x =
                    cell_x > k_search_ring ? cell_x - k_search_ring : 0u;
                const uint32_t max_x =
                    (std::min)(cell_x + k_search_ring, grid.cells_x - 1u);
                const uint32_t min_z =
                    cell_z > k_search_ring ? cell_z - k_search_ring : 0u;
                const uint32_t max_z =
                    (std::min)(cell_z + k_search_ring, grid.cells_z - 1u);
                std::vector<uint32_t> tested_triangles;
                for (uint32_t z = min_z; z <= max_z; ++z) {
                    for (uint32_t x = min_x; x <= max_x; ++x) {
                        const size_t cell =
                            static_cast<size_t>(z) * grid.cells_x + x;
                        const uint32_t begin = grid.cell_offsets[cell];
                        const uint32_t end = grid.cell_offsets[cell + 1u];
                        for (uint32_t i = begin; i < end; ++i) {
                            if (i >= grid.cell_triangle_indices.size()) {
                                continue;
                            }
                            const uint32_t tri = grid.cell_triangle_indices[i];
                            if (!append_unique_triangle(
                                    tested_triangles,
                                    tri))
                            {
                                continue;
                            }
                            accumulate_mesh_surface_triangle_sample(
                                entry,
                                tri,
                                local_probe,
                                max_distance_sq,
                                blend);
                        }
                    }
                }
            }
            else {
                const uint32_t triangle_count =
                    static_cast<uint32_t>(data.indices.size() / 3u);
                const float span_x =
                    (std::max)(data.bounds_max[0] - data.bounds_min[0], 0.0f);
                const float span_z =
                    (std::max)(data.bounds_max[2] - data.bounds_min[2], 0.0f);
                const float max_distance =
                    (std::max)(span_x, span_z) * 0.02f;
                const float max_distance_sq = max_distance * max_distance;
                for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                    accumulate_mesh_surface_triangle_sample(
                        entry,
                        tri,
                        local_probe,
                        max_distance_sq,
                        blend);
                }
            }

            if (blend.samples == 0u || blend.weight_sum <= 0.0f) {
                return false;
            }

            const float local_y = blend.local_y_sum / blend.weight_sum;
            const wz::math::Vec3 local_position{
                .x = local_probe.x,
                .y = local_y,
                .z = local_probe.z,
            };
            wz::math::Vec3 world_normal = blend.world_normal_sum;
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
                .position = wz::math::mul_point(
                    entry.world_from_local,
                    local_position),
                .normal = world_normal,
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

    bool sample_nearest_terrain_surface(
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
            return sample_nearest_mesh_surface(
                entry,
                world_x,
                world_z,
                out_sample);

        default:
            return false;
        }
    }
}
