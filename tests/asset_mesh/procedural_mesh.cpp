#include <gtest/gtest.h>

#include <engine/assets/mesh/procedural_mesh.h>

#include <cmath>

TEST(ProceduralMesh, TriangleIsValid)
{
    const auto mesh = wz::engine::assets::make_triangle_mesh();

    EXPECT_TRUE(mesh.valid());
    EXPECT_EQ(mesh.vertex_count(), 3u);
    EXPECT_EQ(mesh.index_count(), 3u);
}

TEST(ProceduralMesh, QuadIsValid)
{
    const auto mesh = wz::engine::assets::make_quad_mesh();

    EXPECT_TRUE(mesh.valid());
    EXPECT_EQ(mesh.vertex_count(), 4u);
    EXPECT_EQ(mesh.index_count(), 6u);
}

TEST(ProceduralMesh, CubeIsValid)
{
    const auto mesh = wz::engine::assets::make_cube_mesh();

    EXPECT_TRUE(mesh.valid());
    EXPECT_EQ(mesh.vertex_count(), 8u);
    EXPECT_EQ(mesh.index_count(), 36u);
}

TEST(ProceduralMesh, SphereIsValid)
{
    for (uint32_t subdivisions = 0u; subdivisions <= 3u; ++subdivisions) {
        const float radius = 2.5f;
        const auto mesh =
            wz::engine::assets::make_sphere_mesh(subdivisions, radius);

        EXPECT_TRUE(mesh.valid());
        EXPECT_GT(mesh.vertex_count(), 0u);
        EXPECT_GT(mesh.index_count(), 0u);

        // Icosphere triangle count: 20 base faces, each split into 4 per level.
        uint32_t expected_triangles = 20u;
        for (uint32_t i = 0u; i < subdivisions; ++i) {
            expected_triangles *= 4u;
        }
        EXPECT_EQ(mesh.index_count(), expected_triangles * 3u);

        // Every index addresses a real vertex.
        for (const uint32_t index : mesh.indices) {
            EXPECT_LT(index, mesh.vertex_count());
        }

        // Every vertex sits on the sphere of the requested radius.
        for (const auto& v : mesh.vertices) {
            const float length = std::sqrt(
                v.position[0] * v.position[0]
                + v.position[1] * v.position[1]
                + v.position[2] * v.position[2]);
            EXPECT_NEAR(length, radius, 1e-4f);
        }
    }
}

TEST(ProceduralMesh, SphereDefaultsAreDeterministic)
{
    const auto a = wz::engine::assets::make_sphere_mesh();
    const auto b = wz::engine::assets::make_sphere_mesh();

    ASSERT_EQ(a.vertex_count(), b.vertex_count());
    ASSERT_EQ(a.index_count(), b.index_count());
    EXPECT_EQ(a.indices, b.indices);
    // Default subdivisions = 2 -> 20 * 4^2 = 320 triangles.
    EXPECT_EQ(a.index_count(), 320u * 3u);
}