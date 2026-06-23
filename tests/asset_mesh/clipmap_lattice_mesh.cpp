#include <gtest/gtest.h>

#include <engine/assets/mesh/clipmap_lattice_mesh.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
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

    // Level 0: 2*m^2 triangles. Each outer ring: 3*m^2/4 cells, of which 2*m
    // border the hole and are split into 3 triangles instead of 2, giving
    // 3*m^2/2 + 2*m triangles per ring.
    const uint32_t level0 = 2u * m * m;
    const uint32_t per_ring = (3u * m * m) / 2u + 2u * m;
    const uint32_t expected_tris = level0 + (L - 1u) * per_ring;

    EXPECT_EQ(mesh.index_count(), expected_tris * 3u);
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

    // Flat lattice in XZ.
    EXPECT_FLOAT_EQ(min_y, 0.0f);
    EXPECT_FLOAT_EQ(max_y, 0.0f);

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

// The core correctness property: the LOD ring boundaries are crack-free. We
// assert this two independent ways.

TEST(ClipmapLatticeMesh, NoTJunctionCracksAcrossLodSeams)
{
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.0f,
    });
    ASSERT_TRUE(mesh.valid());

    // Collect every unique vertex position.
    std::vector<Vec2> positions;
    positions.reserve(mesh.vertex_count());
    for (uint32_t v = 0; v < mesh.vertex_count(); ++v) {
        positions.push_back(vertex_xz(mesh, v));
    }

    // For every triangle edge, no OTHER vertex may sit strictly inside it.
    // A vertex strictly interior to an edge is, by definition, a T-junction
    // crack between a coarse triangle and the finer geometry meeting it.
    for (uint32_t i = 0; i < mesh.index_count(); i += 3u) {
        const std::array<uint32_t, 3> tri{
            mesh.indices[i + 0u],
            mesh.indices[i + 1u],
            mesh.indices[i + 2u],
        };
        for (int e = 0; e < 3; ++e) {
            const Vec2 a = vertex_xz(mesh, tri[e]);
            const Vec2 b = vertex_xz(mesh, tri[(e + 1) % 3]);
            for (uint32_t v = 0; v < positions.size(); ++v) {
                if (v == tri[e] || v == tri[(e + 1) % 3]) {
                    continue;
                }
                EXPECT_FALSE(point_strictly_inside_segment(positions[v], a, b))
                    << "T-junction: vertex " << v << " lies on a triangle edge "
                    << "(crack across an LOD seam)";
            }
        }
    }
}

TEST(ClipmapLatticeMesh, AdjacentLevelBoundaryVerticesAreShared)
{
    // Independent seam check: every boundary position is backed by exactly one
    // shared vertex (no duplicated-but-unmatched boundary vertex that would
    // leave a gap). Build a position -> count map over unique vertices; then
    // verify that each vertex referenced by the index buffer maps to a position
    // that resolves to a single vertex id. Because levels share the finest
    // integer grid, coincident coarse/fine boundary positions collapse to one
    // vertex; a crack would show up as two distinct vertex ids at one position.
    const auto mesh = ea::make_clipmap_lattice_mesh({
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.0f,
    });
    ASSERT_TRUE(mesh.valid());

    std::map<std::pair<int64_t, int64_t>, uint32_t> by_position;
    for (uint32_t v = 0; v < mesh.vertex_count(); ++v) {
        const Vec2 p = vertex_xz(mesh, v);
        const std::pair<int64_t, int64_t> key{ quantize(p.x), quantize(p.z) };
        const auto [it, inserted] = by_position.emplace(key, v);
        EXPECT_TRUE(inserted)
            << "two distinct vertices share position (" << p.x << ", " << p.z
            << ") -- an unmerged seam vertex that can crack";
        (void)it;
    }
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
