// src/engine/assets/mesh/clipmap_lattice_mesh.cpp

#include <engine/assets/mesh/clipmap_lattice_mesh.h>

#include <cmath>
#include <cstdint>
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
        // hole [center-inner, center+inner]. Every ring cell is a plain quad of
        // this level's own cell size: with per-level view-snapping (#207) the
        // adjacent finer level snaps to a nested grid so its outer boundary
        // vertices stay spatially coincident with this ring's hole edge, and the
        // VS's vertical geomorph lerps those finer edge vertices onto the coarse
        // edge — so no crack-fix fan / split cell is needed.
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

        // Round the base resolution up to a multiple of 4. Each outer ring is
        // m/4 coarse cells thick (outer extent (m/2)*2^k minus inner hole
        // (m/2)*2^(k-1), all divided by the level's own cell 2^k, gives m/4), so
        // m must be a multiple of 4 for the ring to be a whole number of cells
        // and for its hole boundary to fall on this level's cell lines. (This is
        // the nesting constraint; it no longer has anything to do with the old
        // boundary-fan split, which #207 removed.)
        uint32_t m = base_resolution < 4u ? 4u : base_resolution;
        m = (m + 3u) & ~3u;
        params.base_resolution = m;

        params.cell_size =
            (std::isfinite(cell_size) && cell_size > 0.0f)
            ? cell_size
            : 1.0f;
        return params;
    }

    MeshData make_clipmap_lattice_mesh(const ClipmapLatticeParams& raw_params)
    {
        const ClipmapLatticeParams params = sanitize_clipmap_lattice_params(
            raw_params.level_count,
            raw_params.base_resolution,
            raw_params.cell_size);

        const int64_t m = static_cast<int64_t>(params.base_resolution);
        const int64_t levels = static_cast<int64_t>(params.level_count);

        // Coarsest cell size in fine units is 2^(L-1). The whole lattice spans
        // (m/2) coarse cells of that size out from the center in each direction.
        const int64_t coarsest_step = int64_t{1} << (levels - 1);
        const int64_t half_extent = (m / 2) * coarsest_step;

        LatticeBuilder b{};
        b.cell_size = params.cell_size;
        b.fine_extent = 2 * half_extent;
        // Place the center so the whole lattice occupies fine indices [0, 2*half].
        b.center = half_extent;
        b.mesh.topology = MeshPrimitiveTopology::TriangleList;
        b.mesh.index_format = MeshIndexFormat::UInt32;
        b.mesh.has_normals = true;
        b.mesh.has_uv0 = true;

        // Level 0: solid fine center, m x m cells of size 1 fine step.
        fill_solid_block(b, b.center - m / 2, b.center - m / 2, m, 1, 0);

        // Levels 1..L-1: concentric plain rings, each at double the previous
        // cell size and tagged with its own level (position.y = k).
        for (int64_t k = 1; k < levels; ++k) {
            const int64_t step = int64_t{1} << k;
            const int64_t outer = (m / 2) * step;
            const int64_t inner = (m / 2) * (step / 2); // == outer extent of k-1
            fill_ring(b, step, outer, inner, k);
        }

        return std::move(b.mesh);
    }
}
