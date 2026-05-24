// src/engine/assets/gaussian_splat/gaussian_splat_terrain_surface_compiler.cpp
//
// Sibling compiler to gaussian_splat_compilers.cpp's
// GaussianSplatFromScalarField, with very different semantics:
//
//   • Treats heightmap values as raw world elevations (no [0,1] normalization).
//   • Emits anisotropic splats whose tangent-plane extents come from the
//     LOCAL heightmap tangent vectors, so splats stretch along slopes.
//   • Builds a per-splat quaternion that aligns the splat's local frame to
//     the surface tangent/normal frame.
//   • Colors splats with a slope-modulated grayscale.
//
// Determinism: single-threaded; integer iteration order is fixed (iz outer,
// ix inner); central differences with one-sided fallbacks at boundaries.

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_math.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace wz::engine::assets
{
    // Import shared terrain math into this TU's namespace for brevity.
    using namespace terrain_math;


    // ─── Compiler implementation ─────────────────────────────────────────────

    GaussianSplatCloudData make_terrain_surface_splat_cloud(
        const GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc& desc,
        const ScalarFieldData& field)
    {
        GaussianSplatCloudData out{};

        const uint32_t W = field.width;
        const uint32_t H = field.height;
        if (W < 2 || H < 2)
            return out;

        const float step_x = desc.step_x;
        const float step_z = desc.step_z;
        const float height_scale = desc.height_scale;

        // Subsample step (>=1).
        const uint32_t N = (desc.subsample_step == 0) ? 1u : desc.subsample_step;

        // Resolved thickness: auto = 0.05 * max(step_x, step_z).
        const float thickness =
            (desc.thickness > 0.0f)
            ? desc.thickness
            : 0.05f * std::max(step_x, step_z);

        // Logit-encoded opacity.
        const float opacity_logit = [&]() {
            const float p = std::clamp(desc.opacity, 1e-4f, 1.0f - 1e-4f);
            return std::log(p / (1.0f - p));
        }();

        // Centre the heightfield grid on the origin.
        const float half_x = 0.5f * static_cast<float>(W - 1) * step_x;
        const float half_z = 0.5f * static_cast<float>(H - 1) * step_z;

        // Reserve approximate output count.
        const uint64_t emit_cells_u = (W + N - 1) / N;
        const uint64_t emit_cells_v = (H + N - 1) / N;
        out.splats.reserve(static_cast<size_t>(emit_cells_u * emit_cells_v));

        float bmin[3] = {
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)()
        };
        float bmax[3] = {
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)()
        };

        float scale_min = std::numeric_limits<float>::max();
        float scale_max = std::numeric_limits<float>::lowest();

        const float overlap = desc.overlap_factor;
        const float N_f = static_cast<float>(N);

        // Slope-stretch clamp: cap tangent magnitudes to prevent
        // extreme disc elongation on steep slopes and at borders.
        const float max_Lu = step_x * std::max(desc.max_slope_stretch, 1.0f);
        const float max_Lv = step_z * std::max(desc.max_slope_stretch, 1.0f);

        // ── Pre-compute raw per-cell normals when smoothing is enabled ──
        // The smoothing pass needs neighbour normals from the FULL grid
        // regardless of subsample_step, so we build a dense raw-normal map
        // up front and Gaussian-average from it during the emit loop.
        const bool smooth_normals =
            desc.normal_smoothing_enabled
         && desc.normal_smoothing_radius_cells > 0
         && desc.normal_smoothing_sigma_cells  > 0.0f;
        std::vector<Vec3f> raw_normals;
        if (smooth_normals)
        {
            raw_normals.resize(static_cast<size_t>(W) * H);
            for (uint32_t iz = 0; iz < H; ++iz) {
                for (uint32_t ix = 0; ix < W; ++ix) {
                    const float dh_u_r = dh_du(field, ix, iz) * height_scale;
                    const float dh_v_r = dh_dv(field, ix, iz) * height_scale;
                    const Vec3f Tu_r{ step_x, dh_u_r, 0.0f };
                    const Vec3f Tv_r{ 0.0f,   dh_v_r, step_z };
                    raw_normals[static_cast<size_t>(ix) + iz * W] =
                        normalize3(cross3(Tv_r, Tu_r));
                }
            }
        }

        const int32_t smooth_radius =
            smooth_normals
                ? static_cast<int32_t>(desc.normal_smoothing_radius_cells)
                : 0;
        const float smooth_sigma = desc.normal_smoothing_sigma_cells;
        const float smooth_two_sigma_sq =
            2.0f * smooth_sigma * smooth_sigma;

        for (uint32_t iz = 0; iz < H; iz += N) {
            for (uint32_t ix = 0; ix < W; ix += N) {

                // ── Position ──
                const float h_raw = field.at(ix, iz);
                const float wx = static_cast<float>(ix) * step_x - half_x;
                const float wy = h_raw * height_scale;
                const float wz = static_cast<float>(iz) * step_z - half_z;

                // ── Tangents (raw heightmap → world) ──
                // Tu, Tv carry both world-XZ extent and world-Y rise.
                // dh/du and dh/dv are per-cell deltas; multiplying by
                // height_scale converts to world Y units.
                const float dh_u = dh_du(field, ix, iz) * height_scale;
                const float dh_v = dh_dv(field, ix, iz) * height_scale;

                const Vec3f Tu{ step_x, dh_u, 0.0f };
                const Vec3f Tv{ 0.0f,   dh_v, step_z };

                const float Lu = length3(Tu);
                const float Lv = length3(Tv);

                // ── Normal ──
                // Raw: cross(Tv, Tu) gives +Y on flat ground.
                // Smoothed: Gaussian-weighted average of raw normals in a
                // neighbourhood, normalized.
                Vec3f N_hat;
                if (smooth_normals)
                {
                    Vec3f n_sum{ 0.0f, 0.0f, 0.0f };
                    float w_sum = 0.0f;

                    const int32_t ix_i = static_cast<int32_t>(ix);
                    const int32_t iz_i = static_cast<int32_t>(iz);

                    const int32_t W_i = static_cast<int32_t>(W);
                    const int32_t H_i = static_cast<int32_t>(H);

                    for (int32_t dz = -smooth_radius; dz <= smooth_radius; ++dz) {
                        const int32_t sz = iz_i + dz;
                        if (sz < 0 || sz >= H_i) continue;
                        for (int32_t dx = -smooth_radius; dx <= smooth_radius; ++dx) {
                            const int32_t sx = ix_i + dx;
                            if (sx < 0 || sx >= W_i) continue;

                            const float d2 = static_cast<float>(dx * dx + dz * dz);
                            const float w  = std::exp(-d2 / smooth_two_sigma_sq);

                            const Vec3f& nr = raw_normals[
                                static_cast<size_t>(sx) + sz * W];
                            n_sum.x += w * nr.x;
                            n_sum.y += w * nr.y;
                            n_sum.z += w * nr.z;
                            w_sum   += w;
                        }
                    }

                    if (w_sum > 1e-6f)
                    {
                        const float inv = 1.0f / w_sum;
                        n_sum.x *= inv; n_sum.y *= inv; n_sum.z *= inv;
                    }
                    N_hat = normalize3(n_sum);
                }
                else
                {
                    const Vec3f N_raw = cross3(Tv, Tu);
                    N_hat = normalize3(N_raw);
                }

                // ── Build orthonormal surface frame from N_hat ──
                // Align the frame's X axis with the heightfield U tangent
                // projected into the tangent plane defined by N_hat.  This
                // ensures scale_u is applied along the actual slope
                // direction rather than an arbitrary perpendicular to the
                // normal.  With smoothed normals N_hat may not be exactly
                // perpendicular to the raw Tu; the projection handles that.
                //
                // Right-handed completion: Z = cross(X, Y) so that
                // quat_from_frame receives a proper rotation matrix with
                // determinant +1.
                const float tu_dot_n = dot3(Tu, N_hat);
                const Vec3f Xs_raw{
                    Tu.x - N_hat.x * tu_dot_n,
                    Tu.y - N_hat.y * tu_dot_n,
                    Tu.z - N_hat.z * tu_dot_n,
                };
                const Vec3f Xs_hat = normalize3(Xs_raw);
                const Vec3f Zs_hat = cross3(Xs_hat, N_hat);

                const Quat q = quat_from_frame(Xs_hat, N_hat, Zs_hat);

                // ── Anisotropic scale (log-encoded) ──
                // Tangent extents grow with both |Tu|/|Tv| (carrying slope
                // stretch automatically) and the subsample factor (so a
                // subsampled grid still covers the surface).
                // Clamp tangent magnitudes to prevent extreme elongation on
                // steep slopes and at heightfield borders.
                const float Lu_c = std::min(Lu, max_Lu);
                const float Lv_c = std::min(Lv, max_Lv);
                const float s_u = std::max(Lu_c * overlap * N_f, 1e-6f);
                const float s_v = std::max(Lv_c * overlap * N_f, 1e-6f);
                const float s_n = std::max(thickness,             1e-6f);

                const float log_s_u = std::log(s_u);
                const float log_s_v = std::log(s_v);
                const float log_s_n = std::log(s_n);

                // ── Slope-modulated grayscale ──
                // slope_factor = 1 on vertical face, 0 on flat ground.
                const float slope_factor =
                    std::clamp(1.0f - N_hat.y, 0.0f, 1.0f);
                const float display = std::clamp(
                    desc.flat_luminance
                    + (desc.steep_luminance - desc.flat_luminance) * slope_factor,
                    0.0f, 1.0f);
                const float sh_dc = (display - 0.5f) / kSH_C0;

                // ── Build splat ──
                GaussianSplat splat{};
                splat.position[0] = wx;
                splat.position[1] = wy;
                splat.position[2] = wz;

                splat.scale[0] = log_s_u;   // along local X → tangent U
                splat.scale[1] = log_s_n;   // along local Y → surface normal
                splat.scale[2] = log_s_v;   // along local Z → tangent V

                // PLY storage convention: rot_0=w, rot_1=x, rot_2=y, rot_3=z.
                splat.rotation[0] = q.w;
                splat.rotation[1] = q.x;
                splat.rotation[2] = q.y;
                splat.rotation[3] = q.z;

                splat.opacity = opacity_logit;

                splat.color_dc[0] = sh_dc;
                splat.color_dc[1] = sh_dc;
                splat.color_dc[2] = sh_dc;

                // Bounds + stats.
                if (wx < bmin[0]) bmin[0] = wx;
                if (wy < bmin[1]) bmin[1] = wy;
                if (wz < bmin[2]) bmin[2] = wz;
                if (wx > bmax[0]) bmax[0] = wx;
                if (wy > bmax[1]) bmax[1] = wy;
                if (wz > bmax[2]) bmax[2] = wz;

                const float min_log = std::min({ log_s_u, log_s_v, log_s_n });
                const float max_log = std::max({ log_s_u, log_s_v, log_s_n });
                if (min_log < scale_min) scale_min = min_log;
                if (max_log > scale_max) scale_max = max_log;

                out.splats.push_back(splat);
            }
        }

        if (out.splats.empty())
            return {};

        // Pad bounds by the largest tangent extent so the AABB encloses the
        // splat ellipsoid quads, not just the centres.  Conservative.
        const float pad = std::exp(scale_max);
        out.bounds.min[0] = bmin[0] - pad;
        out.bounds.min[1] = bmin[1] - pad;
        out.bounds.min[2] = bmin[2] - pad;
        out.bounds.max[0] = bmax[0] + pad;
        out.bounds.max[1] = bmax[1] + pad;
        out.bounds.max[2] = bmax[2] + pad;
        out.bounds.valid  = true;

        out.opacity_min = opacity_logit;
        out.opacity_max = opacity_logit;
        out.scale_min   = scale_min;
        out.scale_max   = scale_max;
        out.f_rest_count = 0;

        return out;
    }


}  // namespace wz::engine::assets


namespace wz::engine::assets::internal
{
    // ─── Compiler registration ──────────────────────────────────────────────

    void register_gaussian_splat_terrain_surface_compiler(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        GaussianSplatCloudTable& cloud_table,
        ScalarFieldTable& scalar_field_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGaussianSplatTerrainSurfaceFromHeightFieldSchema,
            .output_type  = kAssetTypeGaussianSplatCloud,
            .compile = [&logger, &cloud_table, &scalar_field_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> /*dep_nodes*/,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                -> wz::asset::AssetNode
            {
                const auto* desc = std::any_cast<
                    GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc>(
                        &input.meta);

                if (!desc) {
                    logger.error("terrain-surface splat: missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger.error(
                        "terrain-surface splat: expected exactly one scalar "
                        "field dependency");
                    return compile_failed_node(input);
                }

                const ScalarFieldData* field =
                    scalar_field_table.get(dep_handles[0]);
                if (!field || !field->valid()) {
                    logger.error(
                        "terrain-surface splat: source field not found or invalid");
                    return compile_failed_node(input);
                }

                if (desc->step_x <= 0.0f || desc->step_z <= 0.0f) {
                    logger.error(
                        "terrain-surface splat: non-positive step_x or step_z");
                    return compile_failed_node(input);
                }

                if (desc->overlap_factor <= 0.0f) {
                    logger.error(
                        "terrain-surface splat: non-positive overlap_factor");
                    return compile_failed_node(input);
                }

                GaussianSplatCloudData data =
                    make_terrain_surface_splat_cloud(*desc, *field);

                if (!data.valid()) {
                    logger.error(
                        "terrain-surface splat: compile produced empty cloud");
                    return compile_failed_node(input);
                }

                logger.info(
                    "terrain-surface splat compiled: splats="
                    + std::to_string(data.splats.size())
                    + " (from "
                    + std::to_string(field->width) + "x"
                    + std::to_string(field->height)
                    + " field, subsample_step="
                    + std::to_string(desc->subsample_step) + ")");

                wz::asset::ResourceHandle handle =
                    cloud_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error(
                        "terrain-surface splat: failed to store cloud in table");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }
}
