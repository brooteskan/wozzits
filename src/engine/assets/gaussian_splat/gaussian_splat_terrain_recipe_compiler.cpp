// src/engine/assets/gaussian_splat/gaussian_splat_terrain_recipe_compiler.cpp
//
// Multi-field recipe compiler.  Wrapper pattern:
//
//   1. Validate the recipe against the ScalarFieldSet.
//   2. Call make_terrain_surface_splat_cloud() for geometry + default color.
//   3. If color mode != SlopeHeightDebug, iterate the emitted splats and
//      patch color_dc using the recipe's field mappings.
//
// The color patch loop replicates the same (iz outer, ix inner, stride N)
// iteration as the geometry compiler so that splat index i corresponds to
// the correct field cell (ix, iz).

#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace wz::engine::assets
{
    namespace
    {
        constexpr float kSH_C0 = 0.28209479177387814f;
        constexpr float kFieldEpsilon = 1e-7f;

        // Apply a FieldMap to a raw scalar value from a field.
        // Returns a [0,1] clamped value ready for SH DC encoding.
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

        // Encode a [0,1] display value into SH DC coefficient.
        float to_sh_dc(float display) noexcept
        {
            return (display - 0.5f) / kSH_C0;
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
            // No field references to validate.
            break;
        }

        // ── Compile geometry using the existing terrain surface compiler ──

        GaussianSplatCloudData cloud =
            make_terrain_surface_splat_cloud(recipe.geometry, *height_field);

        if (cloud.empty())
        {
            validation.error = "geometry compile produced empty cloud";
            if (out_validation) *out_validation = validation;
            return {};
        }

        // ── Patch colors if not using the default slope/height mode ────

        if (recipe.color_mode != TerrainSplatColorMode::SlopeHeightDebug)
        {
            const uint32_t W = height_field->width;
            const uint32_t H = height_field->height;
            const uint32_t N = (recipe.geometry.subsample_step == 0)
                ? 1u : recipe.geometry.subsample_step;

            // Resolve field pointers for color channels.
            const ScalarFieldData* field_r = nullptr;
            const ScalarFieldData* field_g = nullptr;
            const ScalarFieldData* field_b = nullptr;

            if (recipe.color_mode == TerrainSplatColorMode::RgbFields)
            {
                field_r = fields.get(recipe.color_r.field_name);
                field_g = fields.get(recipe.color_g.field_name);
                field_b = fields.get(recipe.color_b.field_name);
            }
            else if (recipe.color_mode == TerrainSplatColorMode::GrayscaleField)
            {
                field_r = fields.get(recipe.color_gray.field_name);
                // grayscale: same field for all three channels
            }

            // Walk splats in the same (iz, ix, stride N) order as the
            // geometry compiler so splat index matches field coordinate.
            size_t splat_idx = 0;
            for (uint32_t iz = 0; iz < H; iz += N)
            {
                for (uint32_t ix = 0; ix < W; ix += N)
                {
                    if (splat_idx >= cloud.splats.size())
                        break;

                    GaussianSplat& splat = cloud.splats[splat_idx];

                    if (recipe.color_mode == TerrainSplatColorMode::RgbFields)
                    {
                        const float r = apply_field_map(
                            field_r->at(ix, iz), *field_r, recipe.color_r);
                        const float g = apply_field_map(
                            field_g->at(ix, iz), *field_g, recipe.color_g);
                        const float b = apply_field_map(
                            field_b->at(ix, iz), *field_b, recipe.color_b);

                        splat.color_dc[0] = to_sh_dc(r);
                        splat.color_dc[1] = to_sh_dc(g);
                        splat.color_dc[2] = to_sh_dc(b);
                    }
                    else if (recipe.color_mode ==
                             TerrainSplatColorMode::GrayscaleField)
                    {
                        const float v = apply_field_map(
                            field_r->at(ix, iz), *field_r, recipe.color_gray);
                        const float sh = to_sh_dc(v);

                        splat.color_dc[0] = sh;
                        splat.color_dc[1] = sh;
                        splat.color_dc[2] = sh;
                    }

                    ++splat_idx;
                }
            }
        }

        validation.ok = true;
        if (out_validation) *out_validation = validation;
        return cloud;
    }

}  // namespace wz::engine::assets
