// src/engine/assets/mesh/clipmap_lattice_mesh.cpp

#include <engine/assets/mesh/clipmap_lattice_mesh.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace wz::engine::assets
{
    namespace
    {
        // The lattice is built on one shared integer grid at the finest cell
        // size, but each LOD level OWNS its own vertices: the dedup key includes
        // the level (ix, iz, level), so a position that two adjacent levels both
        // touch on the shared grid resolves to TWO vertices (one per level).
        //
        // This per-level ownership is what makes per-level view-snapping work
        // (issue #207): the vertex shader snaps each level to a multiple of its
        // OWN cell (T_L = floor(camera/(2*c_L))*(2*c_L)), so a level's vertices
        // must not be aliased to another level's — otherwise one vertex would
        // have to follow two different snaps. The seams are kept crack-free not
        // by sharing boundary vertices (as the old uniform-snap build did) but by
        // the VS's nested per-level snap (adjacent boundaries stay spatially
        // coincident) plus a vertical geomorph that lerps the finer level's edge
        // height to the coarser level's, so the extra finer boundary vertices
        // land exactly on the coarse edge (no T-junction).
        //
        // Each vertex stores its level in position.y (otherwise unused: the
        // lattice is flat and the VS overwrites Y with the displaced height).
        struct LatticeBuilder
        {
            MeshData mesh;

            // Center index on the finest grid (world origin maps here).
            int64_t center = 0;
            // Fine cells per side across the whole lattice.
            int64_t fine_extent = 0;
            float cell_size = 1.0f;

            // (ix, iz, level) packed key -> vertex index in mesh.vertices.
            std::unordered_map<uint64_t, uint32_t> vertex_lookup;

            static uint64_t pack(int64_t ix, int64_t iz, int64_t level) noexcept
            {
                // fine_extent is bounded well under 2^24 and level under 2^16,
                // so a 24/24/16-bit pack (axes offset to stay non-negative) is
                // lossless here.
                const uint64_t ux =
                    static_cast<uint64_t>(static_cast<uint32_t>(
                        static_cast<int32_t>(ix))) & 0xFFFFFFull;
                const uint64_t uz =
                    static_cast<uint64_t>(static_cast<uint32_t>(
                        static_cast<int32_t>(iz))) & 0xFFFFFFull;
                const uint64_t ul =
                    static_cast<uint64_t>(level) & 0xFFFFull;
                return (ux << 40) | (uz << 16) | ul;
            }

            uint32_t vertex_at(int64_t ix, int64_t iz, int64_t level)
            {
                const uint64_t key = pack(ix, iz, level);
                const auto it = vertex_lookup.find(key);
                if (it != vertex_lookup.end()) {
                    return it->second;
                }

                MeshVertex v{};
                v.position[0] =
                    static_cast<float>(ix - center) * cell_size;
                // position.y carries the LOD level (0 = finest center, k = ring
                // k). The VS reads it to compute the per-level snap + morph; the
                // displaced surface height overwrites Y, so this never reaches a
                // pixel. When the renderable is NOT view-snapped (an arbitrary
                // supplied mesh), the VS ignores this and treats Y as geometry.
                v.position[1] = static_cast<float>(level);
                v.position[2] =
                    static_cast<float>(iz - center) * cell_size;
                // Lattice lies flat in XZ; the upward normal is valid storage.
                // The VS recomputes shading normals after height displacement.
                v.normal[1] = 1.0f;

                // UV spans [0,1] across the full lattice footprint so the
                // renderer can derive a heightmap sample coordinate per vertex.
                const float span = static_cast<float>(fine_extent);
                v.uv[0] = span > 0.0f
                    ? static_cast<float>(ix) / span
                    : 0.0f;
                v.uv[1] = span > 0.0f
                    ? static_cast<float>(iz) / span
                    : 0.0f;

                const uint32_t index =
                    static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(v);
                vertex_lookup.emplace(key, index);
                return index;
            }

            void add_triangle(uint32_t a, uint32_t b, uint32_t c)
            {
                mesh.indices.push_back(a);
                mesh.indices.push_back(b);
                mesh.indices.push_back(c);
            }

            // Emit one axis-aligned quad as two triangles, owned by `level`.
            // Winding is CCW when viewed from +Y (the lattice's outward face),
            // matching the upward normal stored on each vertex. With x1 > x0 and
            // z1 > z0, the order (v00, v10, v11) / (v00, v11, v01) gives a
            // positive XZ signed area.
            void add_quad(
                int64_t x0,
                int64_t z0,
                int64_t x1,
                int64_t z1,
                int64_t level)
            {
                const uint32_t v00 = vertex_at(x0, z0, level);
                const uint32_t v10 = vertex_at(x1, z0, level);
                const uint32_t v11 = vertex_at(x1, z1, level);
                const uint32_t v01 = vertex_at(x0, z1, level);
                add_triangle(v00, v10, v11);
                add_triangle(v00, v11, v01);
            }
        };

        // Fill a solid grid of [cells x cells] quads of the given fine-step
        // size, with its lower corner at (x0, z0) on the fine grid. Used for the
        // level-0 center.
        void fill_solid_block(
            LatticeBuilder& b,
            int64_t x0,
            int64_t z0,
            int64_t cells,
            int64_t step,
            int64_t level)
        {
            for (int64_t cz = 0; cz < cells; ++cz) {
                for (int64_t cx = 0; cx < cells; ++cx) {
                    const int64_t ax = x0 + cx * step;
                    const int64_t az = z0 + cz * step;
                    b.add_quad(ax, az, ax + step, az + step, level);
                }
            }
        }

        // Build one clipmap ring at coarse cell size `step` (fine units),
        // occupying coarse cells in [center-outer, center+outer] minus the inner
        // hole [center-inner, center+inner]. The caller guarantees `outer` and
        // `inner` are both exact multiples of `step` measured from `center`, so
        // every emitted cell is a WHOLE step-sized quad (the lattice never drops
        // a straddling partial cell) and the hole removes whole cells only. The
        // hole is the previous (finer) level's footprint snapped INWARD to this
        // level's grid, so hole <= finer footprint: adjacent levels meet with at
        // most a one-coarse-cell overlap that the finer level already covers, and
        // there is never a gap regardless of base_resolution.
        //
        // Every ring cell is a plain quad of this level's own cell size: with
        // per-level view-snapping (#207) the adjacent finer level snaps to a
        // nested grid so its outer boundary vertices stay spatially coincident
        // with this ring's hole edge, and the VS's vertical geomorph lerps those
        // finer edge vertices onto the coarse edge — so no crack-fix fan / split
        // cell is needed.
        void fill_ring(
            LatticeBuilder& b,
            int64_t step,
            int64_t outer,
            int64_t inner,
            int64_t level)
        {
            const int64_t lo_out = b.center - outer;
            const int64_t hi_out = b.center + outer;
            const int64_t lo_in = b.center - inner;
            const int64_t hi_in = b.center + inner;

            for (int64_t z = lo_out; z < hi_out; z += step) {
                for (int64_t x = lo_out; x < hi_out; x += step) {
                    // Skip cells strictly inside the hole.
                    const bool inside_hole =
                        x >= lo_in && x < hi_in
                        && z >= lo_in && z < hi_in;
                    if (inside_hole) {
                        continue;
                    }
                    b.add_quad(x, z, x + step, z + step, level);
                }
            }
        }
    } // namespace

    ClipmapLatticeParams sanitize_clipmap_lattice_params(
        uint32_t level_count,
        uint32_t base_resolution,
        float cell_size) noexcept
    {
        ClipmapLatticeParams params{};
        params.level_count = level_count < 1u ? 1u : level_count;

        // Any base resolution >= 1 is valid: completeness is a structural
        // property of the generator (each coarse level's hole is the finer
        // level's footprint snapped INWARD to the coarse grid, so hole <= finer
        // footprint and the seam is an overlap, never a gap), so there is no
        // divisibility constraint to enforce here. Only floor a zero resolution
        // up to 1 so level 0 is a non-empty 1x1 center.
        params.base_resolution = base_resolution < 1u ? 1u : base_resolution;

        params.cell_size =
            (std::isfinite(cell_size) && cell_size > 0.0f)
            ? cell_size
            : 1.0f;
        return params;
    }

    namespace
    {
        // Exact triangle count of a lattice with base resolution m and L levels,
        // mirroring tests/asset_mesh/clipmap_lattice_mesh.cpp's
        // expected_lattice_triangles (and the generator's emitted geometry):
        //   level 0 is a solid m x m grid           -> 2*m^2 triangles,
        //   each ring k>=1 spans h cells/side minus a q-cell/side hole, all whole
        //     cells, with h = floor(m/2), q = floor(h/2) = floor(m/4)
        //                                              -> 8*(h^2 - q^2) tris.
        // Every ring is identical, so total = 2*m^2 + (L-1)*8*(h^2 - q^2).
        // Computed in 64-bit so a large m * L (the budget-fallback corner) cannot
        // overflow (m <= ~2^31, h^2 fits in 64-bit, L <= 24).
        uint64_t exact_lattice_triangles(uint64_t m, uint64_t level_count) noexcept
        {
            const uint64_t h = m / 2u;
            const uint64_t q = h / 2u;  // == m / 4
            const uint64_t level0 = 2u * m * m;
            const uint64_t per_ring = 8u * (h * h - q * q);
            return level0 + (level_count - 1u) * per_ring;
        }
    } // namespace

    ResolvedClipmapLattice resolve_clipmap_lattice(
        float horizon_metres,
        uint64_t triangle_budget,
        float metres_per_texel) noexcept
    {
        // Guard the finest cell size: non-finite / non-positive falls back to 1.
        // This is the SAME finest-cell that sanitize_clipmap_lattice_params will
        // hold, so cell_size round-trips through the generator unchanged.
        const float c =
            (std::isfinite(metres_per_texel) && metres_per_texel > 0.0f)
            ? metres_per_texel
            : 1.0f;

        // Guard the horizon: a non-finite / non-positive radius becomes the
        // smallest positive float so 2R/(...) underflows toward 0 and m(L)
        // clamps to 1 at every L -> the result collapses to the cheapest single
        // fine block rather than producing a degenerate or huge lattice.
        const double r =
            (std::isfinite(horizon_metres) && horizon_metres > 0.0f)
            ? static_cast<double>(horizon_metres)
            : static_cast<double>(std::numeric_limits<float>::min());

        constexpr uint32_t kLmax = 24u;

        // Minimal base resolution whose FLOORED half-extent reaches the horizon.
        // The generator's per-side reach is floor(m/2) * 2^(L-1) * c (an integer
        // half: see make_clipmap_lattice_mesh's `h = m / 2`), so we need
        // floor(m/2) >= ceil( R / (2^(L-1) * c) ); the minimal such m is twice
        // that target half -- always EVEN, which also centres the lattice exactly.
        // (Using a float m/2 here would let an odd m claim a reach the generated
        // mesh never achieves, under-covering the requested horizon.)
        const auto resolution_for = [&](uint32_t level_count) noexcept -> uint64_t {
            const double coarsest = std::ldexp(1.0, static_cast<int>(level_count) - 1);
            const double needed_half = r / (coarsest * static_cast<double>(c));
            const double target_half = std::ceil(needed_half);
            if (!(target_half >= 1.0)) {
                return 2u;  // also catches NaN / non-positive: the minimal even m
            }
            return 2u * static_cast<uint64_t>(target_half);
        };

        const auto make_result = [&](uint32_t level_count) noexcept {
            const uint64_t m = resolution_for(level_count);
            ResolvedClipmapLattice out{};
            out.params.level_count = level_count;
            out.params.base_resolution = static_cast<uint32_t>(m);
            out.params.cell_size = c;
            // reach = floor(m/2) * 2^(L-1) * c -- the generator's EXACT integer
            // half-extent, so this is the real outer reach of the produced mesh
            // (a guarantee that is met, not an upper bound). m is even here, so
            // floor(m/2) == m/2. 2^(L-1) via ldexp avoids overflow at large L.
            const double coarsest = std::ldexp(1.0, static_cast<int>(level_count) - 1);
            out.achieved_horizon_metres = static_cast<float>(
                static_cast<double>(m / 2u) * coarsest * static_cast<double>(c));
            out.triangle_count = exact_lattice_triangles(m, level_count);
            return out;
        };

        // Every candidate L reaches the horizon by construction (resolution_for
        // guarantees floor(m/2)*2^(L-1)*c >= R), so the only question is the
        // budget. Pick the SMALLEST L whose triangle count fits -- that gives the
        // largest m, the finest near-field detail the budget allows. Triangle
        // count is U-shaped in L (it falls as m shrinks, then rises again once m
        // floors at 2 and rings keep accruing), so if nothing fits we fall back to
        // the CHEAPEST horizon-covering config (minimum triangles), not the
        // largest L. Coverage is always honoured; the budget is a target ceiling.
        uint32_t cheapest_level = 1u;
        uint64_t cheapest_tris =
            exact_lattice_triangles(resolution_for(1u), 1u);
        for (uint32_t level_count = 1u; level_count <= kLmax; ++level_count) {
            const uint64_t tris =
                exact_lattice_triangles(resolution_for(level_count), level_count);
            if (tris <= triangle_budget) {
                return make_result(level_count);
            }
            if (tris < cheapest_tris) {
                cheapest_tris = tris;
                cheapest_level = level_count;
            }
        }
        return make_result(cheapest_level);
    }

    MeshData make_clipmap_lattice_mesh(const ClipmapLatticeParams& raw_params)
    {
        const ClipmapLatticeParams params = sanitize_clipmap_lattice_params(
            raw_params.level_count,
            raw_params.base_resolution,
            raw_params.cell_size);

        const int64_t m = static_cast<int64_t>(params.base_resolution);
        const int64_t levels = static_cast<int64_t>(params.level_count);

        // Half the base resolution in finest cells (floor for odd m). This is
        // the per-side outer extent of every level measured in that level's OWN
        // cells: level k's outer half-extent is h * 2^k fine units (h cells of
        // size 2^k), so the boundary is always on level k's grid for any m.
        const int64_t h = m / 2;

        // Coarsest cell size in fine units is 2^(L-1); the whole lattice spans
        // h coarse cells of that size out from the center in each direction. This
        // extent is identical to the pre-rewrite formula (for m a multiple of 4
        // the whole geometry is byte-for-byte unchanged), so the view transform's
        // clipmap_lattice_grid_extent still mirrors it exactly.
        const int64_t coarsest_step = int64_t{1} << (levels - 1);
        const int64_t half_extent = h * coarsest_step;

        LatticeBuilder b{};
        b.cell_size = params.cell_size;
        b.fine_extent = 2 * half_extent;
        // Place the center so the whole lattice occupies fine indices [0, 2*half].
        b.center = half_extent;
        b.mesh.topology = MeshPrimitiveTopology::TriangleList;
        b.mesh.index_format = MeshIndexFormat::UInt32;
        b.mesh.has_normals = true;
        b.mesh.has_uv0 = true;

        // Level 0: solid fine center, exactly m x m cells of size 1 fine step.
        // The lower corner is center - floor(m/2) so the block is centered on the
        // integer grid for even m; for odd m it is off-center by half a cell (the
        // extra cell lands on the +X/+Z side), which is an accepted approximation
        // and never reaches the outer bounds (those are set by the symmetric
        // rings below, so the whole lattice's bounding box stays symmetric).
        fill_solid_block(b, b.center - h, b.center - h, m, 1, 0);

        // Levels 1..L-1: concentric plain rings, each at double the previous
        // cell size and tagged with its own level (position.y = k).
        //
        // outer_k = h * 2^k  (a whole number of this level's cells per side), and
        // the hole = the finer level's footprint snapped INWARD to this level's
        // grid: inner_k = floor(h/2) * 2^k. Snapping inward guarantees
        // inner_k <= outer_{k-1} = h * 2^(k-1) (since floor(h/2)*2 <= h), so the
        // finer level always fully covers this level's hole — adjacent levels
        // overlap by at most one coarse cell instead of risking a gap. For m a
        // multiple of 4, h is even and floor(h/2)*2^k == h*2^(k-1) == the old
        // inner hole exactly, so multiples of 4 reproduce the previous geometry.
        int64_t prev_h = h;  // outer half-extent of level k-1, in fine units
        for (int64_t k = 1; k < levels; ++k) {
            const int64_t step = int64_t{1} << k;
            const int64_t outer = h * step;
            // floor(prev_h / step) * step == floor(h/2) * step: the finer
            // footprint (h * 2^(k-1) fine units) snapped down to this grid.
            const int64_t inner = (prev_h / step) * step;
            fill_ring(b, step, outer, inner, k);
            prev_h = outer;
        }

        return std::move(b.mesh);
    }
}
