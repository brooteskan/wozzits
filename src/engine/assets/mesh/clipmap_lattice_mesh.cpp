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
        // size. A vertex is identified by its integer grid coordinate (ix, iz);
        // coincident coordinates from different LOD levels resolve to one shared
        // vertex, which is what makes the LOD seams structurally crack-free.
        struct LatticeBuilder
        {
            MeshData mesh;

            // Center index on the finest grid (world origin maps here).
            int64_t center = 0;
            // Fine cells per side across the whole lattice.
            int64_t fine_extent = 0;
            float cell_size = 1.0f;

            // (ix, iz) packed key -> vertex index in mesh.vertices.
            std::unordered_map<uint64_t, uint32_t> vertex_lookup;

            static uint64_t pack(int64_t ix, int64_t iz) noexcept
            {
                // fine_extent is bounded well under 2^31, so a 32-bit-per-axis
                // pack (offset to keep it non-negative) is lossless here.
                const uint64_t ux =
                    static_cast<uint64_t>(static_cast<uint32_t>(
                        static_cast<int32_t>(ix)));
                const uint64_t uz =
                    static_cast<uint64_t>(static_cast<uint32_t>(
                        static_cast<int32_t>(iz)));
                return (ux << 32) | uz;
            }

            uint32_t vertex_at(int64_t ix, int64_t iz)
            {
                const uint64_t key = pack(ix, iz);
                const auto it = vertex_lookup.find(key);
                if (it != vertex_lookup.end()) {
                    return it->second;
                }

                MeshVertex v{};
                v.position[0] =
                    static_cast<float>(ix - center) * cell_size;
                v.position[1] = 0.0f;
                v.position[2] =
                    static_cast<float>(iz - center) * cell_size;
                // Lattice lies flat in XZ; the upward normal is valid storage.
                // Step 3 recomputes shading normals after height displacement.
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

            // Emit one axis-aligned quad as two triangles. Winding is CCW when
            // viewed from +Y (the lattice's outward face), matching the upward
            // normal stored on each vertex. With x1 > x0 and z1 > z0, the order
            // (v00, v10, v11) / (v00, v11, v01) gives a positive XZ signed area.
            void add_quad(int64_t x0, int64_t z0, int64_t x1, int64_t z1)
            {
                const uint32_t v00 = vertex_at(x0, z0);
                const uint32_t v10 = vertex_at(x1, z0);
                const uint32_t v11 = vertex_at(x1, z1);
                const uint32_t v01 = vertex_at(x0, z1);
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
            int64_t step)
        {
            for (int64_t cz = 0; cz < cells; ++cz) {
                for (int64_t cx = 0; cx < cells; ++cx) {
                    const int64_t ax = x0 + cx * step;
                    const int64_t az = z0 + cz * step;
                    b.add_quad(ax, az, ax + step, az + step);
                }
            }
        }

        // Triangulate a single coarse boundary cell whose hole-facing edge must
        // be split at the finer level's midpoint vertex (half_step away), so the
        // finer boundary vertex is shared and connected — no T-junction. The
        // cell spans [bx0, bx0+step] x [bz0, bz0+step]; `inward` names which of
        // the cell's four edges faces the hole. The outer corner away from the
        // hole anchors a fan that includes the split midpoint.
        enum class InwardEdge { NegX, PosX, NegZ, PosZ };

        void add_inner_boundary_cell(
            LatticeBuilder& b,
            int64_t bx0,
            int64_t bz0,
            int64_t step,
            InwardEdge inward)
        {
            const int64_t half = step / 2;
            const int64_t bx1 = bx0 + step;
            const int64_t bz1 = bz0 + step;

            // Corner vertices of the coarse cell.
            const uint32_t c00 = b.vertex_at(bx0, bz0);
            const uint32_t c10 = b.vertex_at(bx1, bz0);
            const uint32_t c11 = b.vertex_at(bx1, bz1);
            const uint32_t c01 = b.vertex_at(bx0, bz1);

            // In every case the split (hole-facing) edge is fanned from the
            // single corner opposite that edge, so no fan triangle has all three
            // vertices on the split edge (which would be degenerate) and the
            // finer level's midpoint vertex is referenced and connected — the
            // T-junction is eliminated. Winding is CCW from +Y throughout,
            // matching add_quad and the upward vertex normals.
            switch (inward) {
            case InwardEdge::NegX: {
                // Hole-facing edge is the left edge c00->c01 (x = bx0).
                const uint32_t mid = b.vertex_at(bx0, bz0 + half);
                b.add_triangle(c10, c11, c01);
                b.add_triangle(c10, c01, mid);
                b.add_triangle(c10, mid, c00);
                break;
            }
            case InwardEdge::PosX: {
                // Hole-facing edge is the right edge c10->c11 (x = bx1).
                const uint32_t mid = b.vertex_at(bx1, bz0 + half);
                b.add_triangle(c00, c10, mid);
                b.add_triangle(c00, mid, c11);
                b.add_triangle(c00, c11, c01);
                break;
            }
            case InwardEdge::NegZ: {
                // Hole-facing edge is the bottom edge c00->c10 (z = bz0).
                const uint32_t mid = b.vertex_at(bx0 + half, bz0);
                b.add_triangle(c01, c00, mid);
                b.add_triangle(c01, mid, c10);
                b.add_triangle(c01, c10, c11);
                break;
            }
            case InwardEdge::PosZ: {
                // Hole-facing edge is the top edge c01->c11 (z = bz1).
                const uint32_t mid = b.vertex_at(bx0 + half, bz1);
                b.add_triangle(c00, c10, c11);
                b.add_triangle(c00, c11, mid);
                b.add_triangle(c00, mid, c01);
                break;
            }
            }
        }

        // Build one clipmap ring at coarse cell size `step` (fine units),
        // occupying coarse cells in [center-outer, center+outer] minus the inner
        // hole [center-inner, center+inner]. The single coarse-cell layer that
        // borders the hole is emitted with crack-free split cells so it stitches
        // to the finer level whose outer boundary sits at half_step spacing.
        void fill_ring(
            LatticeBuilder& b,
            int64_t step,
            int64_t outer,
            int64_t inner)
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

                    // Is this cell on the inner (hole-facing) boundary layer,
                    // and if so, which edge faces the hole? A cell can face the
                    // hole on exactly one side along each ring edge; ring corners
                    // are handled as plain quads (their hole-facing corner vertex
                    // is the finer level's corner, already shared).
                    const bool touch_neg_x = (x == hi_in) && (z >= lo_in && z < hi_in);
                    const bool touch_pos_x = (x + step == lo_in) && (z >= lo_in && z < hi_in);
                    const bool touch_neg_z = (z == hi_in) && (x >= lo_in && x < hi_in);
                    const bool touch_pos_z = (z + step == lo_in) && (x >= lo_in && x < hi_in);

                    if (touch_neg_x) {
                        add_inner_boundary_cell(b, x, z, step, InwardEdge::NegX);
                    } else if (touch_pos_x) {
                        add_inner_boundary_cell(b, x, z, step, InwardEdge::PosX);
                    } else if (touch_neg_z) {
                        add_inner_boundary_cell(b, x, z, step, InwardEdge::NegZ);
                    } else if (touch_pos_z) {
                        add_inner_boundary_cell(b, x, z, step, InwardEdge::PosZ);
                    } else {
                        b.add_quad(x, z, x + step, z + step);
                    }
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

        // Round the base resolution up to a multiple of 4. The clipmap rings
        // nest crack-free only when every level's outer extent lands on the
        // next-coarser ring's cell boundary; with cell sizes doubling per level
        // that requires the ring half-width (m/2) to be even, i.e. m % 4 == 0.
        // A merely-even m whose half-width is odd would bisect the coarse cells
        // bordering the hole and reintroduce T-junctions.
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
        fill_solid_block(b, b.center - m / 2, b.center - m / 2, m, 1);

        // Levels 1..L-1: concentric rings, each at double the previous cell size.
        for (int64_t k = 1; k < levels; ++k) {
            const int64_t step = int64_t{1} << k;
            const int64_t outer = (m / 2) * step;
            const int64_t inner = (m / 2) * (step / 2); // == outer extent of k-1
            fill_ring(b, step, outer, inner);
        }

        return std::move(b.mesh);
    }
}
