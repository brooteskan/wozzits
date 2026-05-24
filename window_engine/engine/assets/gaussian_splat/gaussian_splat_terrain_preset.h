#pragma once

// engine/assets/gaussian_splat/gaussian_splat_terrain_preset.h
//
// Named presets for terrain splat surface compilation and rendering.
//
// These capture known-good parameter combinations so that:
//   - Tests compile against the same values the toolhost uses
//   - Future shader/compiler tweaks can be regression-tested
//   - The toolhost can offer "reset to preset" for each configuration
//
// Presets are deliberately simple structs with no behavior — just
// named bundles of parameter values.

#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <gpu/gaussian_splat_coverage_settings.h>

#include <cstdint>

namespace wz::engine::assets
{
    // ── Compile-time surface parameters ─────────────────────────────────
    //
    // Captures the subset of GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc
    // that defines the "shape" of the terrain surface.  Height_scale, step_x,
    // step_z are data-dependent (set per-heightfield), so they are NOT part
    // of the preset — only the parameters that control coverage, smoothing,
    // and disc behavior.
    struct TerrainSplatSurfacePreset
    {
        float    overlap_factor    = 1.25f;
        float    thickness         = 0.0f;   // 0 = auto
        float    max_slope_stretch = 2.0f;
        float    opacity           = 0.95f;
        float    flat_luminance    = 0.55f;
        float    steep_luminance   = 0.30f;
        uint32_t subsample_step    = 1;

        bool     normal_smoothing_enabled      = true;
        uint32_t normal_smoothing_radius_cells = 2;
        float    normal_smoothing_sigma_cells  = 1.0f;

        // Apply this preset's values to a compile desc, leaving the
        // data-dependent fields (height_scale, step_x, step_z) untouched.
        void apply_to(
            GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc& desc) const
        {
            desc.overlap_factor               = overlap_factor;
            desc.thickness                    = thickness;
            desc.max_slope_stretch            = max_slope_stretch;
            desc.opacity                      = opacity;
            desc.flat_luminance               = flat_luminance;
            desc.steep_luminance              = steep_luminance;
            desc.subsample_step               = subsample_step;
            desc.normal_smoothing_enabled      = normal_smoothing_enabled;
            desc.normal_smoothing_radius_cells = normal_smoothing_radius_cells;
            desc.normal_smoothing_sigma_cells  = normal_smoothing_sigma_cells;
        }
    };

    // ── Render-time coverage parameters ─────────────────────────────────
    //
    // Captures the SplatCoverageSettings values for a known-good terrain
    // coverage render.
    struct TerrainSplatCoveragePreset
    {
        wz::gpu::SplatCoverageMode        mode        = wz::gpu::SplatCoverageMode::CoverageDiscard;
        wz::gpu::TerrainCoverageKernelMode kernel_mode = wz::gpu::TerrainCoverageKernelMode::SmoothDisc;

        float radius_scale   = 1.5f;
        float threshold      = 0.4f;
        float inner_radius   = 0.55f;
        float outer_radius   = 1.0f;
        float opacity_scale  = 0.25f;

        void apply_to(wz::gpu::SplatCoverageSettings& settings) const
        {
            settings.mode         = mode;
            settings.kernel_mode  = kernel_mode;
            settings.radius_scale = radius_scale;
            settings.threshold    = threshold;
            settings.inner_radius = inner_radius;
            settings.outer_radius = outer_radius;
            settings.opacity_scale = opacity_scale;
        }
    };

    // ── Named presets ───────────────────────────────────────────────────

    // Smooth Terrain Coverage: connected opaque surface with smooth-disc
    // kernel, coverage-discard mode, normal smoothing enabled.  This is
    // the known-good baseline for terrain surface rendering.
    inline constexpr TerrainSplatSurfacePreset kSmoothTerrainSurface{
        .overlap_factor    = 1.5f,
        .thickness         = 0.0f,
        .max_slope_stretch = 2.0f,
        .opacity           = 0.95f,
        .flat_luminance    = 0.55f,
        .steep_luminance   = 0.30f,
        .subsample_step    = 1,

        .normal_smoothing_enabled      = true,
        .normal_smoothing_radius_cells = 2,
        .normal_smoothing_sigma_cells  = 1.0f,
    };

    inline constexpr TerrainSplatCoveragePreset kSmoothTerrainCoverage{
        .mode          = wz::gpu::SplatCoverageMode::CoverageDiscard,
        .kernel_mode   = wz::gpu::TerrainCoverageKernelMode::SmoothDisc,
        .radius_scale  = 1.5f,
        .threshold     = 0.4f,
        .inner_radius  = 0.55f,
        .outer_radius  = 1.0f,
        .opacity_scale = 0.25f,
    };

}  // namespace wz::engine::assets
