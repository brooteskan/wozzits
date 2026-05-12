#include <gtest/gtest.h>

#include <asset/types.h>
#include <engine/assets/key_factories/mesh.h>
#include <engine/assets/schema_ids.h>

TEST(MeshKeyFactory, ProceduralTriangleKeyIsDeterministic)
{
    const auto a = wz::engine::assets::make_procedural_triangle_mesh_key();
    const auto b = wz::engine::assets::make_procedural_triangle_mesh_key();

    EXPECT_EQ(a, b);
    EXPECT_NE(a, wz::asset::AssetKey{});
}

TEST(MeshKeyFactory, ProceduralQuadKeyIsDeterministic)
{
    const auto a = wz::engine::assets::make_procedural_quad_mesh_key();
    const auto b = wz::engine::assets::make_procedural_quad_mesh_key();

    EXPECT_EQ(a, b);
    EXPECT_NE(a, wz::asset::AssetKey{});
}

TEST(MeshKeyFactory, ProceduralCubeKeyIsDeterministic)
{
    const auto a = wz::engine::assets::make_procedural_cube_mesh_key();
    const auto b = wz::engine::assets::make_procedural_cube_mesh_key();

    EXPECT_EQ(a, b);
    EXPECT_NE(a, wz::asset::AssetKey{});
}

TEST(MeshKeyFactory, ProceduralMeshKindsHaveDistinctKeys)
{
    const auto triangle = wz::engine::assets::make_procedural_triangle_mesh_key();
    const auto quad = wz::engine::assets::make_procedural_quad_mesh_key();
    const auto cube = wz::engine::assets::make_procedural_cube_mesh_key();

    EXPECT_NE(triangle, quad);
    EXPECT_NE(triangle, cube);
    EXPECT_NE(quad, cube);
}

TEST(MeshKeyFactory, ProceduralMeshKeysUseExpectedSchemas)
{
    const auto triangle = wz::engine::assets::make_procedural_triangle_mesh_key();
    const auto quad = wz::engine::assets::make_procedural_quad_mesh_key();
    const auto cube = wz::engine::assets::make_procedural_cube_mesh_key();

    EXPECT_EQ(
        triangle.schema_hash,
        wz::engine::assets::detail::hash_u64(
            wz::engine::assets::kProceduralTriangleMeshSchema.value));

    EXPECT_EQ(
        quad.schema_hash,
        wz::engine::assets::detail::hash_u64(
            wz::engine::assets::kProceduralQuadMeshSchema.value));

    EXPECT_EQ(
        cube.schema_hash,
        wz::engine::assets::detail::hash_u64(
            wz::engine::assets::kProceduralCubeMeshSchema.value));
}