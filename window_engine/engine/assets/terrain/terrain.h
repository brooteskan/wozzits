#pragma once

// engine/assets/terrain/terrain.h

#include <engine/assets/terrain/terrain_visual_proxy.h>

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

    enum class TerrainMeshSurfaceHeightPolicy : uint8_t
    {
        HighestAcceptedSurface = 0,
    };

    enum class TerrainNormalSource : uint8_t
    {
        DerivedGeometry = 0,
        MeshVertexNormal,
        ImportedField,
    };

    enum class TerrainUVSource : uint8_t
    {
        None = 0,
        MeshUV0,
        PlanarXZ,
        ImportedField,
    };

    // ─── Descriptor range checks ──────────────────────────────────────────────────
    //
    // All six are stored in the mesh-terrain disk cache as raw uint8 and were
    // static_cast straight back with no range check, so a flipped byte produced
    // an out-of-range enum that every other check in the loader missed -- magic,
    // format version, compiler version and the stored key all cover different
    // byte regions. Same shape as the scalar-field descriptors (B1-C4).
    //
    // Measured, by neutering these checks and seeing which cases in
    // TerrainLoadRejectsOutOfRangeDescriptorOrdinals stop failing: five of the
    // six were genuinely reachable. `representation` was not, on this path
    // only -- load_cached_mesh_terrain separately requires it to equal
    // MeshSurface. TerrainAssetData::valid() would NOT have caught it: it
    // branches on HeightField and returns true for everything else, including
    // a value that is neither enumerator. So the representation check here is
    // defence in depth for a second cache path that does not repeat that
    // equality test.
    //
    // WHEN YOU APPEND AN ENUMERATOR, UPDATE THE MATCHING PREDICATE. They live
    // beside the enums so the pairing is visible at the point of change.
    // Under-accepting is the safe direction: an entry carrying an enumerator
    // this build does not know is rejected, and the asset recompiles.

    [[nodiscard]] constexpr bool valid_terrain_representation_kind(
        uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(TerrainRepresentationKind::MeshSurface);
    }

    [[nodiscard]] constexpr bool valid_terrain_render_mode(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(TerrainRenderMode::DebugMesh);
    }

    [[nodiscard]] constexpr bool valid_terrain_collision_mode(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(TerrainCollisionMode::MeshSurface);
    }

    [[nodiscard]] constexpr bool valid_terrain_mesh_surface_height_policy(
        uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(
            TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface);
    }

    [[nodiscard]] constexpr bool valid_terrain_normal_source(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(TerrainNormalSource::ImportedField);
    }

    [[nodiscard]] constexpr bool valid_terrain_uv_source(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(TerrainUVSource::ImportedField);
    }

    struct TerrainVisualChunkAggregate
    {
        float mean_height = 0.0f;
        float height_variance = 0.0f;
        float normal_mean[3]{ 0.0f, 1.0f, 0.0f };
        float normal_variance[2]{ 0.0f, 0.0f };
        float albedo_mean[3]{ 1.0f, 1.0f, 1.0f };
        uint32_t triangle_count = 0;
    };

    struct TerrainVisualChunk
    {
        float bounds_min[3]{ 0.0f, 0.0f, 0.0f };
        float bounds_max[3]{ 0.0f, 0.0f, 0.0f };
        uint32_t first_index = 0;
        uint32_t index_count = 0;
        uint32_t replacement_first_index = 0;
        uint32_t replacement_index_count = 0;
        TerrainVisualChunkAggregate aggregate{};

        [[nodiscard]] uint32_t triangle_count() const noexcept
        {
            return index_count / 3u;
        }

        [[nodiscard]] uint32_t replacement_triangle_count() const noexcept
        {
            return replacement_index_count / 3u;
        }
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

        TerrainMeshSurfaceHeightPolicy mesh_height_policy =
            TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface;
        float min_surface_normal_y = 0.2f;
        bool include_backfaces = false;

        TerrainNormalSource normal_source = TerrainNormalSource::DerivedGeometry;
        TerrainUVSource uv_source = TerrainUVSource::None;
        bool mesh_has_source_normals = false;
        bool mesh_has_source_uv0 = false;
        uint32_t mesh_triangle_count = 0;
        uint32_t mesh_accepted_surface_triangle_count = 0;
        uint32_t mesh_visual_chunk_count = 4096;

        float origin[2]{ 0.0f, 0.0f };
        float size[2]{ 1.0f, 1.0f };
        uint32_t resolution_x = 0;
        uint32_t resolution_y = 0;

        float vertical_scale = 1.0f;
        float base_height = 0.0f;
        float min_height = 0.0f;
        float max_height = 0.0f;

        std::vector<float> height_samples;

        float bounds_min[3]{ 0.0f, 0.0f, 0.0f };
        float bounds_max[3]{ 0.0f, 0.0f, 0.0f };

        TerrainRenderMode render_mode = TerrainRenderMode::DebugMesh;
        TerrainCollisionMode collision_mode =
            TerrainCollisionMode::HeightOnly;

        bool supports_height_query = false;
        bool supports_ray_query = false;
        bool supports_render_mesh = false;

        std::vector<float> mesh_surface_points;
        std::vector<uint32_t> mesh_surface_indices;
        std::vector<uint32_t> mesh_visual_indices;
        std::vector<TerrainVisualChunk> mesh_visual_chunks;

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

        TerrainMeshSurfaceHeightPolicy height_policy =
            TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface;
        float min_surface_normal_y = 0.2f;
        bool include_backfaces = false;
        TerrainNormalSource preferred_normal_source =
            TerrainNormalSource::MeshVertexNormal;
        TerrainUVSource preferred_uv_source = TerrainUVSource::MeshUV0;

        TerrainRenderMode render_mode = TerrainRenderMode::DebugMesh;
        TerrainCollisionMode collision_mode =
            TerrainCollisionMode::MeshSurface;
        uint32_t visual_chunk_count = 4096;
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
