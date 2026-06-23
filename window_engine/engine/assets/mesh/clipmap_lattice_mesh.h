#pragma once
// window_engine/engine/assets/mesh/clipmap_lattice_mesh.h

#include <engine/assets/mesh/mesh.h>

#include <cstdint>

namespace wz::engine::assets
{
    // Resolved parameters for a geometry-clipmap lattice. Mirrors the authored
    // ClipmapLatticeMeshDesc but holds only the geometry-relevant fields, after
    // sanitization (even base_resolution, level_count >= 1, positive cell_size).
    struct ClipmapLatticeParams
    {
        uint32_t level_count = 4u;
        uint32_t base_resolution = 8u;
        float cell_size = 1.0f;
    };

    // Clamp/round an authored parameter set into a valid lattice description:
    //   level_count    >= 1
    //   base_resolution rounded up to a multiple of 4 (>= 4) so the rings nest
    //                   crack-free (see the .cpp for why m % 4 == 0 is required)
    //   cell_size      > 0 (falls back to 1.0 for non-finite/non-positive)
    [[nodiscard]] ClipmapLatticeParams sanitize_clipmap_lattice_params(
        uint32_t level_count,
        uint32_t base_resolution,
        float cell_size) noexcept;

    // Generate the nested-LOD-ring lattice in the XZ plane (y = 0), centered at
    // the origin in grid-unit space. Level 0 is the full base_resolution^2 quad
    // center; each outer level k is a square ring at cell size 2^k * cell_size
    // whose central hole is covered by the finer levels. Coarse/fine ring
    // boundaries are stitched crack-free (the coarse hole-facing edge is split
    // at the finer level's boundary vertex, so there are no T-junctions).
    //
    // Vertices are deduplicated on the shared finest integer grid, so every
    // position emitted by two adjacent levels is one shared vertex.
    [[nodiscard]] MeshData make_clipmap_lattice_mesh(
        const ClipmapLatticeParams& params);
}
