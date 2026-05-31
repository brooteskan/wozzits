#pragma once

// engine/assets/collision/collision.h

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    enum class CollisionSourceKind : uint8_t
    {
        Mesh,
        Terrain,
    };

    enum class CollisionOccupancyKind : uint8_t
    {
        Solid,
        Surface,
        WalkableSurface,
        Sensor,
    };

    enum class CollisionShapeKind : uint8_t
    {
        Bounds,
        TriangleMesh,
        TerrainHeightField,
        TerrainMeshSurface,
    };

    enum class CollisionBuildMethod : uint8_t
    {
        Bounds,
        TriangleMesh,
        TerrainSurface,
    };

    struct CollisionOccupancyData
    {
        CollisionOccupancyKind kind = CollisionOccupancyKind::Solid;
        bool blocks_movement = true;
        bool queryable = true;
    };

    struct CollisionPoint
    {
        float position[3]{};
    };

    struct CollisionAssetData
    {
        CollisionSourceKind source_kind = CollisionSourceKind::Mesh;
        CollisionShapeKind shape_kind = CollisionShapeKind::Bounds;
        CollisionOccupancyData occupancy{};

        wz::asset::AssetKey source_asset{};
        wz::asset::AssetKey geometry_asset{};

        float bounds_min[3]{};
        float bounds_max[3]{};

        std::vector<CollisionPoint> points;
        std::vector<uint32_t> indices;

        wz::asset::AssetKey height_field{};
        wz::asset::AssetKey mesh{};
        float origin[2]{};
        float size[2]{};
        uint32_t resolution_x = 0;
        uint32_t resolution_y = 0;
        float min_height = 0.0f;
        float max_height = 0.0f;

        uint32_t source_triangle_count = 0;
        uint32_t accepted_triangle_count = 0;

        bool supports_bounds_query = true;
        bool supports_height_query = false;
        bool supports_ray_query = false;
        bool supports_overlap_query = false;

        bool valid() const noexcept;
    };

    struct CollisionFromMeshCompileDesc
    {
        wz::asset::AssetKey mesh{};
        CollisionBuildMethod build_method =
            CollisionBuildMethod::TriangleMesh;
        CollisionOccupancyData occupancy{};
    };

    struct CollisionFromTerrainCompileDesc
    {
        wz::asset::AssetKey terrain{};
        CollisionBuildMethod build_method =
            CollisionBuildMethod::TerrainSurface;
        CollisionOccupancyData occupancy{
            CollisionOccupancyKind::WalkableSurface,
            true,
            true,
        };
    };

    class CollisionAssetTable
    {
    public:
        CollisionAssetTable();

        wz::asset::ResourceHandle add(CollisionAssetData collision);
        const CollisionAssetData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            CollisionAssetData collision;
        };

        std::vector<Slot> slots_;
    };
}
