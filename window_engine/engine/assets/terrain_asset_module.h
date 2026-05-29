#pragma once

// engine/assets/terrain_asset_module.h

#include <asset/system.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/terrain/terrain.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct TerrainFromHeightFieldDesc
    {
        std::string name;
        ScalarFieldAsset height_field;

        float origin[2]{ 0.0f, 0.0f };
        float size[2]{ 1.0f, 1.0f };
        float vertical_scale = 1.0f;
        float base_height = 0.0f;

        wz::asset::AssetKey normal_field{};
        wz::asset::AssetKey material_mask_set{};
        TerrainRenderMode render_mode = TerrainRenderMode::DebugMesh;
        TerrainCollisionMode collision_mode =
            TerrainCollisionMode::HeightOnly;
    };

    struct TerrainFromMeshDesc
    {
        std::string name;
        MeshAsset mesh;

        TerrainRenderMode render_mode = TerrainRenderMode::DebugMesh;
        TerrainCollisionMode collision_mode =
            TerrainCollisionMode::MeshSurface;
    };

    struct TerrainAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct TerrainHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class TerrainAssetModule
    {
    public:
        TerrainAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            TerrainAssetTable& table);

        [[nodiscard]] TerrainAsset create_from_height_field(
            const TerrainFromHeightFieldDesc& desc);

        [[nodiscard]] TerrainAsset create_from_mesh(
            const TerrainFromMeshDesc& desc);

        [[nodiscard]] TerrainHandle get_terrain(
            const TerrainAsset& asset) const;

        [[nodiscard]] const TerrainAssetData* get_terrain_data(
            TerrainHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        TerrainAssetTable& table_;
    };
}
