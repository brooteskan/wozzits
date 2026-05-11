// tests/asset_mesh/mesh_asset_module_interface.cpp

#include <gtest/gtest.h>

#include <asset/compiler.h>
#include <asset/system.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/key_factories/mesh.h>

#include <utility>

TEST(MeshAssetModuleInterface, DefaultMeshAssetIsInvalid)
{
    wz::engine::assets::MeshAsset asset{};

    EXPECT_FALSE(asset.valid());
}

TEST(MeshAssetModuleInterface, DefaultMeshHandleIsInvalid)
{
    wz::engine::assets::MeshHandle handle{};

    EXPECT_FALSE(handle.valid());
}

TEST(MeshAssetModuleInterface, DefaultProceduralMeshDescIsTriangle)
{
    wz::engine::assets::ProceduralMeshDesc desc{};

    EXPECT_EQ(
        desc.kind,
        wz::engine::assets::ProceduralMeshKind::Triangle);
}

TEST(MeshAssetModuleInterface, ModuleCanBeConstructed)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};

    wz::engine::assets::MeshAssetModule module{ system, table };

    SUCCEED();
}

TEST(MeshAssetModuleInterface, CreateProceduralMeshReturnsValidAsset)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    wz::engine::assets::ProceduralMeshDesc desc{};
    desc.name = "triangle";
    desc.kind = wz::engine::assets::ProceduralMeshKind::Triangle;

    const auto asset = module.create_procedural_mesh(desc);

    EXPECT_TRUE(asset.valid());
    EXPECT_NE(asset.output, wz::asset::AssetKey{});
}

TEST(MeshAssetModuleInterface, InvalidAssetDoesNotResolveToMeshHandle)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    const auto handle = module.get_mesh({});

    EXPECT_FALSE(handle.valid());
}

TEST(MeshAssetModuleInterface, InvalidHandleDoesNotResolveToMeshData)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    const wz::engine::assets::MeshData* data =
        module.get_mesh_data({});

    EXPECT_EQ(data, nullptr);
}

TEST(MeshAssetModuleInterface, CreateProceduralTriangleReturnsValidAsset)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    const auto asset = module.create_procedural_mesh({
        .name = "triangle",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        });

    EXPECT_TRUE(asset.valid());
    EXPECT_EQ(
        asset.output,
        wz::engine::assets::make_procedural_triangle_mesh_key());
}

TEST(MeshAssetModuleInterface, CreateProceduralQuadReturnsValidAsset)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    const auto asset = module.create_procedural_mesh({
        .name = "quad",
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        });

    EXPECT_TRUE(asset.valid());
    EXPECT_EQ(
        asset.output,
        wz::engine::assets::make_procedural_quad_mesh_key());
}

TEST(MeshAssetModuleInterface, CreateProceduralCubeReturnsValidAsset)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    const auto asset = module.create_procedural_mesh({
        .name = "cube",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });

    EXPECT_TRUE(asset.valid());
    EXPECT_EQ(
        asset.output,
        wz::engine::assets::make_procedural_cube_mesh_key());
}

TEST(MeshAssetModuleInterface, ProceduralMeshKindsHaveDistinctAssets)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    const auto triangle = module.create_procedural_mesh({
        .name = "triangle",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        });

    const auto quad = module.create_procedural_mesh({
        .name = "quad",
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        });

    const auto cube = module.create_procedural_mesh({
        .name = "cube",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        });

    ASSERT_TRUE(triangle.valid());
    ASSERT_TRUE(quad.valid());
    ASSERT_TRUE(cube.valid());

    EXPECT_NE(triangle.output, quad.output);
    EXPECT_NE(triangle.output, cube.output);
    EXPECT_NE(quad.output, cube.output);
}

TEST(MeshAssetModuleInterface, DuplicateProceduralMeshRegistrationReturnsInvalid)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    const auto first = module.create_procedural_mesh({
        .name = "triangle-a",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        });

    const auto second = module.create_procedural_mesh({
        .name = "triangle-b",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        });

    EXPECT_TRUE(first.valid());
    EXPECT_FALSE(second.valid());
}

TEST(MeshAssetModuleInterface, CommitSucceedsAfterRegisteringProceduralMeshes)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    EXPECT_TRUE(module.create_procedural_mesh({
        .name = "triangle",
        .kind = wz::engine::assets::ProceduralMeshKind::Triangle,
        }).valid());

    EXPECT_TRUE(module.create_procedural_mesh({
        .name = "quad",
        .kind = wz::engine::assets::ProceduralMeshKind::Quad,
        }).valid());

    EXPECT_TRUE(module.create_procedural_mesh({
        .name = "cube",
        .kind = wz::engine::assets::ProceduralMeshKind::Cube,
        }).valid());

    EXPECT_TRUE(system.commit());
}