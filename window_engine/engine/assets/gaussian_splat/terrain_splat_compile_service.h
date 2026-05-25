#pragma once

// engine/assets/gaussian_splat/terrain_splat_compile_service.h
//
// Pure CPU terrain splat compilation service.
//
// Wraps the two compile paths (simple heightfield + optional recipe) and
// optional color-LOD pass into a single call with value-type input/output.
// No GPU, no asset system, no side effects — just data in, data out.
//
// Consumers:
//   - Toolhost `apply_live_terrain_recompile()` calls this for live tuning.
//   - Future headless compile tools can use the same interface.

#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_recipe.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/scalar_field/scalar_field_set.h>

#include <string>

namespace wz::engine::assets
{
    // ─── Compile input ───────────────────────────────────────────────────
    //
    // Describes what to compile.  Exactly one of two modes:
    //
    //   Simple:  desc + field  (use_recipe == false)
    //   Recipe:  recipe + field_set  (use_recipe == true)
    //
    // Color LOD is optionally computed after the cloud compile.

    struct TerrainSplatCompileInput
    {
        // ── Simple-mode fields (used when use_recipe == false) ──
        GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc desc;
        const ScalarFieldData* field = nullptr;

        // ── Recipe-mode fields (used when use_recipe == true) ──
        bool use_recipe = false;
        TerrainSplatFieldRecipe recipe;
        const ScalarFieldSet* field_set = nullptr;

        // ── Optional color LOD pass ──
        bool compute_color_lod = false;
        GaussianSplatColorLODCompileDesc color_lod_desc;
    };

    // ─── Compile output ──────────────────────────────────────────────────
    //
    // On success: ok == true, cloud is valid, and color_lod is valid iff
    // the input requested it.
    //
    // On failure: ok == false, error describes what went wrong, cloud and
    // color_lod are empty/invalid.

    struct TerrainSplatCompileOutput
    {
        bool ok = false;
        std::string error;

        GaussianSplatCloudData cloud;
        GaussianSplatColorLODData color_lod;
    };

    // ─── Compile function ────────────────────────────────────────────────
    //
    // Pure, deterministic, no side effects.  Identical input → identical
    // output.

    [[nodiscard]] TerrainSplatCompileOutput compile_terrain_splat_surface(
        const TerrainSplatCompileInput& input);

}  // namespace wz::engine::assets
