// src/engine/assets/gaussian_splat/gaussian_splat_terrain_recipe_compiler.cpp
//
// Multi-field recipe compiler.
//
// When only color fields are involved (no normal/curvature/peaks), this
// delegates to make_terrain_surface_splat_cloud() and patches colors.
//
// When geometry-affecting fields are present (normal, curvature, peaks),
// this runs its own full geometry loop so that normal blending, curvature-
// based radius modulation, and peaks-based attribute modification happen
// inside the per-splat emit.  The geometry loop matches the existing
// terrain surface compiler's conventions:
//
//   - Iteration order: iz outer, ix inner, stride N
//   - Position: centred on origin
//   - Tangent frame: right-handed, X aligned to heightfield U tangent
//   - Quaternion: PLY convention (w, x, y, z)
//   - Scale: log-encoded, (s_u, s_n, s_v)
//   - Color: SH DC encoding
//   - Normal smoothing: pre-computed raw normals + Gaussian kernel
//
// Attribute application order per splat:
//   1. Position from height field
//   2. Finite-difference normal from height
//   3. If imported normal fields exist, decode Gaea normal
//   4. Blend FD and imported normal according to recipe
//   5. Build right-handed tangent frame from chosen normal
//   6. Base tangent scale from heightfield step/slope/subsample
//   7. Apply curvature/peaks modifiers to scale/opacity
//   8. Store rotation, scale (log), color

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    using namespace terrain_math;

    namespace
    {
        constexpr float kFieldEpsilon = 1e-7f;

        // Apply a FieldMap to a raw scalar value from a field.
        // Returns a [0,1] clamped value.
        float apply_field_map(
            float raw_value,
            const ScalarFieldData& field,
            const FieldMap& map) noexcept
        {
            float v = raw_value;
            if (map.normalize)
            {
                const float range = field.max_value - field.min_value;
                if (range > kFieldEpsilon)
                    v = (v - field.min_value) / range;
                else
                    v = 0.0f;
            }
            v = v * map.scale + map.bias;
            return std::clamp(v, 0.0f, 1.0f);
        }

        // Apply a FieldMap (generic: normalize/scale/bias/clamp) to a raw value.
        float apply_generic_map(
            float raw_value,
            const ScalarFieldData& field,
            bool do_normalize, float scale, float bias) noexcept
        {
            float v = raw_value;
            if (do_normalize)
            {
                const float range = field.max_value - field.min_value;
                if (range > kFieldEpsilon)
                    v = (v - field.min_value) / range;
                else
                    v = 0.0f;
            }
            v = v * scale + bias;
            return std::clamp(v, 0.0f, 1.0f);
        }

        // Validate that a FieldMap references an existing field in the set.
        bool validate_field_map(
            const FieldMap& map,
            const ScalarFieldSet& fields,
            const std::string& channel_name,
            TerrainSplatRecipeValidation& validation)
        {
            if (map.field_name.empty())
            {
                validation.ok    = false;
                validation.error = "color " + channel_name
                    + ": field_name is empty";
                return false;
            }
            if (!fields.contains(map.field_name))
            {
                validation.ok    = false;
                validation.error = "color " + channel_name
                    + ": field '" + map.field_name + "' not found in field set";
                return false;
            }
            return true;
        }

        // Check if geometry-affecting fields are active.
        bool has_geometry_fields(const TerrainSplatFieldRecipe& recipe)
        {
            if (recipe.normal.source != NormalSource::FiniteDifference)
                return true;
            if (!recipe.curvature.field_name.empty()
                && (recipe.curvature.radius_influence > 0.0f
                    || recipe.curvature.opacity_influence > 0.0f))
                return true;
            if (!recipe.peaks.field_name.empty()
                && (recipe.peaks.radius_influence > 0.0f
                    || recipe.peaks.opacity_influence > 0.0f
                    || recipe.peaks.color_influence > 0.0f))
                return true;
            if (recipe.normal_debug != NormalDebugView::Off)
                return true;
            return false;
        }

        // Decode a normal channel value from its stored range to [-1, 1].
        float decode_normal_channel(float v, NormalEncodedRange range) noexcept
        {
            if (range == NormalEncodedRange::ZeroToOne)
                return v * 2.0f - 1.0f;
            return v;  // MinusOneToOne: already in [-1, 1]
        }

        // Decode an imported normal from three scalar fields at (ix, iz).
        Vec3f decode_imported_normal(
            const ScalarFieldData& fx,
            const ScalarFieldData& fy,
            const ScalarFieldData& fz,
            uint32_t ix, uint32_t iz,
            const NormalFieldConfig& cfg) noexcept
        {
            float nx = decode_normal_channel(fx.at(ix, iz), cfg.encoded_range);
            float ny = decode_normal_channel(fy.at(ix, iz), cfg.encoded_range);
            float nz = decode_normal_channel(fz.at(ix, iz), cfg.encoded_range);

            if (cfg.flip_x) nx = -nx;
            if (cfg.flip_z) nz = -nz;

            // Remap axes if source has Z-up convention.
            if (cfg.up_axis == NormalUpAxis::Z)
            {
                // Source: (x, y, z) with Z up
                // Target: (x, z, y) with Y up  (swap y <-> z)
                float tmp = ny;
                ny = nz;
                nz = tmp;
            }

            return normalize3(Vec3f{ nx, ny, nz });
        }
    }


    GaussianSplatCloudData make_terrain_splat_cloud_from_recipe(
        const TerrainSplatFieldRecipe& recipe,
        const ScalarFieldSet& fields,
        TerrainSplatRecipeValidation* out_validation)
    {
        TerrainSplatRecipeValidation validation;

        // ── Validate height field ──────────────────────────────────────

        if (fields.empty())
        {
            validation.error = "field set is empty";
            if (out_validation) *out_validation = validation;
            return {};
        }

        const ScalarFieldData* height_field =
            fields.get(recipe.height_field_name);
        if (!height_field)
        {
            validation.error = "height field '"
                + recipe.height_field_name + "' not found in field set";
            if (out_validation) *out_validation = validation;
            return {};
        }

        // ── Validate color field references ────────────────────────────

        switch (recipe.color_mode)
        {
        case TerrainSplatColorMode::RgbFields:
            if (!validate_field_map(recipe.color_r, fields, "r", validation) ||
                !validate_field_map(recipe.color_g, fields, "g", validation) ||
                !validate_field_map(recipe.color_b, fields, "b", validation))
            {
                if (out_validation) *out_validation = validation;
                return {};
            }
            break;
        case TerrainSplatColorMode::GrayscaleField:
            if (!validate_field_map(recipe.color_gray, fields, "gray", validation))
            {
                if (out_validation) *out_validation = validation;
                return {};
            }
            break;
        case TerrainSplatColorMode::SlopeHeightDebug:
            break;
        }

        // ── Validate normal field references ───────────────────────────

        const bool use_imported_normal =
            recipe.normal.source == NormalSource::ImportedField
         || recipe.normal.source == NormalSource::Blend;

        const ScalarFieldData* normal_fx = nullptr;
        const ScalarFieldData* normal_fy = nullptr;
        const ScalarFieldData* normal_fz = nullptr;

        if (use_imported_normal)
        {
            normal_fx = fields.get(recipe.normal.field_x);
            normal_fy = fields.get(recipe.normal.field_y);
            normal_fz = fields.get(recipe.normal.field_z);

            if (!normal_fx || !normal_fy || !normal_fz)
            {
                validation.error = "normal fields not found: need '"
                    + recipe.normal.field_x + "', '"
                    + recipe.normal.field_y + "', '"
                    + recipe.normal.field_z + "'";
                if (out_validation) *out_validation = validation;
                return {};
            }
        }

        // Also need imported normal for debug views that show it.
        const bool need_imported_for_debug =
            recipe.normal_debug == NormalDebugView::Imported
         || recipe.normal_debug == NormalDebugView::Blended
         || recipe.normal_debug == NormalDebugView::Difference;

        if (need_imported_for_debug && !use_imported_normal)
        {
            // Try to resolve normal fields for debug even if source is FD.
            normal_fx = fields.get(recipe.normal.field_x);
            normal_fy = fields.get(recipe.normal.field_y);
            normal_fz = fields.get(recipe.normal.field_z);
            // Not an error if they're missing — debug view will just show black.
        }

        // ── Validate curvature/peaks field references ──────────────────

        const ScalarFieldData* curvature_field = nullptr;
        if (!recipe.curvature.field_name.empty())
        {
            curvature_field = fields.get(recipe.curvature.field_name);
            if (!curvature_field)
            {
                validation.error = "curvature field '"
                    + recipe.curvature.field_name + "' not found";
                if (out_validation) *out_validation = validation;
                return {};
            }
        }

        const ScalarFieldData* peaks_field = nullptr;
        if (!recipe.peaks.field_name.empty())
        {
            peaks_field = fields.get(recipe.peaks.field_name);
            if (!peaks_field)
            {
                validation.error = "peaks field '"
                    + recipe.peaks.field_name + "' not found";
                if (out_validation) *out_validation = validation;
                return {};
            }
        }

        // ── Decide compile path ────────────────────────────────────────
        // If no geometry-affecting fields are active, delegate to the
        // existing single-field compiler and patch colors (fast path).

        if (!has_geometry_fields(recipe))
        {
            GaussianSplatCloudData cloud =
                make_terrain_surface_splat_cloud(recipe.geometry, *height_field);

            if (cloud.empty())
            {
                validation.error = "geometry compile produced empty cloud";
                if (out_validation) *out_validation = validation;
                return {};
            }

            // Patch colors if not using default slope/height mode.
            if (recipe.color_mode != TerrainSplatColorMode::SlopeHeightDebug)
            {
                const uint32_t W = height_field->width;
                const uint32_t H = height_field->height;
                const uint32_t N = (recipe.geometry.subsample_step == 0)
                    ? 1u : recipe.geometry.subsample_step;

                const ScalarFieldData* fr = nullptr;
                const ScalarFieldData* fg = nullptr;
                const ScalarFieldData* fb = nullptr;

                if (recipe.color_mode == TerrainSplatColorMode::RgbFields)
                {
                    fr = fields.get(recipe.color_r.field_name);
                    fg = fields.get(recipe.color_g.field_name);
                    fb = fields.get(recipe.color_b.field_name);
                }
                else if (recipe.color_mode == TerrainSplatColorMode::GrayscaleField)
                {
                    fr = fields.get(recipe.color_gray.field_name);
                }

                size_t splat_idx = 0;
                for (uint32_t iz = 0; iz < H; iz += N)
                {
                    for (uint32_t ix = 0; ix < W; ix += N)
                    {
                        if (splat_idx >= cloud.splats.size()) break;
                        GaussianSplat& s = cloud.splats[splat_idx];

                        if (recipe.color_mode == TerrainSplatColorMode::RgbFields)
                        {
                            const float r = apply_field_map(fr->at(ix, iz), *fr, recipe.color_r);
                            const float g = apply_field_map(fg->at(ix, iz), *fg, recipe.color_g);
                            const float b = apply_field_map(fb->at(ix, iz), *fb, recipe.color_b);
                            s.color_dc[0] = to_sh_dc(r);
                            s.color_dc[1] = to_sh_dc(g);
                            s.color_dc[2] = to_sh_dc(b);
                        }
                        else
                        {
                            const float v = apply_field_map(fr->at(ix, iz), *fr, recipe.color_gray);
                            const float sh = to_sh_dc(v);
                            s.color_dc[0] = sh;
                            s.color_dc[1] = sh;
                            s.color_dc[2] = sh;
                        }
                        ++splat_idx;
                    }
                }
            }

            validation.ok = true;
            if (out_validation) *out_validation = validation;
            return cloud;
        }

        // ══════════════════════════════════════════════════════════════
        // Full geometry loop — geometry-affecting fields are active
        // ══════════════════════════════════════════════════════════════

        const auto& desc = recipe.geometry;
        const ScalarFieldData& field = *height_field;

        GaussianSplatCloudData out{};

        const uint32_t W = field.width;
        const uint32_t H = field.height;
        if (W < 2 || H < 2)
        {
            validation.error = "height field too small (need >= 2x2)";
            if (out_validation) *out_validation = validation;
            return {};
        }

        const float step_x = desc.step_x;
        const float step_z = desc.step_z;
        const float height_scale = desc.height_scale;
        const uint32_t N = (desc.subsample_step == 0) ? 1u : desc.subsample_step;

        const float thickness =
            (desc.thickness > 0.0f)
            ? desc.thickness
            : 0.05f * std::max(step_x, step_z);

        const float base_opacity_logit = [&]() {
            const float p = std::clamp(desc.opacity, 1e-4f, 1.0f - 1e-4f);
            return std::log(p / (1.0f - p));
        }();

        const float half_x = 0.5f * static_cast<float>(W - 1) * step_x;
        const float half_z = 0.5f * static_cast<float>(H - 1) * step_z;

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

        const float max_Lu = step_x * std::max(desc.max_slope_stretch, 1.0f);
        const float max_Lv = step_z * std::max(desc.max_slope_stretch, 1.0f);

        // ── Pre-compute raw FD normals for smoothing ──
        const bool smooth_normals =
            desc.normal_smoothing_enabled
         && desc.normal_smoothing_radius_cells > 0
         && desc.normal_smoothing_sigma_cells  > 0.0f;
        std::vector<Vec3f> raw_normals;
        if (smooth_normals)
        {
            raw_normals.resize(static_cast<size_t>(W) * H);
            for (uint32_t iz = 0; iz < H; ++iz)
                for (uint32_t ix = 0; ix < W; ++ix)
                {
                    const float dhu = dh_du(field, ix, iz) * height_scale;
                    const float dhv = dh_dv(field, ix, iz) * height_scale;
                    const Vec3f Tu_r{ step_x, dhu, 0.0f };
                    const Vec3f Tv_r{ 0.0f,   dhv, step_z };
                    raw_normals[static_cast<size_t>(ix) + iz * W] =
                        normalize3(cross3(Tv_r, Tu_r));
                }
        }

        const int32_t smooth_radius =
            smooth_normals
                ? static_cast<int32_t>(desc.normal_smoothing_radius_cells)
                : 0;
        const float smooth_sigma = desc.normal_smoothing_sigma_cells;
        const float smooth_two_sigma_sq = 2.0f * smooth_sigma * smooth_sigma;

        // ── Color field pointers ──
        const ScalarFieldData* color_fr = nullptr;
        const ScalarFieldData* color_fg = nullptr;
        const ScalarFieldData* color_fb = nullptr;

        if (recipe.color_mode == TerrainSplatColorMode::RgbFields)
        {
            color_fr = fields.get(recipe.color_r.field_name);
            color_fg = fields.get(recipe.color_g.field_name);
            color_fb = fields.get(recipe.color_b.field_name);
        }
        else if (recipe.color_mode == TerrainSplatColorMode::GrayscaleField)
        {
            color_fr = fields.get(recipe.color_gray.field_name);
        }

        // ── Emit loop ──

        for (uint32_t iz = 0; iz < H; iz += N)
        {
            for (uint32_t ix = 0; ix < W; ix += N)
            {
                // ── 1. Position ──
                const float h_raw = field.at(ix, iz);
                const float wx = static_cast<float>(ix) * step_x - half_x;
                const float wy = h_raw * height_scale;
                const float wz = static_cast<float>(iz) * step_z - half_z;

                // ── 2. Finite-difference tangents + normal ──
                const float dhu = dh_du(field, ix, iz) * height_scale;
                const float dhv = dh_dv(field, ix, iz) * height_scale;

                const Vec3f Tu{ step_x, dhu, 0.0f };
                const Vec3f Tv{ 0.0f,   dhv, step_z };

                const float Lu = length3(Tu);
                const float Lv = length3(Tv);

                // Compute FD normal (possibly smoothed).
                Vec3f fd_normal;
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
                            const float wt = std::exp(-d2 / smooth_two_sigma_sq);
                            const Vec3f& nr = raw_normals[
                                static_cast<size_t>(sx) + sz * W];
                            n_sum.x += wt * nr.x;
                            n_sum.y += wt * nr.y;
                            n_sum.z += wt * nr.z;
                            w_sum   += wt;
                        }
                    }
                    if (w_sum > 1e-6f)
                    {
                        const float inv = 1.0f / w_sum;
                        n_sum.x *= inv; n_sum.y *= inv; n_sum.z *= inv;
                    }
                    fd_normal = normalize3(n_sum);
                }
                else
                {
                    fd_normal = normalize3(cross3(Tv, Tu));
                }

                // ── 3. Decode imported normal (if available) ──
                Vec3f imported_normal = fd_normal;
                if (normal_fx && normal_fy && normal_fz)
                {
                    imported_normal = decode_imported_normal(
                        *normal_fx, *normal_fy, *normal_fz,
                        ix, iz, recipe.normal);
                }

                // ── 4. Blend FD and imported normal ──
                Vec3f N_hat;
                switch (recipe.normal.source)
                {
                case NormalSource::FiniteDifference:
                    N_hat = fd_normal;
                    break;
                case NormalSource::ImportedField:
                    N_hat = imported_normal;
                    break;
                case NormalSource::Blend:
                {
                    const float t = std::clamp(recipe.normal.blend, 0.0f, 1.0f);
                    N_hat = normalize3(lerp3(fd_normal, imported_normal, t));
                    break;
                }
                }

                // ── Normal debug view override ──
                // When a debug view is active, override color to visualize normals.
                Vec3f debug_normal_color{ -1.0f, -1.0f, -1.0f }; // sentinel
                switch (recipe.normal_debug)
                {
                case NormalDebugView::Off:
                    break;
                case NormalDebugView::FiniteDifference:
                    debug_normal_color = {
                        fd_normal.x * 0.5f + 0.5f,
                        fd_normal.y * 0.5f + 0.5f,
                        fd_normal.z * 0.5f + 0.5f };
                    break;
                case NormalDebugView::Imported:
                    debug_normal_color = {
                        imported_normal.x * 0.5f + 0.5f,
                        imported_normal.y * 0.5f + 0.5f,
                        imported_normal.z * 0.5f + 0.5f };
                    break;
                case NormalDebugView::Blended:
                    debug_normal_color = {
                        N_hat.x * 0.5f + 0.5f,
                        N_hat.y * 0.5f + 0.5f,
                        N_hat.z * 0.5f + 0.5f };
                    break;
                case NormalDebugView::Difference:
                {
                    Vec3f diff = fd_normal - imported_normal;
                    debug_normal_color = {
                        std::abs(diff.x),
                        std::abs(diff.y),
                        std::abs(diff.z) };
                    break;
                }
                }

                // ── 5. Build right-handed tangent frame ──
                const float tu_dot_n = dot3(Tu, N_hat);
                const Vec3f Xs_raw{
                    Tu.x - N_hat.x * tu_dot_n,
                    Tu.y - N_hat.y * tu_dot_n,
                    Tu.z - N_hat.z * tu_dot_n,
                };
                const Vec3f Xs_hat = normalize3(Xs_raw);
                const Vec3f Zs_hat = cross3(Xs_hat, N_hat);

                const Quat q = quat_from_frame(Xs_hat, N_hat, Zs_hat);

                // ── 6. Base tangent scale ──
                const float Lu_c = std::min(Lu, max_Lu);
                const float Lv_c = std::min(Lv, max_Lv);
                float s_u = std::max(Lu_c * overlap * N_f, 1e-6f);
                float s_v = std::max(Lv_c * overlap * N_f, 1e-6f);
                float s_n = std::max(thickness,             1e-6f);

                // ── 7. Curvature modifiers ──
                float opacity_logit = base_opacity_logit;

                if (curvature_field
                    && (recipe.curvature.radius_influence > 0.0f
                        || recipe.curvature.opacity_influence > 0.0f))
                {
                    const float c = apply_generic_map(
                        curvature_field->at(ix, iz), *curvature_field,
                        recipe.curvature.normalize,
                        recipe.curvature.scale, recipe.curvature.bias);

                    if (recipe.curvature.radius_influence > 0.0f)
                    {
                        // lerp(1.0, min_radius_mul, c * radius_influence)
                        const float t = c * recipe.curvature.radius_influence;
                        const float radius_mul = 1.0f
                            + t * (recipe.curvature.min_radius_mul - 1.0f);
                        s_u *= radius_mul;
                        s_v *= radius_mul;
                    }

                    if (recipe.curvature.opacity_influence > 0.0f)
                    {
                        // lerp(1.0, opacity_boost, c * opacity_influence)
                        const float t = c * recipe.curvature.opacity_influence;
                        const float op_mul = 1.0f
                            + t * (recipe.curvature.opacity_boost - 1.0f);
                        // Convert logit back to probability, multiply, re-encode.
                        const float p_orig = 1.0f / (1.0f + std::exp(-opacity_logit));
                        const float p_new = std::clamp(p_orig * op_mul, 1e-4f, 1.0f - 1e-4f);
                        opacity_logit = std::log(p_new / (1.0f - p_new));
                    }
                }

                // ── Peaks modifiers ──
                float peaks_brightness_add = 0.0f;

                if (peaks_field
                    && (recipe.peaks.radius_influence > 0.0f
                        || recipe.peaks.opacity_influence > 0.0f
                        || recipe.peaks.color_influence > 0.0f))
                {
                    const float pk = apply_generic_map(
                        peaks_field->at(ix, iz), *peaks_field,
                        recipe.peaks.normalize,
                        recipe.peaks.scale, recipe.peaks.bias);

                    if (recipe.peaks.radius_influence > 0.0f)
                    {
                        const float t = pk * recipe.peaks.radius_influence;
                        const float radius_mul = 1.0f
                            + t * (recipe.peaks.min_radius_mul - 1.0f);
                        s_u *= radius_mul;
                        s_v *= radius_mul;
                    }

                    if (recipe.peaks.opacity_influence > 0.0f)
                    {
                        const float t = pk * recipe.peaks.opacity_influence;
                        const float op_mul = 1.0f
                            + t * (recipe.peaks.opacity_boost - 1.0f);
                        const float p_orig = 1.0f / (1.0f + std::exp(-opacity_logit));
                        const float p_new = std::clamp(p_orig * op_mul, 1e-4f, 1.0f - 1e-4f);
                        opacity_logit = std::log(p_new / (1.0f - p_new));
                    }

                    if (recipe.peaks.color_influence > 0.0f)
                    {
                        peaks_brightness_add =
                            pk * recipe.peaks.color_influence
                                * recipe.peaks.color_brightness_boost;
                    }
                }

                // ── 8. Log-encoded scale ──
                s_u = std::max(s_u, 1e-6f);
                s_v = std::max(s_v, 1e-6f);

                const float log_s_u = std::log(s_u);
                const float log_s_v = std::log(s_v);
                const float log_s_n = std::log(s_n);

                // ── 9. Color ──
                float disp_r, disp_g, disp_b;

                // If debug normal view is active, override color.
                if (debug_normal_color.x >= 0.0f)
                {
                    disp_r = std::clamp(debug_normal_color.x, 0.0f, 1.0f);
                    disp_g = std::clamp(debug_normal_color.y, 0.0f, 1.0f);
                    disp_b = std::clamp(debug_normal_color.z, 0.0f, 1.0f);
                }
                else if (recipe.color_mode == TerrainSplatColorMode::RgbFields
                         && color_fr && color_fg && color_fb)
                {
                    disp_r = apply_field_map(color_fr->at(ix, iz), *color_fr, recipe.color_r);
                    disp_g = apply_field_map(color_fg->at(ix, iz), *color_fg, recipe.color_g);
                    disp_b = apply_field_map(color_fb->at(ix, iz), *color_fb, recipe.color_b);
                }
                else if (recipe.color_mode == TerrainSplatColorMode::GrayscaleField
                         && color_fr)
                {
                    const float v = apply_field_map(
                        color_fr->at(ix, iz), *color_fr, recipe.color_gray);
                    disp_r = disp_g = disp_b = v;
                }
                else
                {
                    // SlopeHeightDebug fallback.
                    const float slope_factor =
                        std::clamp(1.0f - N_hat.y, 0.0f, 1.0f);
                    const float display = std::clamp(
                        desc.flat_luminance
                        + (desc.steep_luminance - desc.flat_luminance) * slope_factor,
                        0.0f, 1.0f);
                    disp_r = disp_g = disp_b = display;
                }

                // Apply peaks brightness boost.
                if (peaks_brightness_add > 0.0f)
                {
                    disp_r = std::clamp(disp_r + peaks_brightness_add, 0.0f, 1.0f);
                    disp_g = std::clamp(disp_g + peaks_brightness_add, 0.0f, 1.0f);
                    disp_b = std::clamp(disp_b + peaks_brightness_add, 0.0f, 1.0f);
                }

                // ── Build splat ──
                GaussianSplat splat{};
                splat.position[0] = wx;
                splat.position[1] = wy;
                splat.position[2] = wz;

                splat.scale[0] = log_s_u;
                splat.scale[1] = log_s_n;
                splat.scale[2] = log_s_v;

                splat.rotation[0] = q.w;
                splat.rotation[1] = q.x;
                splat.rotation[2] = q.y;
                splat.rotation[3] = q.z;

                splat.opacity = opacity_logit;

                splat.color_dc[0] = to_sh_dc(disp_r);
                splat.color_dc[1] = to_sh_dc(disp_g);
                splat.color_dc[2] = to_sh_dc(disp_b);

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
        {
            validation.error = "geometry compile produced empty cloud";
            if (out_validation) *out_validation = validation;
            return {};
        }

        const float pad = std::exp(scale_max);
        out.bounds.min[0] = bmin[0] - pad;
        out.bounds.min[1] = bmin[1] - pad;
        out.bounds.min[2] = bmin[2] - pad;
        out.bounds.max[0] = bmax[0] + pad;
        out.bounds.max[1] = bmax[1] + pad;
        out.bounds.max[2] = bmax[2] + pad;
        out.bounds.valid  = true;

        out.opacity_min = base_opacity_logit;
        out.opacity_max = base_opacity_logit;
        out.scale_min   = scale_min;
        out.scale_max   = scale_max;
        out.f_rest_count = 0;

        validation.ok = true;
        if (out_validation) *out_validation = validation;
        return out;
    }

}  // namespace wz::engine::assets
