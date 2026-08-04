#include <engine/collision/collision_surface_sampling.h>

#include <engine/rendering/clipmap_drawn_surface.h>
#include <math/vec3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace wz::engine::collision
{
    namespace
    {
        // height_field_grid_span lives in engine/assets/collision/collision.h,
        // next to the data it describes, so the compiler that resamples a field
        // onto this grid and this sampler that reads it back cannot disagree.
        using wz::engine::assets::height_field_grid_span;

        float height_field_pitch(
            const wz::engine::assets::CollisionAssetData& data,
            float size,
            uint32_t resolution) noexcept
        {
            return size
                / height_field_grid_span(data.placement_driven, resolution);
        }

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
            // A NaN det slips a bare `abs(det) <= eps` gate (any NaN comparison
            // is false), leaving inv_det = 1/NaN = NaN to propagate through the
            // outputs. The neighbouring degenerate-guards in this file all carry
            // the `|| !std::isfinite` companion; these two Moller-Trumbore/inverse
            // det gates were missed (C1(v2)-H20, #314). Sealed by the public
            // sampling boundary today, so this is defence-in-depth.
            if (std::abs(det) <= 1e-8f || !std::isfinite(det)) {
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
            // A NaN det slips a bare `abs(det) <= eps` gate (any NaN comparison
            // is false), leaving inv_det = 1/NaN = NaN to propagate through the
            // outputs. The neighbouring degenerate-guards in this file all carry
            // the `|| !std::isfinite` companion; these two Moller-Trumbore/inverse
            // det gates were missed (C1(v2)-H20, #314). Sealed by the public
            // sampling boundary today, so this is defence-in-depth.
            if (std::abs(det) <= 1e-8f || !std::isfinite(det)) {
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

        struct HeightFieldEvaluation
        {
            float height = 0.0f;
            float d_height_dx = 0.0f;
            float d_height_dz = 0.0f;
        };

        float cubic_interp(
            float p0,
            float p1,
            float p2,
            float p3,
            float t) noexcept
        {
            return 0.5f
                * ((2.0f * p1)
                    + (-p0 + p2) * t
                    + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3)
                        * t * t
                    + (-p0 + 3.0f * p1 - 3.0f * p2 + p3)
                        * t * t * t);
        }

        float cubic_derivative(
            float p0,
            float p1,
            float p2,
            float p3,
            float t) noexcept
        {
            return 0.5f
                * ((-p0 + p2)
                    + 2.0f
                        * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3)
                        * t
                    + 3.0f
                        * (-p0 + 3.0f * p1 - 3.0f * p2 + p3)
                        * t * t);
        }

        uint32_t clamped_height_index(int value, uint32_t count) noexcept
        {
            return static_cast<uint32_t>(
                (std::clamp)(value, 0, static_cast<int>(count) - 1));
        }

        HeightFieldEvaluation smooth_height_field_evaluation(
            const wz::engine::assets::CollisionAssetData& data,
            float sample_x,
            float sample_z,
            float step_x,
            float step_z) noexcept
        {
            if (data.resolution_x < 2u || data.resolution_y < 2u) {
                return HeightFieldEvaluation{
                    .height = bilinear_height_sample(data, sample_x, sample_z),
                };
            }

            int x1 = static_cast<int>(std::floor(sample_x));
            int z1 = static_cast<int>(std::floor(sample_z));
            if (x1 >= static_cast<int>(data.resolution_x) - 1) {
                x1 = static_cast<int>(data.resolution_x) - 2;
            }
            if (z1 >= static_cast<int>(data.resolution_y) - 1) {
                z1 = static_cast<int>(data.resolution_y) - 2;
            }
            const float tx = sample_x - static_cast<float>(x1);
            const float tz = sample_z - static_cast<float>(z1);

            float row_values[4]{};
            float row_dx[4]{};
            for (int row = 0; row < 4; ++row) {
                const uint32_t z =
                    clamped_height_index(z1 + row - 1, data.resolution_y);
                const float p0 = height_sample_at(
                    data,
                    clamped_height_index(x1 - 1, data.resolution_x),
                    z);
                const float p1 = height_sample_at(
                    data,
                    clamped_height_index(x1, data.resolution_x),
                    z);
                const float p2 = height_sample_at(
                    data,
                    clamped_height_index(x1 + 1, data.resolution_x),
                    z);
                const float p3 = height_sample_at(
                    data,
                    clamped_height_index(x1 + 2, data.resolution_x),
                    z);
                row_values[row] = cubic_interp(p0, p1, p2, p3, tx);
                row_dx[row] =
                    cubic_derivative(p0, p1, p2, p3, tx)
                    / (std::max)(step_x, 1e-6f);
            }

            return HeightFieldEvaluation{
                .height = cubic_interp(
                    row_values[0],
                    row_values[1],
                    row_values[2],
                    row_values[3],
                    tz),
                .d_height_dx = cubic_interp(
                    row_dx[0],
                    row_dx[1],
                    row_dx[2],
                    row_dx[3],
                    tz),
                .d_height_dz =
                    cubic_derivative(
                        row_values[0],
                        row_values[1],
                        row_values[2],
                        row_values[3],
                        tz)
                    / (std::max)(step_z, 1e-6f),
            };
        }

        // clamp_to_bounds: when the (x,z) probe falls OUTSIDE the heightfield,
        // false rejects it (no surface there); true clamps the probe to the
        // nearest edge and returns that edge's height -- the genuine "nearest
        // terrain surface" for a heightfield. The constraint uses the height to
        // keep an actor that drove off the terrain riding the boundary instead of
        // falling through (the precise sampler stays exact; only the nearest-
        // surface fallback clamps).
        // Reconstruct the DRAWN clipmap surface at a LOCAL XZ, when this asset
        // opted in and carries everything the reconstruction needs. Returns
        // false when it does not, leaving the caller on the true surface.
        //
        // Requires placement_driven: the schedule's cell size is in world
        // metres, and only a placement-driven collision measures its own
        // origin/size in those same units. A collision scaled by its scene
        // node would silently reconstruct rings of the wrong size -- and it
        // could not be sharing the clipmap's Placement anyway, so it is not a
        // configuration the drawn surface is defined for.
        bool drawn_surface_local_height(
            const wz::engine::assets::CollisionAssetData& data,
            const CollisionWorldEntry& entry,
            const wz::math::Vec3& observer_world,
            float local_x,
            float local_z,
            float& out_height) noexcept
        {
            if (!data.constrain_to_drawn_surface
                || !data.placement_driven
                || data.render_lod_level_count < 1u
                || data.render_lod_base_resolution < 2u
                || !(data.render_lod_cell_size > 0.0f)
                || data.height_mips.empty()
                || data.height_samples.empty())
            {
                return false;
            }

            // Level 0 IS height_samples; the stored chain starts at level 1.
            // Fixed capacity, no allocation: this runs per actor per frame,
            // times the footprint ring. 24 levels covers a 16M-texel field.
            std::array<wz::engine::rendering::ClipmapHeightMipView, 24> levels{};
            size_t used = 0;
            levels[used++] = wz::engine::rendering::ClipmapHeightMipView{
                data.height_samples.data(),
                data.resolution_x,
                data.resolution_y,
            };
            for (const auto& mip : data.height_mips) {
                if (used >= levels.size()) {
                    break;
                }
                levels[used++] = wz::engine::rendering::ClipmapHeightMipView{
                    mip.values.data(), mip.width, mip.height };
            }

            wz::engine::rendering::ClipmapHeightFieldView field{};
            field.levels = std::span<const
                wz::engine::rendering::ClipmapHeightMipView>(
                    levels.data(), used);
            field.world_origin[0] = data.origin[0];
            field.world_origin[1] = data.origin[1];
            field.world_size[0] = data.size[0];
            field.world_size[1] = data.size[1];

            wz::math::Vec3 local_observer{};
            if (!inverse_affine_point(
                    entry.world_from_local, observer_world, local_observer))
            {
                return false;
            }

            wz::engine::rendering::ClipmapDrawnSurfaceParams params{};
            params.observer_xz[0] = local_observer.x;
            params.observer_xz[1] = local_observer.z;
            params.c0 = data.render_lod_cell_size;
            params.base_resolution = data.render_lod_base_resolution;
            params.level_count = data.render_lod_level_count;
            if (!field.valid() || !params.valid()) {
                return false;
            }

            out_height = wz::engine::rendering::clipmap_drawn_surface_height(
                field, params, local_x, local_z);
            return true;
        }

        bool sample_height_field_surface(
            const CollisionWorldEntry& entry,
            float world_x,
            float world_z,
            CollisionSurfaceSample& out_sample,
            const wz::math::Vec3& observer,
            bool clamp_to_bounds = false) noexcept
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
            const bool out_of_bounds =
                u < -k_bounds_epsilon
                || u > 1.0f + k_bounds_epsilon
                || v < -k_bounds_epsilon
                || v > 1.0f + k_bounds_epsilon;
            if (out_of_bounds && !clamp_to_bounds) {
                return false;  // exact sample: no terrain at this (x,z)
            }
            // When clamping, the height/normal below come from the clamped (edge)
            // u,v, so an off-terrain probe resolves to the boundary surface.

            const float clamped_u = (std::clamp)(u, 0.0f, 1.0f);
            const float clamped_v = (std::clamp)(v, 0.0f, 1.0f);
            const float span_x = height_field_grid_span(
                data.placement_driven, data.resolution_x);
            const float span_z = height_field_grid_span(
                data.placement_driven, data.resolution_y);
            const float sample_x = clamped_u * span_x;
            const float sample_z = clamped_v * span_z;
            const float step_x = data.size[0] / span_x;
            const float step_z = data.size[1] / span_z;
            const HeightFieldEvaluation eval =
                smooth_height_field_evaluation(
                    data,
                    sample_x,
                    sample_z,
                    step_x,
                    step_z);
            // HEIGHT may come from the drawn surface; the NORMAL never does.
            //
            // Splitting them is deliberate. The drawn surface is piecewise
            // bilinear over cells as wide as a coarse ring -- 15 m on the live
            // landscape -- so its gradient is piecewise constant, and an actor
            // aligning to it would snap between facets as it crossed cell
            // boundaries. The bicubic's gradient is smooth and is what the
            // orientation blend was tuned against, so eval.d_height_d* stay in
            // charge of the normal below. The pipeline already separates the
            // two: height comes from the centre sample, orientation from the
            // averaged footprint ring.
            float surface_height = eval.height;
            float drawn_height = 0.0f;
            if (drawn_surface_local_height(
                    data,
                    entry,
                    observer,
                    local_probe.x,
                    local_probe.z,
                    drawn_height))
            {
                surface_height = drawn_height;
            }

            const wz::math::Vec3 local_position{
                .x = local_probe.x,
                .y = surface_height,
                .z = local_probe.z,
            };

            const wz::math::Vec3 world_position =
                wz::math::mul_point(entry.world_from_local, local_position);
            const wz::math::Vec3 world_tangent_x =
                wz::math::mul_point(
                    entry.world_from_local,
                    local_position
                        + wz::math::Vec3{
                            .x = 1.0f,
                            .y = eval.d_height_dx,
                            .z = 0.0f,
                        })
                - world_position;
            const wz::math::Vec3 world_tangent_z =
                wz::math::mul_point(
                    entry.world_from_local,
                    local_position
                        + wz::math::Vec3{
                            .x = 0.0f,
                            .y = eval.d_height_dz,
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

        // The true full-res surface height at a local XZ, reconstructed by
        // BILINEAR filtering (matching the clipmap render shader's level-0 ring,
        // NOT the bicubic vertical sampler). u,v are clamped to the footprint;
        // callers clip the ray to the footprint first, so this only sees in-range
        // probes.
        float height_field_ray_surface_local_y(
            const wz::engine::assets::CollisionAssetData& data,
            float local_x,
            float local_z) noexcept
        {
            const float u =
                (std::clamp)((local_x - data.origin[0]) / data.size[0],
                    0.0f, 1.0f);
            const float v =
                (std::clamp)((local_z - data.origin[1]) / data.size[1],
                    0.0f, 1.0f);
            const float sample_x = u * height_field_grid_span(
                data.placement_driven, data.resolution_x);
            const float sample_z = v * height_field_grid_span(
                data.placement_driven, data.resolution_y);
            return bilinear_height_sample(data, sample_x, sample_z);
        }

        // Render-LOD reconstruction context. When a heightfield is DRAWN as a
        // geometry-clipmap, its coarse rings triangulate the field at 2^L-cell
        // spacing -- straight chords that BRIDGE any dip finer than a coarse cell
        // -- so the drawn surface floats ABOVE the true field over sub-cell
        // relief. A ray marching the true full-res field slips under that drawn
        // chord (occluded) but over the true ground (no hit), so a grazing shot
        // passes through and reappears. This mirrors the clipmap ring schedule so
        // the ray can strike the DRAWN surface instead. Keyed off the ray origin
        // (the shooter) rather than the render camera: for a shot the player
        // scrutinises the camera rides the shooter, so the two coincide.
        struct HeightFieldRayLod
        {
            bool enabled = false;
            float center_x = 0.0f;    // ring center (shooter local XZ)
            float center_z = 0.0f;
            float origin_x = 0.0f;    // footprint origin (coarse-grid anchor)
            float origin_z = 0.0f;
            float step_x = 1.0f;      // finest cell = sample spacing size/(res-1)
            float step_z = 1.0f;
            float inner_half = 1.0f;  // level-0 square half-extent (0.5 * m * c0)
            float max_level = 0.0f;   // level_count - 1
        };

        HeightFieldRayLod make_height_field_ray_lod(
            const wz::engine::assets::CollisionAssetData& data,
            float center_x,
            float center_z) noexcept
        {
            HeightFieldRayLod lod{};
            if (data.render_lod_level_count < 1u
                || data.render_lod_base_resolution < 2u
                || data.resolution_x < 2u
                || data.resolution_y < 2u)
            {
                return lod;
            }
            // The finest lattice cell is the sample spacing, so a level-0 ring
            // reconstructs the true bilinear surface exactly (its coarse grid IS
            // the sample grid) and level L subsamples every 2^L-th sample -- the
            // clipmap's coarse triangulation.
            lod.enabled = true;
            lod.center_x = center_x;
            lod.center_z = center_z;
            lod.origin_x = data.origin[0];
            lod.origin_z = data.origin[1];
            lod.step_x =
                height_field_pitch(data, data.size[0], data.resolution_x);
            lod.step_z =
                height_field_pitch(data, data.size[1], data.resolution_y);
            lod.inner_half = 0.5f
                * static_cast<float>(data.render_lod_base_resolution)
                * lod.step_x;
            lod.max_level =
                static_cast<float>(data.render_lod_level_count - 1u);
            return lod;
        }

        // The DRAWN surface height at a local XZ. With LOD off this is the true
        // bilinear surface. With LOD on, pick the clipmap ring for this point (the
        // finest ring whose square half-extent inner_half*2^L covers the Chebyshev
        // distance from the ring center) and reconstruct that ring's coarse
        // triangulation: sample the true field at the four surrounding coarse-grid
        // vertices (spacing 2^L sample steps, anchored on the footprint origin so
        // corners land on samples) and bilinear-blend, so the chords bridge
        // sub-cell dips exactly as the drawn ring does. Mip box-filter, per-level
        // snap and geomorph are intentionally omitted -- the coarse-chord bridging
        // is the term that stops the pass-through.
        float height_field_drawn_surface_local_y(
            const wz::engine::assets::CollisionAssetData& data,
            const HeightFieldRayLod& lod,
            float local_x,
            float local_z) noexcept
        {
            if (!lod.enabled) {
                return height_field_ray_surface_local_y(data, local_x, local_z);
            }
            const float dist = (std::max)(
                std::abs(local_x - lod.center_x),
                std::abs(local_z - lod.center_z));
            float level = 0.0f;
            if (dist > lod.inner_half && lod.inner_half > 1e-6f) {
                level = std::ceil(std::log2(dist / lod.inner_half));
            }
            level = (std::clamp)(level, 0.0f, lod.max_level);
            const float scale = std::exp2(level);
            const float clx = lod.step_x * scale;
            const float clz = lod.step_z * scale;
            if (!(clx > 1e-6f) || !(clz > 1e-6f)) {
                return height_field_ray_surface_local_y(data, local_x, local_z);
            }

            const float gx = (local_x - lod.origin_x) / clx;
            const float gz = (local_z - lod.origin_z) / clz;
            const float fx = std::floor(gx);
            const float fz = std::floor(gz);
            const float frx = gx - fx;
            const float frz = gz - fz;
            const float x0 = lod.origin_x + fx * clx;
            const float x1 = lod.origin_x + (fx + 1.0f) * clx;
            const float z0 = lod.origin_z + fz * clz;
            const float z1 = lod.origin_z + (fz + 1.0f) * clz;
            const float h00 = height_field_ray_surface_local_y(data, x0, z0);
            const float h10 = height_field_ray_surface_local_y(data, x1, z0);
            const float h01 = height_field_ray_surface_local_y(data, x0, z1);
            const float h11 = height_field_ray_surface_local_y(data, x1, z1);
            const float h0 = h00 + (h10 - h00) * frx;
            const float h1 = h01 + (h11 - h01) * frx;
            return h0 + (h1 - h0) * frz;
        }

        // Fill out_sample for a heightfield ray hit whose LOCAL XZ is
        // (local_x, local_z): snap Y onto the DRAWN surface (true field with LOD
        // off) and build the world normal from central differences of that same
        // surface, one texel to each side.
        bool emit_height_field_ray_hit(
            const CollisionWorldEntry& entry,
            const wz::engine::assets::CollisionAssetData& data,
            const HeightFieldRayLod& lod,
            float local_x,
            float local_z,
            CollisionSurfaceSample& out_sample) noexcept
        {
            const float step_x =
                height_field_pitch(data, data.size[0], data.resolution_x);
            const float step_z =
                height_field_pitch(data, data.size[1], data.resolution_y);

            const wz::math::Vec3 local_position{
                .x = local_x,
                .y = height_field_drawn_surface_local_y(
                    data, lod, local_x, local_z),
                .z = local_z,
            };

            const float d_height_dx =
                (height_field_drawn_surface_local_y(
                     data, lod, local_x + step_x, local_z)
                 - height_field_drawn_surface_local_y(
                     data, lod, local_x - step_x, local_z))
                / (2.0f * step_x);
            const float d_height_dz =
                (height_field_drawn_surface_local_y(
                     data, lod, local_x, local_z + step_z)
                 - height_field_drawn_surface_local_y(
                     data, lod, local_x, local_z - step_z))
                / (2.0f * step_z);

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

        // March a ray against a TerrainHeightField collider and report the
        // nearest surface crossing. Works entirely in the collider's LOCAL frame
        // -- like sample_height_field_surface -- so it hits the SAME surface the
        // rest of the collision system reports. ray_dir_unit must be normalized.
        bool raycast_height_field_surface(
            const CollisionWorldEntry& entry,
            const wz::math::Vec3& ray_origin,
            const wz::math::Vec3& ray_dir_unit,
            float max_distance,
            CollisionSurfaceSample& out_sample) noexcept
        {
            const auto& data = *entry.resolved;
            if (data.resolution_x < 2u
                || data.resolution_y < 2u
                || data.size[0] <= 0.0f
                || data.size[1] <= 0.0f
                || data.height_samples.size()
                    != static_cast<size_t>(data.resolution_x)
                        * data.resolution_y)
            {
                return false;
            }

            // Transform the world ray's endpoints into local space and rebuild
            // the local ray from them; this avoids needing the inverse of the
            // linear part explicitly (same trick as the mesh grid ray path).
            const wz::math::Vec3 world_end =
                ray_origin + ray_dir_unit * max_distance;
            wz::math::Vec3 local_origin{};
            wz::math::Vec3 local_end{};
            if (!inverse_affine_point(
                    entry.world_from_local, ray_origin, local_origin)
                || !inverse_affine_point(
                    entry.world_from_local, world_end, local_end))
            {
                return false;
            }

            const wz::math::Vec3 local_delta = local_end - local_origin;
            const float local_len = wz::math::length(local_delta);
            if (local_len <= 1e-6f || !std::isfinite(local_len)) {
                return false;
            }
            const wz::math::Vec3 local_dir = local_delta / local_len;

            // Clip the local ray to the field's XZ footprint: only that span can
            // produce a real crossing (outside it the sampler just clamps).
            float t0 = 0.0f;
            float t1 = local_len;
            const auto clip_axis =
                [&](float o, float d, float lo, float hi) -> bool
            {
                if (std::abs(d) < 1e-9f) {
                    return o >= lo && o <= hi;  // parallel: inside slab or reject
                }
                float ta = (lo - o) / d;
                float tb = (hi - o) / d;
                if (ta > tb) {
                    const float tmp = ta;
                    ta = tb;
                    tb = tmp;
                }
                t0 = (std::max)(t0, ta);
                t1 = (std::min)(t1, tb);
                return t0 <= t1;
            };
            if (!clip_axis(
                    local_origin.x,
                    local_dir.x,
                    data.origin[0],
                    data.origin[0] + data.size[0])
                || !clip_axis(
                    local_origin.z,
                    local_dir.z,
                    data.origin[1],
                    data.origin[1] + data.size[1]))
            {
                return false;
            }
            t0 = (std::max)(t0, 0.0f);
            t1 = (std::min)(t1, local_len);
            if (t0 > t1) {
                return false;
            }

            // Reconstruct the DRAWN clipmap surface, keyed off the ray origin
            // (shooter) as the ring center. With no render-LOD params authored
            // this is a no-op and the ray marches the true full-res surface.
            const HeightFieldRayLod lod = make_height_field_ray_lod(
                data, local_origin.x, local_origin.z);

            const auto signed_height = [&](float t) -> float {
                const wz::math::Vec3 p = local_origin + local_dir * t;
                return p.y
                    - height_field_drawn_surface_local_y(data, lod, p.x, p.z);
            };

            // Origin already at/under the surface within the footprint: the ray
            // starts underground, so the entry point is the hit.
            const float f0 = signed_height(t0);
            if (f0 <= 0.0f) {
                const wz::math::Vec3 p = local_origin + local_dir * t0;
                return emit_height_field_ray_hit(
                    entry, data, lod, p.x, p.z, out_sample);
            }

            // March in XZ at ~half a texel per step. `horizontal` is the ray's XZ
            // speed per unit t (local_dir is unit, so <= 1); a near-vertical ray
            // barely moves in XZ, so fall back to a single span and just test the
            // endpoints. Step count is bounded so a grazing ray can't spin.
            const float span = t1 - t0;
            const float cell = (std::min)(
                height_field_pitch(data, data.size[0], data.resolution_x),
                height_field_pitch(data, data.size[1], data.resolution_y));
            const float horizontal = std::sqrt(
                local_dir.x * local_dir.x + local_dir.z * local_dir.z);
            constexpr uint32_t k_max_steps = 4096u;
            float step_t = (horizontal > 1e-4f)
                ? 0.5f * cell / horizontal
                : span;
            if (!(step_t > 0.0f) || !std::isfinite(step_t)) {
                step_t = span > 0.0f ? span : 1.0f;
            }
            step_t = (std::clamp)(
                step_t,
                span / static_cast<float>(k_max_steps),
                (std::max)(span, 1e-6f));

            // `t += step_t` is a NO-OP once step_t falls below half an ULP of t,
            // and `t >= t1` is this loop's only exit -- so a far-away shooter
            // (large t) with a small step spins forever. Measured (#314, C1-C6):
            // with 1 m cells the stall opens at ~9e6 local units, and with 10 cm
            // cells at ~4e6, because a finer field makes step_t smaller. Bound
            // the ITERATION COUNT, which is what k_max_steps was always for --
            // it previously only clamped the step SIZE, so the comment above
            // ("Step count is bounded so a grazing ray can't spin") was false.
            float t_prev = t0;
            uint32_t steps = 0u;
            for (float t = t0 + step_t; ; t += step_t) {
                if (++steps > k_max_steps) {
                    return false;  // no hit found within the step budget
                }
                const bool last = t >= t1;
                const float tc = last ? t1 : t;
                if (signed_height(tc) <= 0.0f) {
                    // Crossing in (t_prev, tc]: bisect for the exact surface.
                    float lo = t_prev;  // above surface
                    float hi = tc;      // at/under surface
                    for (int i = 0; i < 24; ++i) {
                        const float mid = 0.5f * (lo + hi);
                        if (signed_height(mid) > 0.0f) {
                            lo = mid;
                        } else {
                            hi = mid;
                        }
                    }
                    const wz::math::Vec3 p = local_origin + local_dir * hi;
                    return emit_height_field_ray_hit(
                        entry, data, lod, p.x, p.z, out_sample);
                }
                t_prev = tc;
                if (last) {
                    break;
                }
            }
            return false;
        }
    }

    namespace
    {
        // The QUERY side of every public entry point below is finiteness-checked;
        // the ASSET side was trusted, so a NaN in height_samples or
        // vertical_scale produced hit=true with position=(nan,nan,nan) (#314,
        // C1-C3). CollisionAssetData::valid() now refuses to build such an asset,
        // and this is the runtime half of the same guard: whatever the field
        // holds, a sample that is not finite is not a sample.
        //
        // Placed at the public exits rather than at each internal producer so
        // every path -- exact, nearest, clamped, drawn-surface, ray -- is covered
        // by one check that a new internal path cannot forget to call.
        bool finite_sample_or_miss(
            bool found, CollisionSurfaceSample& out_sample) noexcept
        {
            if (!found) {
                return false;
            }
            const bool finite =
                std::isfinite(out_sample.position.x)
                && std::isfinite(out_sample.position.y)
                && std::isfinite(out_sample.position.z)
                && std::isfinite(out_sample.normal.x)
                && std::isfinite(out_sample.normal.y)
                && std::isfinite(out_sample.normal.z);
            if (finite) {
                return true;
            }
            out_sample = CollisionSurfaceSample{};
            return false;
        }
    }

    bool sample_terrain_surface(
        const CollisionWorldEntry& entry,
        float world_x,
        float world_z,
        CollisionSurfaceSample& out_sample,
        const wz::math::Vec3& observer) noexcept
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
            return finite_sample_or_miss(
                sample_height_field_surface(
                    entry,
                    world_x,
                    world_z,
                    out_sample,
                    observer),
                out_sample);

        case wz::engine::assets::CollisionShapeKind::TerrainMeshSurface:
            return finite_sample_or_miss(
                sample_mesh_surface(
                    entry,
                    world_x,
                    world_z,
                    out_sample),
                out_sample);

        default:
            return false;
        }
    }

    bool sample_nearest_terrain_surface(
        const CollisionWorldEntry& entry,
        float world_x,
        float world_z,
        CollisionSurfaceSample& out_sample,
        const wz::math::Vec3& observer) noexcept
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
            // Nearest-surface query: clamp an off-terrain probe to the boundary
            // height so an actor that drove off the heightfield edge sticks to
            // the rim instead of falling through (the exact sampler above does
            // NOT clamp).
            return finite_sample_or_miss(
                sample_height_field_surface(
                    entry,
                    world_x,
                    world_z,
                    out_sample,
                    observer,
                    /*clamp_to_bounds=*/true),
                out_sample);

        case wz::engine::assets::CollisionShapeKind::TerrainMeshSurface:
            return finite_sample_or_miss(
                sample_nearest_mesh_surface(
                    entry,
                    world_x,
                    world_z,
                    out_sample),
                out_sample);

        default:
            return false;
        }
    }

    bool raycast_terrain_surface(
        const CollisionWorldEntry& entry,
        const wz::math::Vec3& ray_origin,
        const wz::math::Vec3& ray_direction,
        float max_distance,
        CollisionSurfaceSample& out_sample) noexcept
    {
        out_sample = CollisionSurfaceSample{};
        if (!entry.enabled
            || !entry.resolved
            || max_distance <= 0.0f
            || !std::isfinite(max_distance)
            || !std::isfinite(ray_origin.x)
            || !std::isfinite(ray_origin.y)
            || !std::isfinite(ray_origin.z))
        {
            return false;
        }

        wz::math::Vec3 direction = ray_direction;
        if (!normalize_checked(direction)) {
            return false;
        }

        switch (entry.resolved->shape_kind) {
        case wz::engine::assets::CollisionShapeKind::TerrainHeightField:
            return finite_sample_or_miss(
                raycast_height_field_surface(
                    entry,
                    ray_origin,
                    direction,
                    max_distance,
                    out_sample),
                out_sample);

        default:
            // Mesh-surface ray-casting stays in the behavior adapter's existing
            // triangle path for now; only the heightfield gap is filled here.
            return false;
        }
    }
}
