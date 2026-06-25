#pragma once
// window_engine/engine/assets/mesh/clipmap_lattice_mesh.h

#include <engine/assets/mesh/mesh.h>

#include <cstdint>

namespace wz::engine::assets
{
    // Resolved parameters for a geometry-clipmap lattice. Mirrors the authored
    // ClipmapLatticeMeshDesc but holds only the geometry-relevant fields, after
    // sanitization (base_resolution >= 1, level_count >= 1, positive cell_size).
    struct ClipmapLatticeParams
    {
        uint32_t level_count = 4u;
        uint32_t base_resolution = 8u;
        float cell_size = 1.0f;
    };

    // Clamp an authored parameter set into a valid lattice description:
    //   level_count    >= 1
    //   base_resolution >= 1 (no divisibility constraint: the generator tiles
    //                   gap-free for ANY resolution — see make_clipmap_lattice_mesh
    //                   and the .cpp's hole-snapping note)
    //   cell_size      > 0 (falls back to 1.0 for non-finite/non-positive)
    [[nodiscard]] ClipmapLatticeParams sanitize_clipmap_lattice_params(
        uint32_t level_count,
        uint32_t base_resolution,
        float cell_size) noexcept;

    // Generate the nested-LOD-ring lattice in the XZ plane, centered at the
    // origin in grid-unit space. Level 0 is the full base_resolution^2 (m x m)
    // quad center; each outer level k is a square ring of plain quads at cell
    // size 2^k * cell_size whose central hole is covered by the finer levels.
    //
    // Gap-free for ANY base_resolution >= 1 (no multiple-of-4 requirement):
    // completeness is a structural invariant, not a property of the value. Every
    // level is a full grid of WHOLE cells (level k's per-side outer extent is
    // h = floor(m/2) of its own 2^k cells), and each coarse level's hole is the
    // finer level's footprint snapped INWARD to the coarse grid. Snapping inward
    // makes the hole <= the finer footprint, so adjacent levels meet with at most
    // a one-coarse-cell OVERLAP (which the finer level already covers) instead of
    // ever leaving a gap. For odd m the center block is off-origin by half a cell
    // (the symmetric rings still set the bounding box, so it stays centered).
    //
    // Each level OWNS its vertices: vertices are deduplicated per level
    // (ix, iz, level), so boundary positions shared by two adjacent levels
    // resolve to two distinct vertices. This lets the geometry-clipmap vertex
    // shader (#207) snap each level to a multiple of its OWN cell independently;
    // the seams stay crack-free via the VS's nested per-level snap + a vertical
    // geomorph (not via shared boundary vertices).
    //
    // Each vertex's LOD level is stored in position.y (0 = finest center, k =
    // ring k). The lattice is otherwise flat in XZ; the VS overwrites Y with the
    // displaced terrain height. (A non-view-snapped consumer ignores the tag.)
    [[nodiscard]] MeshData make_clipmap_lattice_mesh(
        const ClipmapLatticeParams& params);
}
