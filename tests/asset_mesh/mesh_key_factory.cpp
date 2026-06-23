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

TEST(MeshKeyFactory, PlaceholderMeshKeyIsDeterministic)
{
    const auto a = wz::engine::assets::make_placeholder_mesh_key();
    const auto b = wz::engine::assets::make_placeholder_mesh_key();

    EXPECT_EQ(a, b);
    EXPECT_NE(a, wz::asset::AssetKey{});
}

TEST(MeshKeyFactory, ProceduralMeshKindsHaveDistinctKeys)
{
    const auto triangle = wz::engine::assets::make_procedural_triangle_mesh_key();
    const auto quad = wz::engine::assets::make_procedural_quad_mesh_key();
    const auto cube = wz::engine::assets::make_procedural_cube_mesh_key();
    const auto placeholder = wz::engine::assets::make_placeholder_mesh_key();

    EXPECT_NE(triangle, quad);
    EXPECT_NE(triangle, cube);
    EXPECT_NE(triangle, placeholder);
    EXPECT_NE(quad, cube);
    EXPECT_NE(quad, placeholder);
    EXPECT_NE(cube, placeholder);
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

TEST(MeshKeyFactory, PlaceholderMeshKeyUsesExpectedSchema)
{
    const auto placeholder =
        wz::engine::assets::make_placeholder_mesh_key();

    EXPECT_EQ(
        placeholder.schema_hash,
        wz::engine::assets::detail::hash_u64(
            wz::engine::assets::kPlaceholderMeshSchema.value));
}

TEST(MeshKeyFactory, ClipmapLatticeKeyIsDeterministic)
{
    const wz::engine::assets::ClipmapLatticeMeshDesc desc{
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.0f,
    };

    const auto a = wz::engine::assets::make_clipmap_lattice_mesh_key(desc);
    const auto b = wz::engine::assets::make_clipmap_lattice_mesh_key(desc);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, wz::asset::AssetKey{});
}

TEST(MeshKeyFactory, ClipmapLatticeKeyUsesExpectedSchema)
{
    const wz::engine::assets::ClipmapLatticeMeshDesc desc{};
    const auto key = wz::engine::assets::make_clipmap_lattice_mesh_key(desc);

    EXPECT_EQ(
        key.schema_hash,
        wz::engine::assets::detail::hash_u64(
            wz::engine::assets::kProceduralClipmapLatticeMeshSchema.value));
}

TEST(MeshKeyFactory, ClipmapLatticeParametersAreIdentityInputs)
{
    const wz::engine::assets::ClipmapLatticeMeshDesc base{
        .level_count = 4u,
        .base_resolution = 8u,
        .cell_size = 1.0f,
    };

    const auto base_key =
        wz::engine::assets::make_clipmap_lattice_mesh_key(base);

    auto with_levels = base;
    with_levels.level_count = 5u;
    auto with_resolution = base;
    with_resolution.base_resolution = 16u;
    auto with_cell = base;
    with_cell.cell_size = 2.0f;

    EXPECT_NE(
        base_key,
        wz::engine::assets::make_clipmap_lattice_mesh_key(with_levels));
    EXPECT_NE(
        base_key,
        wz::engine::assets::make_clipmap_lattice_mesh_key(with_resolution));
    EXPECT_NE(
        base_key,
        wz::engine::assets::make_clipmap_lattice_mesh_key(with_cell));
}

TEST(MeshKeyFactory, ClipmapLatticeKeyDistinctFromOtherProceduralMeshes)
{
    const auto lattice = wz::engine::assets::make_clipmap_lattice_mesh_key(
        wz::engine::assets::ClipmapLatticeMeshDesc{});
    const auto cube = wz::engine::assets::make_procedural_cube_mesh_key();
    const auto quad = wz::engine::assets::make_procedural_quad_mesh_key();

    EXPECT_NE(lattice, cube);
    EXPECT_NE(lattice, quad);
}
