#pragma once

// engine/assets/terrain/terrain.h

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    enum class TerrainRepresentationKind : uint8_t
    {
        HeightField,
        MeshSurface,
    };

    enum class TerrainRenderMode : uint8_t
    {
        None,
        DebugMesh,
    };

    enum class TerrainCollisionMode : uint8_t
    {
        None,
        HeightOnly,
        MeshSurface,
    };

    struct TerrainAssetData
    {
        TerrainRepresentationKind representation =
            TerrainRepresentationKind::HeightField;

        wz::asset::AssetKey source_asset{};
        wz::asset::AssetKey height_field{};
        wz::asset::AssetKey mesh{};
        wz::asset::AssetKey normal_field{};
        wz::asset::AssetKey material_mask_set{};

        float origin[2]{ 0.0f, 0.0f };
        float size[2]{ 1.0f, 1.0f };
        uint32_t resolution_x = 0;
        uint32_t resolution_y = 0;

        float vertical_scale = 1.0f;
        float base_height = 0.0f;
        float min_height = 0.0f;
        float max_height = 0.0f;

        float bounds_min[3]{ 0.0f, 0.0f, 0.0f };
        float bounds_max[3]{ 0.0f, 0.0f, 0.0f };

        TerrainRenderMode render_mode = TerrainRenderMode::DebugMesh;
        TerrainCollisionMode collision_mode =
            TerrainCollisionMode::HeightOnly;

        bool supports_height_query = false;
        bool supports_ray_query = false;
        bool supports_render_mesh = false;

        bool valid() const noexcept;
    };

    struct TerrainFromHeightFieldCompileDesc
    {
        wz::asset::AssetKey height_field{};

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

    struct TerrainFromMeshCompileDesc
    {
        wz::asset::AssetKey mesh{};

        TerrainRenderMode render_mode = TerrainRenderMode::DebugMesh;
        TerrainCollisionMode collision_mode =
            TerrainCollisionMode::MeshSurface;
    };

    class TerrainAssetTable
    {
    public:
        TerrainAssetTable();

        wz::asset::ResourceHandle add(TerrainAssetData terrain);

        const TerrainAssetData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            TerrainAssetData terrain;
        };

        std::vector<Slot> slots_;
    };
}
