// tests/asset_mesh/mesh_asset_module_interface.cpp

#include <gtest/gtest.h>

#include <asset/compiler.h>
#include <asset/system.h>
#include <engine/assets/mesh_asset_module.h>

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

TEST(MeshAssetModuleInterface, StubCreateProceduralMeshReturnsInvalidAsset)
{
    wz::asset::CompilerRegistry registry{};
    wz::asset::AssetSystem system{ std::move(registry) };
    wz::engine::assets::MeshTable table{};
    wz::engine::assets::MeshAssetModule module{ system, table };

    wz::engine::assets::ProceduralMeshDesc desc{};
    desc.name = "triangle";
    desc.kind = wz::engine::assets::ProceduralMeshKind::Triangle;

    const auto asset = module.create_procedural_mesh(desc);

    EXPECT_FALSE(asset.valid());
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