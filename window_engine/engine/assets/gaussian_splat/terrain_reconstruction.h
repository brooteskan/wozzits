#pragma once

// engine/assets/gaussian_splat/terrain_reconstruction.h
//
// CPU-side terrain surface reconstruction from Gaussian splats.
//
// Treats splats as radial basis functions over the (x, z) domain and
// evaluates weighted-average height, color, and normal on a regular
// grid.  The result is a displaced triangle mesh ready for upload to
// the GPU.
//
// This module is independent of the GPU backend and DX12 — it produces
// plain vertex/index arrays that the caller uploads however it likes.

#include <gpu/gaussian_splat_coverage_settings.h>  // TerrainCoverageKernelMode

#include <cstdint>
#include <span>
#include <vector>

namespace wz::engine::assets
{
    // ── Input: decoded splat projected onto the terrain domain ────────

    struct TerrainReconSplat
    {
        float pos_x, pos_z;     // world XZ position (projection center)
        float height;           // world Y position
        float radius_u;         // world-space extent along tangent U (X)
        float radius_v;         // world-space extent along tangent V (Z)
        float normal[3];        // surface normal (decoded from quaternion)
        float color[3];         // display color  (decoded from SH DC)
        float opacity;          // sigmoid-decoded opacity ∈ (0, 1)
    };

    // ── Output: per-vertex data for the reconstructed mesh ───────────

    struct TerrainReconVertex
    {
        float pos[3];      // POSITION  (offset  0)
        float normal[3];   // NORMAL    (offset 12)
        float color[3];    // COLOR     (offset 24)
        float weight;      // TEXCOORD0 (offset 36) — normalized accum weight
    };
    static_assert(sizeof(TerrainReconVertex) == 40);

    // ── Reconstruction descriptor ────────────────────────────────────

    struct TerrainReconDesc
    {
        // Grid resolution (N × N vertices, (N-1)² × 2 triangles).
        int grid_res = 256;

        // Domain bounds in world space.  If zero-extent, reconstruction
        // computes bounds from the splat positions.
        float domain_min_x = 0, domain_max_x = 0;
        float domain_min_z = 0, domain_max_z = 0;

        // Coverage kernel parameters.
        wz::gpu::TerrainCoverageKernelMode kernel_mode =
            wz::gpu::TerrainCoverageKernelMode::SmoothDisc;
        float radius_scale     = 1.0f;
        float inner_radius     = 0.65f;
        float outer_radius     = 1.0f;
        float gaussian_falloff = 4.0f;

        // When true, derive normals from the reconstructed height gradient
        // (N = normalize(-dH/dx, 1, -dH/dz)) instead of blending the
        // per-splat normals.
        bool use_gradient_normals = true;
    };

    // ── Reconstruction result ────────────────────────────────────────

    struct TerrainReconResult
    {
        std::vector<TerrainReconVertex> vertices;
        std::vector<uint32_t>           indices;

        float height_min = 0;
        float height_max = 0;

        bool valid() const noexcept
        {
            return !vertices.empty() && !indices.empty();
        }
    };

    // ── The reconstruction function ──────────────────────────────────
    //
    // Scatter-based evaluation: for each splat, accumulate its weighted
    // contribution into all covered grid cells.  Complexity is
    // O(splats × cells_per_splat) rather than O(splats × grid²).
    //
    // Returns an invalid result if splats is empty or grid_res < 2.
    TerrainReconResult reconstruct_terrain_grid(
        std::span<const TerrainReconSplat> splats,
        const TerrainReconDesc& desc);

}  // namespace wz::engine::assets
