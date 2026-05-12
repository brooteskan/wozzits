#include <gtest/gtest.h>

#include <engine/assets/mesh/procedural_mesh.h>

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