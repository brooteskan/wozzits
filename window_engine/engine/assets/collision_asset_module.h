#pragma once

// engine/assets/collision_asset_module.h

#include <asset/system.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/terrain_asset_module.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct CollisionFromMeshDesc
    {
        std::string name;
        MeshAsset mesh;
        CollisionBuildMethod build_method =
            CollisionBuildMethod::TriangleMesh;
        CollisionOccupancyData occupancy{};
    };

    struct CollisionFromTerrainDesc
    {
        std::string name;
        TerrainAsset terrain;
        CollisionBuildMethod build_method =
            CollisionBuildMethod::TerrainSurface;
        CollisionOccupancyData occupancy{
            CollisionOccupancyKind::WalkableSurface,
            true,
            true,
        };
        uint32_t projection_resolution_x = 0;
        uint32_t projection_resolution_y = 0;
    };

    struct CollisionAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct CollisionHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class CollisionAssetModule
    {
    public:
        CollisionAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            CollisionAssetTable& table);

        [[nodiscard]] CollisionAsset create_from_mesh(
            const CollisionFromMeshDesc& desc);

        [[nodiscard]] CollisionAsset create_from_terrain(
            const CollisionFromTerrainDesc& desc);

        [[nodiscard]] CollisionHandle get_collision(
            const CollisionAsset& asset) const;

        [[nodiscard]] CollisionHandle find_collision(
            const CollisionAsset& asset) const;

        [[nodiscard]] const CollisionAssetData* get_collision_data(
            CollisionHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        CollisionAssetTable& table_;
    };
}
