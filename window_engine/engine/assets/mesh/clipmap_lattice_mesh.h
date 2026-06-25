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

    // Result of resolving a clipmap's AUTHORED, physical parameters into the
    // GEOMETRIC lattice parameters make_clipmap_lattice_mesh consumes.
    struct ResolvedClipmapLattice
    {
        // The geometric lattice: level_count, base_resolution, and
        // cell_size == metres_per_texel (the finest cell equals one texel).
        ClipmapLatticeParams params{};
        // Actual world-metre reach from the lattice center to its outer edge,
        // reach = floor(m/2) * 2^(L-1) * cell_size -- the generator's EXACT
        // integer half-extent (h = m / 2), so this is the real outer reach of the
        // produced mesh, not an estimate. It is always >= the requested horizon,
        // including on the too-small-budget fallback (coverage is guaranteed).
        float achieved_horizon_metres = 0.0f;
        // Exact triangle count of the chosen config (the same value the
        // generator produces): 2*m^2 + (L-1)*8*((m/2)^2 - (m/4)^2), with all
        // divisions integer. This is <= triangle_budget for every config that
        // fits; on the fallback it may exceed the budget (the budget is a
        // target ceiling, not a hard guarantee).
        uint64_t triangle_count = 0u;
    };

    // Resolve a geometry-clipmap's authored/physical parameters into the
    // geometric lattice parameters the generator consumes. PURE: no I/O, no
    // global state, deterministic for identical inputs.
    //
    // Authored/physical inputs:
    //   horizon_metres    (R) world-space radius the terrain is drawn out to.
    //   triangle_budget   (T) target triangle count for the WHOLE lattice.
    //   metres_per_texel  (s) world size of one height-field texel; this is the
    //                         finest cell size, so cell_size == s exactly.
    //
    // Algorithm (deterministic enumeration over level counts):
    //   1. c = s. Guards: if s is non-finite or <= 0 it is treated as 1.0; if R
    //      is non-finite or <= 0 it is treated as a minimal positive horizon
    //      (the smallest positive float, so L* collapses to 1 / a single fine
    //      block); T is a uint, so T == 0 simply fits no config and triggers the
    //      fallback below.
    //   2. For each candidate L in [1, 24], the minimal base resolution whose
    //      FLOORED half-extent reaches the horizon is m(L) = 2 * ceil( R /
    //      (2^(L-1) * c) ), clamped to the minimal even value 2. (m is twice the
    //      target half so that floor(m/2)*2^(L-1)*c >= R exactly; making m even
    //      also centres the lattice perfectly.) tris(L) = the exact formula above
    //      for m(L), L. Every candidate therefore reaches the horizon.
    //   3. Pick the SMALLEST L whose tris(L) <= T. The smallest fitting L yields
    //      the LARGEST m -> the finest near-field detail the budget allows.
    //   4. If NO L in [1, 24] fits the budget, return the CHEAPEST covering
    //      config (minimum triangle_count over all L) -- not the largest L, since
    //      tris(L) is U-shaped (it falls as m shrinks, then rises once m floors at
    //      2 and rings keep accruing). Coverage is always honoured; the budget is
    //      a TARGET CEILING, so only triangle_count may exceed triangle_budget.
    //
    // Notes:
    //   * The resolver always picks an EVEN m (>= 2). The generator itself tiles
    //     gap-free for any resolution >= 1, so an odd m is still valid if set by
    //     hand; the resolver simply has no reason to emit one.
    //   * reach / achieved_horizon_metres uses the generator's integer half-
    //     extent floor(m/2), so the reported reach is the lattice's true outer
    //     extent and the horizon guarantee (reach >= R) genuinely holds.
    [[nodiscard]] ResolvedClipmapLattice resolve_clipmap_lattice(
        float horizon_metres,
        uint64_t triangle_budget,
        float metres_per_texel) noexcept;

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
