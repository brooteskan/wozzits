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
        TerrainProjectionHeightField,
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

    struct CollisionTriangleBounds
    {
        float min[3]{};
        float max[3]{};
    };

    struct CollisionSurfaceGrid
    {
        float origin_x = 0.0f;
        float origin_z = 0.0f;
        float cell_size_x = 1.0f;
        float cell_size_z = 1.0f;
        uint32_t cells_x = 0;
        uint32_t cells_z = 0;
        std::vector<uint32_t> cell_offsets;
        std::vector<uint32_t> cell_triangle_indices;
        std::vector<CollisionTriangleBounds> cell_bounds;
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
        std::vector<CollisionTriangleBounds> triangle_bounds;
        CollisionSurfaceGrid surface_grid;

        wz::asset::AssetKey height_field{};
        wz::asset::AssetKey mesh{};
        float origin[2]{};
        float size[2]{};
        uint32_t resolution_x = 0;
        uint32_t resolution_y = 0;
        float vertical_scale = 1.0f;
        float base_height = 0.0f;
        float min_height = 0.0f;
        float max_height = 0.0f;
        std::vector<float> height_samples;

        // True when origin/size/vertical_scale/base_height were baked from a
        // Placement asset (issue #218) and are therefore WORLD-frame values.
        // The collision runtime must NOT compose the carrying scene node's
        // world_from_local on top of them (issue #224); doing so would
        // double-apply a non-unit node scale.
        bool placement_driven = false;

        // Optional render-LOD reconstruction (clipmap ray-collision match). When
        // this heightfield is DRAWN as a geometry-clipmap, its coarse rings
        // triangulate the field at 2^L-cell spacing, so the drawn surface floats
        // above the true field over sub-cell relief and a full-res ray misses
        // grazing shots that visibly strike the drawn hill. These mirror the
        // clipmap LOD schedule (base_resolution m + level_count) so a RAY query
        // can reconstruct the drawn coarse surface (the finest cell c0 is derived
        // as size/resolution). 0 = disabled: ray uses the true full-res surface.
        // Only ray queries consult these; height/nearest sampling is unaffected.
        uint32_t render_lod_base_resolution = 0;
        uint32_t render_lod_level_count = 0;

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
        uint32_t projection_resolution_x = 0;
        uint32_t projection_resolution_y = 0;
    };

    // Collision derived DIRECTLY from a scalar-field heightfield + world
    // mapping, with no TerrainAsset intermediary. Mirrors the heightfield
    // branch of CollisionFromTerrain but reads the scalar field grid itself.
    struct CollisionFromHeightFieldCompileDesc
    {
        wz::asset::AssetKey height_field{};
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

        // Render-LOD reconstruction schedule for ray queries (see
        // CollisionAssetData). 0 = disabled.
        uint32_t render_lod_base_resolution = 0;
        uint32_t render_lod_level_count = 0;
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
