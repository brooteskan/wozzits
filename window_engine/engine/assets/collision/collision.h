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

    // One coarser level of the box-filtered mip chain over a heightfield's
    // samples. Row-major, index x + y*width, tightly packed. Mirrors the
    // ScalarFieldMipLevel the resident GPU height texture is built from -- see
    // CollisionAssetData::height_mips for why the two agree.
    struct CollisionHeightMipLevel
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<float> values;
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
        // Ray queries consult these unconditionally; HEIGHT sampling does so
        // only when constrain_to_drawn_surface is set below.
        uint32_t render_lod_base_resolution = 0;
        uint32_t render_lod_level_count = 0;

        // The lattice's finest cell (metres), from the same
        // ClipmapLatticeSchedule as the two above. Carried rather than derived:
        // it equals size/resolution only while the collision samples the field
        // at its native resolution, and a projection_resolution that resamples
        // the grid would break that silently.
        float render_lod_cell_size = 0.0f;

        // Opt in to standing on the DRAWN surface instead of the true one.
        //
        // A terrain-constrained actor normally snaps to the full-resolution
        // bicubic -- the physical truth, and independent of where anyone is
        // looking. With a clipmap drawing this field, that truth is not what is
        // on screen: coarse rings read box-filtered mips at wide cell spacing,
        // so the drawn ground sits above a valley floor and below a ridge, by
        // more the further it is from the camera. An actor placed by the truth
        // then visibly sinks or floats out there.
        //
        // Setting this trades the view-independence for agreement with the
        // picture: heights are answered from the reconstruction, which is a
        // function of the OBSERVER as well as the position, so an actor's
        // ground moves as the camera does. Orientation is deliberately NOT
        // switched over -- see collision_surface_sampling.
        bool constrain_to_drawn_surface = false;

        // Box-filtered mip chain over height_samples, from level 1 outward --
        // level 0 IS height_samples, so it is not duplicated here. Present only
        // when render-LOD reconstruction is configured above; a heightfield with
        // no clipmap drawing it pays nothing.
        //
        // These are the levels the clipmap VS samples: a level-L ring reads mip
        // L of the resident height texture. That texture's pyramid is built by
        // build_scalar_field_mip_pyramid from the RAW field, while these are
        // built from height_samples, which have vertical_scale / base_height
        // already baked in -- the two still agree, because a box filter is a
        // mean and mean(a*x + b) == a*mean(x) + b, so baking the affine before
        // or after filtering yields the same surface.
        //
        // DERIVED, never serialized: rebuilt after a disk-cache load from the
        // samples themselves, so it cannot go stale against them.
        //
        // Caveat: the agreement holds at the field's NATIVE resolution. A
        // collision built with a projection_resolution that resamples the grid
        // has its own sample lattice, and its pyramid is then an approximation
        // of the drawn one rather than a match.
        std::vector<CollisionHeightMipLevel> height_mips;

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

        // Render-LOD reconstruction schedule (see CollisionAssetData). 0 =
        // disabled. Normally supplied by a connected ClipmapLatticeSchedule,
        // which supersedes anything authored here.
        uint32_t render_lod_base_resolution = 0;
        uint32_t render_lod_level_count = 0;
        float render_lod_cell_size = 0.0f;

        // Opt in to constraining actors to the DRAWN surface (see
        // CollisionAssetData::constrain_to_drawn_surface).
        bool constrain_to_drawn_surface = false;
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
