#pragma once

// engine/assets/collision_asset_module.h

#include <asset/system.h>
#include <engine/assets/collision/collision.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/placement_asset_module.h>
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

    // Authoring recipe for a constraint surface built DIRECTLY from a scalar
    // field heightfield + world mapping, bypassing TerrainAsset. World Y is
    // baked as value * vertical_scale + base_height at the projection
    // resolution (0 = use the source field resolution).
    struct CollisionFromHeightFieldDesc
    {
        std::string name;
        ScalarFieldAsset height_field;
        // Optional world-space frame (issue #218 Phase 1). When valid, it is
        // connected as a second graph dependency and becomes authoritative for
        // origin / size / vertical_scale / base_height at compile time,
        // overriding the per-field values below. When invalid (default), the
        // per-field values are used exactly as before (back-compatible).
        PlacementAsset placement;
        float origin[2]{ 0.0f, 0.0f };
        float size[2]{ 1.0f, 1.0f };
        float vertical_scale = 1.0f;
        float base_height = 0.0f;
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

        [[nodiscard]] CollisionAsset create_from_height_field(
            const CollisionFromHeightFieldDesc& desc);

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
