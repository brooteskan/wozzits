#include <gtest/gtest.h>

#include <engine/assets/mesh/clipmap_lattice_mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
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
    // level_count floored to 1, resolution rounded up to a multiple of 4, and a
    // non-positive cell size falls back to 1.
    const auto p = ea::sanitize_clipmap_lattice_params(0u, 7u, -3.0f);
    EXPECT_EQ(p.level_count, 1u);
    EXPECT_EQ(p.base_resolution, 8u);
    EXPECT_FLOAT_EQ(p.cell_size, 1.0f);

    // Tiny/odd resolutions clamp up to the minimum valid value (4).
    const auto p2 = ea::sanitize_clipmap_lattice_params(3u, 1u, 2.5f);
    EXPECT_EQ(p2.level_count, 3u);
    EXPECT_EQ(p2.base_resolution, 4u);
    EXPECT_FLOAT_EQ(p2.cell_size, 2.5f);

    // An even resolution whose half-width is odd (m % 4 == 2) rounds up to the
    // next multiple of 4, keeping the seams crack-free.
    const auto p3 = ea::sanitize_clipmap_lattice_params(2u, 6u, 1.0f);
    EXPECT_EQ(p3.base_resolution, 8u);

    // Already a multiple of 4: unchanged.
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

TEST(ClipmapLatticeMesh, MultiLevelTriangleCountMatchesRingFormula)
{
    const uint32_t m = 8u;
    const uint32_t L = 4u;
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = L,
        .base_resolution = m,
        .cell_size = 1.0f,
    });

    ASSERT_TRUE(mesh.valid());

    // Level 0: 2*m^2 triangles. Each outer ring is now PLAIN quads (no crack-fix
    // split cells — #207 closes the seam with the VS vertical morph instead):
    // (outer/step)^2 - (inner/step)^2 = m^2 - (m/2)^2 = 3*m^2/4 cells, each 2
    // triangles -> 3*m^2/2 triangles per ring.
    const uint32_t level0 = 2u * m * m;
    const uint32_t per_ring = (3u * m * m) / 2u;
    const uint32_t expected_tris = level0 + (L - 1u) * per_ring;

    EXPECT_EQ(mesh.index_count(), expected_tris * 3u);
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
