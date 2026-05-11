#include <gtest/gtest.h>

#include <engine/assets/mesh_asset_module.h>

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