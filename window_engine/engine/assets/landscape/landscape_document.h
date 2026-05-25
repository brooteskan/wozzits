#pragma once

// engine/assets/landscape/landscape_document.h
//
// Landscape document: a portable, versioned JSON sidecar that captures
// the user-authored configuration for a terrain surface.
//
// This is a *recipe* document, not a baked asset.  It stores source
// references (field file paths, compile descriptors, render settings)
// sufficient to regenerate the same terrain pipeline.  No compiled
// data (splat clouds, LOD arrays, GPU resources) is stored here.
//
// Design notes for future compatibility:
//   - Top-level "schema" + "version" fields allow format evolution.
//   - Paths are stored relative to the document file.
//   - Unknown fields are silently ignored on load.
//   - Missing optional fields get defaults without error.

#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/gaussian_splat/gaussian_splat_terrain_recipe.h>
#include <engine/assets/gaussian_splat/terrain_coverage_kernel.h>
#include <engine/assets/gaussian_splat/terrain_reconstruction.h>

#include <gpu/gaussian_splat_color_lod_settings.h>
#include <gpu/gaussian_splat_coverage_settings.h>
#include <gpu/dx12/dx12_terrain_field_accum.h>
#include <gpu/dx12/dx12_terrain_surface_mesh.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    // ─── Field source descriptor ─────────────────────────────────────────
    //
    // Identifies a single named scalar field and its source file.
    // Paths are relative to the document's directory.

    struct LandscapeFieldSource
    {
        std::string name;       // field name in the ScalarFieldSet
        std::string path;       // relative file path
        std::string format;     // "r32", "rgb32"
        std::string channel;    // optional: "r", "g", "b" for rgb32 channel split
        uint32_t width  = 0;    // 0 = auto-detect from file size
        uint32_t height = 0;
    };

    // ─── Landscape document ──────────────────────────────────────────────

    struct LandscapeDocument
    {
        // Schema identification.
        static constexpr const char* kSchema  = "wozzits.landscape";
        static constexpr uint32_t    kVersion = 1;

        uint32_t version = kVersion;

        // ── Source fields ──
        std::vector<LandscapeFieldSource> fields;

        // ── Terrain splat recipe ──
        TerrainSplatFieldRecipe terrain_recipe;

        // Whether the toolhost should use the recipe's color/normal/curvature
        // fields (true) or fall back to simple slope/height debug (false).
        bool use_recipe_color = false;

        // ── Color LOD ──
        bool color_lod_enabled = false;
        GaussianSplatColorLODCompileDesc color_lod;

        // ── Preview / toolhost settings (optional, non-critical) ──

        // Active render mode: 0=pull-debug, 1=blend, 2=coverage.
        int active_program = 0;

        wz::gpu::SplatColorLODSettings lod_preview;

        wz::gpu::SplatCoverageSettings coverage_preview;

        // Density / stride tuning.
        int   force_stride = 1;
        float target_spp   = 2.0f;

        wz::gpu::dx12::TerrainFieldAccumSettings field_accum_preview;
        bool field_accum_enabled = false;

        // Surface reconstruction CPU settings.
        TerrainReconDesc surface_recon;
        bool surface_recon_enabled = false;

        // Surface reconstruction render settings.
        wz::gpu::dx12::TerrainSurfaceMeshSettings surface_mesh_preview;
    };

    // ─── Load result ─────────────────────────────────────────────────────

    struct LandscapeLoadResult
    {
        bool ok = false;
        std::string error;
        LandscapeDocument document;
    };

    // ─── API ─────────────────────────────────────────────────────────────

    // Serialize a document to pretty-printed JSON and write to disk.
    // Paths in the document should already be relative to the target dir.
    [[nodiscard]] bool save_landscape_document(
        const LandscapeDocument& doc,
        const std::string& path,
        std::string* error = nullptr);

    // Parse a .wzlandscape.json file from disk.
    [[nodiscard]] LandscapeLoadResult load_landscape_document(
        const std::string& path);

}  // namespace wz::engine::assets
