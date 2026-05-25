// engine/assets/gaussian_splat/terrain_splat_compile_service.cpp

#include <engine/assets/gaussian_splat/terrain_splat_compile_service.h>
#include <engine/assets/gaussian_splat/gaussian_splat_compilers.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod_compiler.h>

namespace wz::engine::assets
{

TerrainSplatCompileOutput compile_terrain_splat_surface(
    const TerrainSplatCompileInput& input)
{
    TerrainSplatCompileOutput out;

    // ── Compile cloud ──────────────────────────────────────────────────
    if (input.use_recipe)
    {
        if (!input.field_set)
        {
            out.error = "recipe mode requires a field set";
            return out;
        }

        TerrainSplatRecipeValidation val;
        out.cloud = make_terrain_splat_cloud_from_recipe(
            input.recipe, *input.field_set, &val);
        if (!val.ok)
        {
            out.error = "recipe: " + val.error;
            return out;
        }
    }
    else
    {
        if (!input.field || !input.field->valid())
        {
            out.error = "simple mode requires a valid field snapshot";
            return out;
        }

        out.cloud = make_terrain_surface_splat_cloud(
            input.desc, *input.field);
    }

    if (!out.cloud.valid())
    {
        out.error = "terrain compile produced empty cloud";
        return out;
    }

    // ── Optional color LOD pass ────────────────────────────────────────
    if (input.compute_color_lod)
    {
        out.color_lod = compute_gaussian_splat_color_lod(
            out.cloud, input.color_lod_desc, nullptr);
        if (!out.color_lod.valid())
        {
            out.error = "color LOD compute produced empty data";
            return out;
        }
    }

    out.ok = true;
    return out;
}

}  // namespace wz::engine::assets
