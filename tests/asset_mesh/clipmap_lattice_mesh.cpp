#include <gtest/gtest.h>

#include <engine/assets/mesh/clipmap_lattice_mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;

    struct Vec2
    {
        float x = 0.0f;
        float z = 0.0f;
    };

    Vec2 vertex_xz(const ea::MeshData& mesh, uint32_t index)
    {
        const auto& v = mesh.vertices[index];
        return Vec2{ v.position[0], v.position[2] };
    }

    // Twice the signed area of triangle (a, b, c) in the XZ plane. Non-zero
    // magnitude means the triangle is not degenerate.
    float signed_area2(const Vec2& a, const Vec2& b, const Vec2& c)
    {
        return (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
    }

    // True if point p lies strictly in the interior of segment (a, b): collinear
    // with the segment and strictly between the endpoints (not equal to either).
    // This is exactly a T-junction crack when p is some other triangle's vertex.
    bool point_strictly_inside_segment(
        const Vec2& p,
        const Vec2& a,
        const Vec2& b)
    {
        const float cross = (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
        const float eps = 1e-5f;
        if (std::fabs(cross) > eps) {
            return false; // not collinear
        }
        const float dot = (p.x - a.x) * (b.x - a.x) + (p.z - a.z) * (b.z - a.z);
        const float len2 =
            (b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z);
        if (len2 <= eps) {
            return false;
        }
        // Strictly between: 0 < t < 1, with a margin so shared endpoints (t≈0/1)
        // are excluded.
        const float t = dot / len2;
        return t > eps && t < 1.0f - eps;
    }

    // Snap to a small grid so positions that should coincide compare equal.
    int64_t quantize(float v)
    {
        return static_cast<int64_t>(std::llround(v * 1024.0f));
    }
}

TEST(ClipmapLatticeMesh, SanitizesDegenerateParameters)
{
    // level_count floored to 1 and a non-positive cell size falls back to 1. The
    // base resolution is NO LONGER rounded to a multiple of 4: completeness is a
    // structural invariant of the generator, so any m >= 1 passes through intact.
    const auto p = ea::sanitize_clipmap_lattice_params(0u, 7u, -3.0f);
    EXPECT_EQ(p.level_count, 1u);
    EXPECT_EQ(p.base_resolution, 7u);  // odd, kept as-is
    EXPECT_FLOAT_EQ(p.cell_size, 1.0f);

    // Only a zero resolution is floored up (to 1, a non-empty 1x1 center).
    const auto p0 = ea::sanitize_clipmap_lattice_params(2u, 0u, 1.0f);
    EXPECT_EQ(p0.base_resolution, 1u);

    // Small odd/even resolutions pass through unchanged (no clamp to 4).
    const auto p1 = ea::sanitize_clipmap_lattice_params(3u, 1u, 2.5f);
    EXPECT_EQ(p1.level_count, 3u);
    EXPECT_EQ(p1.base_resolution, 1u);
    EXPECT_FLOAT_EQ(p1.cell_size, 2.5f);

    const auto p2 = ea::sanitize_clipmap_lattice_params(2u, 2u, 1.0f);
    EXPECT_EQ(p2.base_resolution, 2u);

    // A non-multiple-of-4 even resolution is no longer bumped up.
    const auto p3 = ea::sanitize_clipmap_lattice_params(2u, 6u, 1.0f);
    EXPECT_EQ(p3.base_resolution, 6u);

    // Already valid: unchanged.
    const auto p4 = ea::sanitize_clipmap_lattice_params(2u, 12u, 1.0f);
    EXPECT_EQ(p4.base_resolution, 12u);
}

TEST(ClipmapLatticeMesh, SingleLevelIsAFullGrid)
{
    // L = 1 is the degenerate (center-only) case: a full m x m quad grid.
    const uint32_t m = 8u;
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = 1u,
        .base_resolution = m,
        .cell_size = 1.0f,
    });

    ASSERT_TRUE(mesh.valid());
    EXPECT_TRUE(mesh.has_normals);
    EXPECT_TRUE(mesh.has_uv0);

    // A solid m x m grid has (m+1)^2 unique vertices and 2*m^2 triangles.
    EXPECT_EQ(mesh.vertex_count(), (m + 1u) * (m + 1u));
    EXPECT_EQ(mesh.index_count(), 2u * m * m * 3u);
}

// Exact per-level cell/triangle count for the rewritten (gap-free, any-m)
// generator. With h = floor(m/2) and q = floor(h/2) = floor(m/4):
//   * level 0 is a solid m x m grid  -> 2*m^2 triangles.
//   * level k>=1 spans h cells of its own size per side (outer = (2h)^2 cells)
//     minus a centered hole of q cells per side (hole = (2q)^2 cells), all whole
//     cells -> 4*(h^2 - q^2) cells -> 8*(h^2 - q^2) triangles per ring.
// Every ring count is identical, so total = 2*m^2 + (L-1)*8*(h^2 - q^2).
namespace
{
    uint32_t expected_lattice_triangles(uint32_t m, uint32_t L)
    {
        const uint32_t h = m / 2u;
        const uint32_t q = h / 2u;  // == m / 4
        const uint32_t level0 = 2u * m * m;
        const uint32_t per_ring = 8u * (h * h - q * q);
        return level0 + (L - 1u) * per_ring;
    }
}

TEST(ClipmapLatticeMesh, MultiLevelTriangleCountMatchesRingFormula)
{
    // Canonical multiple-of-4 case (unchanged from before the rewrite): for m=8
    // the new formula reduces to the old 2*m^2 + (L-1)*3*m^2/2.
    {
        const uint32_t m = 8u;
        const uint32_t L = 4u;
        const auto mesh = ea::make_clipmap_lattice_mesh({
            .level_count = L,
            .base_resolution = m,
            .cell_size = 1.0f,
        });
        ASSERT_TRUE(mesh.valid());

        const uint32_t old_formula =
            2u * m * m + (L - 1u) * ((3u * m * m) / 2u);
        EXPECT_EQ(expected_lattice_triangles(m, L), old_formula);
        EXPECT_EQ(mesh.index_count(), expected_lattice_triangles(m, L) * 3u);
    }

    // A non-multiple-of-4 resolution (m=5, the headline regression): h=2, q=1,
    // so level 0 = 2*25 = 50 tris and each ring = 8*(4-1) = 24 tris.
    {
        const uint32_t m = 5u;
        const uint32_t L = 3u;
        const auto mesh = ea::make_clipmap_lattice_mesh({
            .level_count = L,
            .base_resolution = m,
            .cell_size = 1.0f,
        });
        ASSERT_TRUE(mesh.valid());

        EXPECT_EQ(expected_lattice_triangles(m, L), 50u + 2u * 24u);
        EXPECT_EQ(mesh.index_count(), expected_lattice_triangles(m, L) * 3u);
    }
}

// Each vertex carries its LOD level in position.y (0 = finest center, k = ring
// k), which the geometry-clipmap VS reads to snap + morph per level (#207).
TEST(ClipmapLatticeMesh, VerticesCarryLodLevelInPositionY)
{
    const uint32_t m = 8u;
    const uint32_t L = 4u;
    const float cell = 1.0f;
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = L,
        .base_resolution = m,
        .cell_size = cell,
    });
    ASSERT_TRUE(mesh.valid());

    // Coarsest cell in fine units is 2^(L-1); the half-extent of the whole
    // lattice. Level 0 occupies the central m/2 fine cells out from the center.
    const float coarsest_step = static_cast<float>(1u << (L - 1u));
    const float half_extent = static_cast<float>(m / 2u) * coarsest_step;
    const float level0_half = static_cast<float>(m / 2u);  // fine cells

    int seen_max_level = 0;
    for (const auto& v : mesh.vertices) {
        const int level = static_cast<int>(std::llround(v.position[1]));
        EXPECT_GE(level, 0);
        EXPECT_LT(level, static_cast<int>(L)) << "level tag out of range";
        seen_max_level = std::max(seen_max_level, level);

        // The Chebyshev radius of a vertex (in fine units / cell) bounds its
        // level: a level-0 vertex sits within the central solid block, so its
        // radius is <= the level-0 half-extent.
        const float rx = std::fabs(v.position[0]) / cell;
        const float rz = std::fabs(v.position[2]) / cell;
        const float cheb = std::max(rx, rz);
        if (level == 0) {
            EXPECT_LE(cheb, level0_half + 1e-3f)
                << "a level-0 vertex sits outside the fine center";
        }
        EXPECT_LE(cheb, half_extent + 1e-3f);
    }

    // Every level 0..L-1 is present (the coarsest ring reaches level L-1).
    EXPECT_EQ(seen_max_level, static_cast<int>(L) - 1);
}

TEST(ClipmapLatticeMesh, AllIndicesInRangeAndNoDegenerateTriangles)
{
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.5f,
    });

    ASSERT_TRUE(mesh.valid());
    ASSERT_EQ(mesh.index_count() % 3u, 0u);

    for (uint32_t i = 0; i < mesh.index_count(); i += 3u) {
        const uint32_t ia = mesh.indices[i + 0u];
        const uint32_t ib = mesh.indices[i + 1u];
        const uint32_t ic = mesh.indices[i + 2u];

        ASSERT_LT(ia, mesh.vertex_count());
        ASSERT_LT(ib, mesh.vertex_count());
        ASSERT_LT(ic, mesh.vertex_count());

        const float area2 = signed_area2(
            vertex_xz(mesh, ia),
            vertex_xz(mesh, ib),
            vertex_xz(mesh, ic));
        EXPECT_GT(std::fabs(area2), 1e-4f)
            << "degenerate (zero-area) triangle at index " << i;
    }
}

TEST(ClipmapLatticeMesh, ConsistentTriangleWinding)
{
    // The lattice is the displacement footprint; a consistent facing matters for
    // back-face culling. All triangles wind the same way in XZ (CCW from +Y).
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.0f,
    });
    ASSERT_TRUE(mesh.valid());

    for (uint32_t i = 0; i < mesh.index_count(); i += 3u) {
        const float area2 = signed_area2(
            vertex_xz(mesh, mesh.indices[i + 0u]),
            vertex_xz(mesh, mesh.indices[i + 1u]),
            vertex_xz(mesh, mesh.indices[i + 2u]));
        EXPECT_GT(area2, 0.0f) << "inconsistent winding at index " << i;
    }
}

TEST(ClipmapLatticeMesh, BoundsAreCenteredAtOrigin)
{
    const float cell = 2.0f;
    const uint32_t m = 8u;
    const uint32_t L = 4u;
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = L,
        .base_resolution = m,
        .cell_size = cell,
    });
    ASSERT_TRUE(mesh.valid());

    float min_x = 0.0f, max_x = 0.0f, min_z = 0.0f, max_z = 0.0f;
    float max_y = 0.0f, min_y = 0.0f;
    for (const auto& v : mesh.vertices) {
        min_x = std::min(min_x, v.position[0]);
        max_x = std::max(max_x, v.position[0]);
        min_z = std::min(min_z, v.position[2]);
        max_z = std::max(max_z, v.position[2]);
        min_y = std::min(min_y, v.position[1]);
        max_y = std::max(max_y, v.position[1]);
    }

    // The lattice is flat in XZ, but position.y now carries the LOD level
    // (#207): the finest center is 0 and the coarsest ring is L-1.
    EXPECT_FLOAT_EQ(min_y, 0.0f);
    EXPECT_FLOAT_EQ(max_y, static_cast<float>(L - 1u));

    // Symmetric about the origin.
    EXPECT_FLOAT_EQ(min_x, -max_x);
    EXPECT_FLOAT_EQ(min_z, -max_z);

    // Outer half-extent is (m/2) * 2^(L-1) * cell.
    const float expected_half =
        static_cast<float>(m / 2u)
        * static_cast<float>(1u << (L - 1u))
        * cell;
    EXPECT_FLOAT_EQ(max_x, expected_half);
    EXPECT_FLOAT_EQ(max_z, expected_half);
}

// The core correctness property changed with #207: each level is its own clean
// uniform grid (so no T-junction WITHIN a level), and seams are closed by the
// VS vertical morph — NOT by sharing boundary vertices across levels. We assert
// both the within-level cleanliness and the deliberate per-level un-sharing.

TEST(ClipmapLatticeMesh, NoTJunctionWithinAnyLevel)
{
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.0f,
    });
    ASSERT_TRUE(mesh.valid());

    // Group vertices by their level tag (every triangle's three vertices share
    // one level, since the generator emits each level independently).
    auto level_of = [&](uint32_t v) {
        return static_cast<int>(std::llround(mesh.vertices[v].position[1]));
    };

    // For every triangle edge, no other vertex OF THE SAME LEVEL may sit
    // strictly inside it. (Cross-level coincidences on a boundary are expected
    // and intentional — the morph handles them — so they are excluded.)
    for (uint32_t i = 0; i < mesh.index_count(); i += 3u) {
        const std::array<uint32_t, 3> tri{
            mesh.indices[i + 0u],
            mesh.indices[i + 1u],
            mesh.indices[i + 2u],
        };
        const int tri_level = level_of(tri[0]);
        for (int e = 0; e < 3; ++e) {
            const Vec2 a = vertex_xz(mesh, tri[e]);
            const Vec2 b = vertex_xz(mesh, tri[(e + 1) % 3]);
            for (uint32_t v = 0; v < mesh.vertex_count(); ++v) {
                if (v == tri[e] || v == tri[(e + 1) % 3]) {
                    continue;
                }
                if (level_of(v) != tri_level) {
                    continue;  // cross-level coincidence: closed by the morph
                }
                EXPECT_FALSE(
                    point_strictly_inside_segment(vertex_xz(mesh, v), a, b))
                    << "T-junction WITHIN level " << tri_level
                    << ": vertex " << v << " lies on a same-level edge";
            }
        }
    }
}

TEST(ClipmapLatticeMesh, AdjacentLevelBoundaryVerticesAreUnshared)
{
    // #207 requires each level to OWN its vertices so the VS can snap each level
    // independently. A position on the boundary between two adjacent levels must
    // therefore be backed by TWO distinct vertices (one per level), not one
    // shared vertex. Verify at least one position carries multiple vertices and
    // that every such multiply-owned position has distinct level tags.
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.0f,
    });
    ASSERT_TRUE(mesh.valid());

    std::map<std::pair<int64_t, int64_t>, std::vector<uint32_t>> by_position;
    for (uint32_t v = 0; v < mesh.vertex_count(); ++v) {
        const Vec2 p = vertex_xz(mesh, v);
        by_position[{ quantize(p.x), quantize(p.z) }].push_back(v);
    }

    size_t duplicated_positions = 0;
    for (const auto& [pos, verts] : by_position) {
        (void)pos;
        if (verts.size() <= 1) {
            continue;
        }
        ++duplicated_positions;
        // Each vertex at a shared position belongs to a DIFFERENT level (a level
        // never duplicates its own vertices: its dedup is per (ix,iz,level)).
        std::vector<int> levels;
        for (const uint32_t v : verts) {
            levels.push_back(
                static_cast<int>(std::llround(mesh.vertices[v].position[1])));
        }
        std::sort(levels.begin(), levels.end());
        const auto last = std::unique(levels.begin(), levels.end());
        EXPECT_EQ(last, levels.end())
            << "a single level duplicated a vertex at one position";
    }

    EXPECT_GT(duplicated_positions, 0u)
        << "expected adjacent levels to own separate boundary vertices";
}

TEST(ClipmapLatticeMesh, IsDeterministic)
{
    const ea::ClipmapLatticeParams params{
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.25f,
    };

    const auto a = ea::make_clipmap_lattice_mesh(params);
    const auto b = ea::make_clipmap_lattice_mesh(params);

    ASSERT_EQ(a.vertex_count(), b.vertex_count());
    ASSERT_EQ(a.index_count(), b.index_count());

    for (uint32_t v = 0; v < a.vertex_count(); ++v) {
        EXPECT_FLOAT_EQ(a.vertices[v].position[0], b.vertices[v].position[0]);
        EXPECT_FLOAT_EQ(a.vertices[v].position[1], b.vertices[v].position[1]);
        EXPECT_FLOAT_EQ(a.vertices[v].position[2], b.vertices[v].position[2]);
    }
    EXPECT_EQ(a.indices, b.indices);
}

namespace
{
    // An axis-aligned cell reconstructed from a triangle: its XZ bounding box is
    // exactly the parent quad (both triangles of a quad span the full quad bbox),
    // and every quad is a square, so x1-x0 == z1-z0 == the level's step.
    struct Cell
    {
        int64_t x0 = 0;
        int64_t z0 = 0;
        int64_t step = 0;
        int level = 0;

        bool operator<(const Cell& o) const
        {
            if (x0 != o.x0) return x0 < o.x0;
            if (z0 != o.z0) return z0 < o.z0;
            if (step != o.step) return step < o.step;
            return level < o.level;
        }
    };

    // Per-side cell counts of level k's full grid and its centered hole, mirrored
    // from the generator: outer side = 2*h, hole side = 2*floor(h/2), with
    // h = floor(m/2). Level 0 is a solid m x m grid (no hole).
    uint32_t expected_level_cell_count(uint32_t m, uint32_t level)
    {
        if (level == 0u) {
            return m * m;
        }
        const uint32_t h = m / 2u;
        const uint32_t q = h / 2u;
        const uint32_t outer_side = 2u * h;
        const uint32_t hole_side = 2u * q;
        return outer_side * outer_side - hole_side * hole_side;
    }
}

// HEADLINE gap-free regression. Sweeps a range of resolutions (including the
// non-multiple-of-4 m=5 that the old generator could not tile) and level counts
// and PROVES the lattice is watertight:
//   (1) no zero-area triangle anywhere;
//   (2) within each level, no two cells overlap, and the emitted cell count
//       equals the analytic annulus count (so count == covered area: nothing is
//       missing or double-covered WITHIN a level);
//   (3) every finest-grid cell inside the outer bounding box is covered by at
//       least one quad of some level -> NO GAPS across the whole footprint
//       (cross-level overlaps at the seams are allowed; gaps are not).
// (3) is a topological coverage proof independent of the ring formula, so it
// genuinely demonstrates completeness rather than merely "does not crash".
TEST(ClipmapLatticeMesh, FullyTilesWithNoGapsForAnyResolution)
{
    const uint32_t resolutions[] = { 1u, 2u, 3u, 4u, 5u, 7u, 8u, 16u, 33u };
    const uint32_t level_counts[] = { 1u, 2u, 3u, 4u, 5u };

    for (const uint32_t m : resolutions) {
        for (const uint32_t L : level_counts) {
            SCOPED_TRACE(
                "base_resolution=" + std::to_string(m)
                + " level_count=" + std::to_string(L));

            // cell_size == 1 so positions are exact integers (fine units): the
            // coverage rasterization below is then exact, with no float slop.
            const auto mesh = ea::make_clipmap_lattice_mesh({
                .level_count = L,
                .base_resolution = m,
                .cell_size = 1.0f,
            });
            ASSERT_TRUE(mesh.valid());
            ASSERT_EQ(mesh.index_count() % 3u, 0u);

            // Reconstruct the set of cells per level from the triangles, and
            // tally how many triangles fall on each cell (must be exactly 2).
            std::map<Cell, int> cell_tri_count;
            std::set<int> levels_seen;

            for (uint32_t i = 0; i < mesh.index_count(); i += 3u) {
                const uint32_t ia = mesh.indices[i + 0u];
                const uint32_t ib = mesh.indices[i + 1u];
                const uint32_t ic = mesh.indices[i + 2u];
                ASSERT_LT(ia, mesh.vertex_count());
                ASSERT_LT(ib, mesh.vertex_count());
                ASSERT_LT(ic, mesh.vertex_count());

                const Vec2 a = vertex_xz(mesh, ia);
                const Vec2 b = vertex_xz(mesh, ib);
                const Vec2 c = vertex_xz(mesh, ic);

                // (1) no degenerate triangle.
                EXPECT_GT(std::fabs(signed_area2(a, b, c)), 1e-4f)
                    << "degenerate triangle at index " << i;

                // All three vertices of a triangle share one level.
                const int la =
                    static_cast<int>(std::llround(mesh.vertices[ia].position[1]));
                const int lb =
                    static_cast<int>(std::llround(mesh.vertices[ib].position[1]));
                const int lc =
                    static_cast<int>(std::llround(mesh.vertices[ic].position[1]));
                ASSERT_EQ(la, lb);
                ASSERT_EQ(la, lc);
                levels_seen.insert(la);

                const int64_t x0 = quantize(std::min({ a.x, b.x, c.x }));
                const int64_t x1 = quantize(std::max({ a.x, b.x, c.x }));
                const int64_t z0 = quantize(std::min({ a.z, b.z, c.z }));
                const int64_t z1 = quantize(std::max({ a.z, b.z, c.z }));
                // Square, axis-aligned, non-degenerate cell.
                ASSERT_GT(x1, x0);
                ASSERT_EQ(x1 - x0, z1 - z0);
                cell_tri_count[Cell{ x0, z0, x1 - x0, la }] += 1;
            }

            // Exactly the levels with a non-empty annulus are present. Every
            // ring level is non-empty when h = floor(m/2) >= 1 (i.e. m >= 2);
            // the m == 1 lattice has zero ring extent (half_extent == 0, which
            // also keeps it consistent with clipmap_lattice_grid_extent), so it
            // legitimately collapses to the single level-0 cell.
            std::set<int> expected_levels;
            for (uint32_t level = 0u; level < L; ++level) {
                if (expected_level_cell_count(m, level) > 0u) {
                    expected_levels.insert(static_cast<int>(level));
                }
            }
            EXPECT_EQ(levels_seen, expected_levels);

            // (2) Each reconstructed cell carries exactly two triangles (a clean
            // quad), and the per-level cell totals match the analytic annulus
            // count -> within a level nothing overlaps and nothing is missing.
            std::map<int, uint32_t> cells_per_level;
            for (const auto& [cell, tris] : cell_tri_count) {
                EXPECT_EQ(tris, 2)
                    << "level " << cell.level << " cell (" << cell.x0 << ","
                    << cell.z0 << ") step " << cell.step
                    << " is not a clean 2-triangle quad";
                cells_per_level[cell.level] += 1u;
            }
            for (uint32_t level = 0u; level < L; ++level) {
                EXPECT_EQ(
                    cells_per_level[static_cast<int>(level)],
                    expected_level_cell_count(m, level))
                    << "level " << level << " annulus cell count mismatch";
            }

            // (3) Watertight coverage proof. Rasterize every quad onto the finest
            // (1x1) integer grid and assert the whole outer bounding box is
            // covered. Positions are in 1/1024 units (quantize scale) and every
            // coordinate is an integer multiple of 1024 here (cell_size == 1), so
            // divide back to whole fine cells.
            constexpr int64_t kUnit = 1024;  // quantize() scale
            int64_t min_x = 0, max_x = 0, min_z = 0, max_z = 0;
            bool first = true;
            for (const auto& [cell, tris] : cell_tri_count) {
                (void)tris;
                if (first) {
                    min_x = cell.x0;
                    max_x = cell.x0 + cell.step;
                    min_z = cell.z0;
                    max_z = cell.z0 + cell.step;
                    first = false;
                } else {
                    min_x = std::min(min_x, cell.x0);
                    max_x = std::max(max_x, cell.x0 + cell.step);
                    min_z = std::min(min_z, cell.z0);
                    max_z = std::max(max_z, cell.z0 + cell.step);
                }
            }
            ASSERT_FALSE(first);

            const int64_t span_x = (max_x - min_x) / kUnit;
            const int64_t span_z = (max_z - min_z) / kUnit;
            ASSERT_GT(span_x, 0);
            ASSERT_GT(span_z, 0);

            // covered[ (i - min) , (j - min) ] over fine cells [i,i+1)x[j,j+1).
            std::vector<uint8_t> covered(
                static_cast<size_t>(span_x) * static_cast<size_t>(span_z), 0u);

            for (const auto& [cell, tris] : cell_tri_count) {
                (void)tris;
                const int64_t cx0 = (cell.x0 - min_x) / kUnit;
                const int64_t cz0 = (cell.z0 - min_z) / kUnit;
                const int64_t cells = cell.step / kUnit;  // step in fine cells
                for (int64_t dz = 0; dz < cells; ++dz) {
                    for (int64_t dx = 0; dx < cells; ++dx) {
                        const int64_t fx = cx0 + dx;
                        const int64_t fz = cz0 + dz;
                        covered[static_cast<size_t>(fz) *
                                    static_cast<size_t>(span_x) +
                                static_cast<size_t>(fx)] = 1u;
                    }
                }
            }

            size_t uncovered = 0;
            for (const uint8_t flag : covered) {
                if (flag == 0u) {
                    ++uncovered;
                }
            }
            EXPECT_EQ(uncovered, 0u)
                << uncovered << " finest-grid cell(s) of the "
                << span_x << "x" << span_z
                << " footprint are not covered by any quad (gap)";
        }
    }
}

// ---------------------------------------------------------------------------
// resolve_clipmap_lattice: authored/physical params -> geometric lattice.
// ---------------------------------------------------------------------------
namespace
{
    // Integer-floor reach, matching BOTH the generator (h = m / 2) and the
    // resolver: reach(m, L, c) = floor(m/2) * 2^(L-1) * c. This is the true outer
    // extent of the produced mesh, so a resolved (even) m reaches exactly here.
    double reach_metres(uint32_t m, uint32_t L, double c)
    {
        return static_cast<double>(m / 2u)
            * std::ldexp(1.0, static_cast<int>(L) - 1)
            * c;
    }
}

// The anchored worked example: R = 2000 m, T = 50000 tris, s = 1.0 m/texel
// resolves to L = 7, m = 64, cell = 1.0. m is the minimal EVEN resolution whose
// floored half-extent reaches R: floor(64/2)*2^6 = 32*64 = 2048 m (>= 2000), at
// exact tris = 45056 (<= 50000). L = 6 would need m = 126 at 152072 tris (over
// budget), so 7 is the smallest fitting L.
TEST(ResolveClipmapLattice, AnchoredWorkedExample)
{
    const auto r = ea::resolve_clipmap_lattice(2000.0f, 50000u, 1.0f);

    EXPECT_EQ(r.params.level_count, 7u);
    EXPECT_EQ(r.params.base_resolution, 64u);
    EXPECT_FLOAT_EQ(r.params.cell_size, 1.0f);

    // The reported reach is the generator's real extent (floor(m/2)*2^(L-1)*c),
    // so it meets -- never overstates -- the requested horizon.
    EXPECT_FLOAT_EQ(r.achieved_horizon_metres, 2048.0f);
    EXPECT_GE(r.achieved_horizon_metres, 2000.0f);

    EXPECT_EQ(r.triangle_count, 45056u);
    EXPECT_LE(r.triangle_count, 50000u);
    EXPECT_EQ(r.triangle_count,
        static_cast<uint64_t>(expected_lattice_triangles(64u, 7u)));

    // L = 6 is rejected because it is over budget: m = 2*ceil(2000/32) = 126,
    // tris = 152072 > 50000. Confirms 7 is the SMALLEST fitting L.
    EXPECT_EQ(expected_lattice_triangles(126u, 6u), 152072u);
    EXPECT_GT(expected_lattice_triangles(126u, 6u), 50000u);
}

// cell_size always equals metres_per_texel: the resolved finest cell IS one
// height-field texel, for a spread of authored values.
TEST(ResolveClipmapLattice, FinestCellEqualsMetresPerTexel)
{
    const float texel_sizes[] = { 0.25f, 0.5f, 1.0f, 2.0f, 7.5f, 100.0f };
    for (const float s : texel_sizes) {
        const auto r = ea::resolve_clipmap_lattice(1500.0f, 80000u, s);
        EXPECT_FLOAT_EQ(r.params.cell_size, s)
            << "cell_size must equal metres_per_texel s = " << s;
    }
}

// Horizon coverage: across a sweep of (R, T, s), whenever a config fit the
// budget the chosen lattice must reach the requested horizon (reach >= R) AND
// stay within budget. The triangle_count is independently cross-checked against
// the test's own exact formula.
TEST(ResolveClipmapLattice, CoversHorizonAndRespectsBudgetWhenItFits)
{
    const float horizons[] = { 50.0f, 250.0f, 1000.0f, 2000.0f, 8000.0f };
    const uint64_t budgets[] = { 20000u, 50000u, 200000u, 1000000u };
    const float texel_sizes[] = { 0.5f, 1.0f, 4.0f };

    for (const float R : horizons) {
        for (const uint64_t T : budgets) {
            for (const float s : texel_sizes) {
                SCOPED_TRACE(
                    "R=" + std::to_string(R)
                    + " T=" + std::to_string(T)
                    + " s=" + std::to_string(s));
                const auto r = ea::resolve_clipmap_lattice(R, T, s);

                // triangle_count agrees with the test's independent formula.
                EXPECT_EQ(
                    r.triangle_count,
                    static_cast<uint64_t>(expected_lattice_triangles(
                        r.params.base_resolution, r.params.level_count)));

                // achieved_horizon_metres agrees with the reach formula.
                const double reach = reach_metres(
                    r.params.base_resolution, r.params.level_count,
                    static_cast<double>(s));
                EXPECT_NEAR(
                    static_cast<double>(r.achieved_horizon_metres), reach,
                    reach * 1e-5 + 1e-3);

                // Coverage is guaranteed for EVERY result (fallback included):
                // the chosen config always reaches at least the requested
                // horizon -- the resolver never overstates its reach.
                EXPECT_GE(
                    static_cast<double>(r.achieved_horizon_metres),
                    static_cast<double>(R) - 1e-3)
                    << "the resolved lattice fell short of the horizon";
            }
        }
    }
}

// A budget too small for ANY config must fall back gracefully: no crash, a
// usable lattice that STILL covers the horizon, and -- because triangle count is
// U-shaped in L -- the CHEAPEST (minimum-triangle) covering config rather than
// merely the largest L. The budget is a ceiling, not a guarantee, so the count
// exceeds it on fallback.
TEST(ResolveClipmapLattice, TinyBudgetFallsBackGracefully)
{
    const float R = 5000.0f;
    const float s = 1.0f;
    const auto r = ea::resolve_clipmap_lattice(R, 1u, s);  // T = 1: nothing fits

    EXPECT_GE(r.params.base_resolution, 2u);          // minimal even resolution
    EXPECT_FLOAT_EQ(r.params.cell_size, s);
    EXPECT_GE(static_cast<double>(r.achieved_horizon_metres),
              static_cast<double>(R) - 1e-3)          // still covers the horizon
        << "fallback dropped below the horizon";
    EXPECT_GT(r.triangle_count, 1u);                  // exceeds the tiny budget
    EXPECT_EQ(r.triangle_count,
        static_cast<uint64_t>(expected_lattice_triangles(
            r.params.base_resolution, r.params.level_count)));

    // It is the MINIMUM-triangle covering config: no level count is cheaper.
    // (Independently mirror resolution_for and scan the whole L range.)
    for (uint32_t L = 1u; L <= 24u; ++L) {
        const double coarsest = std::ldexp(1.0, static_cast<int>(L) - 1);
        const double half = std::ceil(static_cast<double>(R) / (coarsest * s));
        const uint32_t m = 2u * static_cast<uint32_t>(half >= 1.0 ? half : 1.0);
        EXPECT_LE(r.triangle_count,
            static_cast<uint64_t>(expected_lattice_triangles(m, L)))
            << "a cheaper covering config exists at L = " << L;
    }

    // T = 0 is the same fallback path: still graceful and horizon-covering.
    const auto r0 = ea::resolve_clipmap_lattice(100.0f, 0u, s);
    EXPECT_GE(r0.params.base_resolution, 2u);
    EXPECT_GE(static_cast<double>(r0.achieved_horizon_metres), 100.0 - 1e-3);
}

// Monotonicity in the triangle budget: with R and s fixed, a larger budget buys
// MORE (or equal) near-field detail, i.e. base_resolution is non-decreasing in
// the budget. (Smaller L -> larger m, and a bigger budget can only admit an L
// that is <= the smaller budget's L.)
TEST(ResolveClipmapLattice, LargerBudgetGivesAtLeastAsMuchDetail)
{
    const uint64_t budgets[] = {
        5000u, 20000u, 50000u, 100000u, 400000u, 2000000u };
    const float R = 3000.0f;
    const float s = 1.0f;

    uint32_t prev_m = 0u;
    for (const uint64_t T : budgets) {
        const auto r = ea::resolve_clipmap_lattice(R, T, s);
        EXPECT_GE(r.params.base_resolution, prev_m)
            << "a larger budget reduced near-field detail (T = " << T << ")";
        prev_m = r.params.base_resolution;
    }
}

// Monotonicity in the horizon: with T and s fixed, a larger horizon needs at
// least as many levels (reach grows with L, so covering farther forces L up or
// equal). level_count is non-decreasing in R.
TEST(ResolveClipmapLattice, LargerHorizonNeedsAtLeastAsManyLevels)
{
    const float horizons[] = {
        100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 16000.0f };
    const uint64_t T = 60000u;
    const float s = 1.0f;

    uint32_t prev_L = 0u;
    for (const float R : horizons) {
        const auto r = ea::resolve_clipmap_lattice(R, T, s);
        EXPECT_GE(r.params.level_count, prev_L)
            << "a larger horizon used fewer levels (R = " << R << ")";
        prev_L = r.params.level_count;
    }
}

// Texel-size scaling: coarser texels (larger s) shrink the horizon measured in
// CELLS, so the lattice needs the same or fewer levels for a fixed world
// horizon. Doubling s repeatedly must never increase level_count.
TEST(ResolveClipmapLattice, CoarserTexelsUseSameOrFewerLevels)
{
    const float R = 4000.0f;
    const uint64_t T = 80000u;

    uint32_t prev_L = 0xFFFFFFFFu;
    for (float s = 0.25f; s <= 64.0f; s *= 2.0f) {
        SCOPED_TRACE("s=" + std::to_string(s));
        const auto r = ea::resolve_clipmap_lattice(R, T, s);
        EXPECT_LE(r.params.level_count, prev_L)
            << "coarser texels (larger s) increased the level count";
        EXPECT_FLOAT_EQ(r.params.cell_size, s);
        prev_L = r.params.level_count;
    }
}

// Determinism: identical inputs yield byte-identical results.
TEST(ResolveClipmapLattice, IsDeterministic)
{
    const auto a = ea::resolve_clipmap_lattice(1234.0f, 77777u, 1.5f);
    const auto b = ea::resolve_clipmap_lattice(1234.0f, 77777u, 1.5f);

    EXPECT_EQ(a.params.level_count, b.params.level_count);
    EXPECT_EQ(a.params.base_resolution, b.params.base_resolution);
    EXPECT_FLOAT_EQ(a.params.cell_size, b.params.cell_size);
    EXPECT_FLOAT_EQ(a.achieved_horizon_metres, b.achieved_horizon_metres);
    EXPECT_EQ(a.triangle_count, b.triangle_count);
}

// Degenerate inputs are handled per the documented guards: s <= 0 / non-finite
// becomes 1.0, R <= 0 / non-finite collapses to a minimal positive horizon
// (m clamps to the minimal even 2 -> the cheapest 2x2 fine block at L = 1).
TEST(ResolveClipmapLattice, DegenerateInputsHandledPerGuards)
{
    // s <= 0 falls back to a 1.0 finest cell.
    {
        const auto r = ea::resolve_clipmap_lattice(1000.0f, 50000u, -2.0f);
        EXPECT_FLOAT_EQ(r.params.cell_size, 1.0f);
        EXPECT_GE(r.params.base_resolution, 1u);
        EXPECT_EQ(r.triangle_count,
            static_cast<uint64_t>(expected_lattice_triangles(
                r.params.base_resolution, r.params.level_count)));
    }
    {
        const auto r = ea::resolve_clipmap_lattice(
            1000.0f, 50000u, 0.0f);
        EXPECT_FLOAT_EQ(r.params.cell_size, 1.0f);
    }
    // Non-finite s -> 1.0 as well.
    {
        const auto r = ea::resolve_clipmap_lattice(
            1000.0f, 50000u,
            std::numeric_limits<float>::quiet_NaN());
        EXPECT_FLOAT_EQ(r.params.cell_size, 1.0f);
    }

    // R <= 0 collapses to a minimal horizon: m clamps to the minimal even value
    // (2) at every L, so the smallest fitting L (1) wins -> a 2x2 fine block
    // (2*2^2 = 8 tris).
    {
        const auto r = ea::resolve_clipmap_lattice(-50.0f, 50000u, 1.0f);
        EXPECT_EQ(r.params.level_count, 1u);
        EXPECT_EQ(r.params.base_resolution, 2u);
        EXPECT_EQ(r.triangle_count, 8u);  // 2*2^2, no rings at L = 1
    }
    {
        const auto r = ea::resolve_clipmap_lattice(0.0f, 50000u, 1.0f);
        EXPECT_EQ(r.params.base_resolution, 2u);
    }
    // Non-finite R behaves like the minimal-horizon guard, not a huge lattice.
    {
        const auto r = ea::resolve_clipmap_lattice(
            std::numeric_limits<float>::infinity(), 50000u, 1.0f);
        EXPECT_EQ(r.params.base_resolution, 2u);
        EXPECT_EQ(r.params.level_count, 1u);
    }
}

// Cross-check against the generator: a resolved ClipmapLatticeParams fed to
// make_clipmap_lattice_mesh must yield a valid mesh whose triangle count equals
// the resolver's reported triangle_count (so the resolver's math and the
// generator's emitted geometry agree end-to-end).
TEST(ResolveClipmapLattice, ResolvedParamsDriveTheGenerator)
{
    struct Case { float R; uint64_t T; float s; };
    const Case cases[] = {
        { 2000.0f, 50000u, 1.0f },   // the anchored example
        { 500.0f, 20000u, 0.5f },
        { 8000.0f, 1000000u, 2.0f },
        { 120.0f, 5000u, 1.0f },
        { 3000.0f, 1u, 1.0f },       // tiny-budget fallback
    };

    for (const Case& cs : cases) {
        SCOPED_TRACE(
            "R=" + std::to_string(cs.R)
            + " T=" + std::to_string(cs.T)
            + " s=" + std::to_string(cs.s));
        const auto r = ea::resolve_clipmap_lattice(cs.R, cs.T, cs.s);

        const auto mesh = ea::make_clipmap_lattice_mesh(r.params);
        ASSERT_TRUE(mesh.valid());
        ASSERT_EQ(mesh.index_count() % 3u, 0u);

        // The generator's actual triangle count == the resolver's prediction.
        EXPECT_EQ(
            static_cast<uint64_t>(mesh.index_count() / 3u),
            r.triangle_count);
    }
}
