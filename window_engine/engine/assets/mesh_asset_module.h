#pragma once
// window_engine/engine/assets/mesh_asset_module.h

#include <asset/system.h>
#include <engine/assets/mesh/mesh.h>

#include <cstdint>
#include <string>

namespace wz::engine::assets
{
    enum class ProceduralMeshKind
    {
        Triangle,
        Quad,
        Cube,
    };

    struct ProceduralMeshDesc
    {
        std::string name;
        ProceduralMeshKind kind = ProceduralMeshKind::Triangle;
    };

    struct MeshAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct MeshHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    struct GLBMeshDesc
    {
        std::string name;
        wz::asset::AssetKey source_file;
        uint32_t mesh_index = 0;
    };

    struct MeshDecimationAssetDesc
    {
        std::string name;
        MeshAsset source_mesh;

        uint32_t target_vertex_count = 0;
        uint32_t target_triangle_count = 0;
        float target_ratio = 0.0f;

        bool preserve_boundary = true;

        float aspect_ratio = 0.0f;
        float edge_length = 0.0f;
        uint32_t max_valence = 0;
        float normal_deviation = 0.0f;
        float hausdorff_error = 0.0f;
    };

    struct DebugTriangleStrideMeshDesc
    {
        std::string name;
        MeshAsset source_mesh;
        uint32_t stride = 1;
        uint32_t phase = 0;
    };

    // Procedural geometry-clipmap lattice. Generates a static, reusable
    // nested-LOD-ring footprint in the XZ plane (y = 0), centered at the
    // origin in grid-unit space. All three fields are identity inputs folded
    // into the AssetKey. Defaults: 4 levels, an 8 x 8 fine center, unit cells.
    //
    //   level_count   Number of LOD levels (>= 1). Level 0 is the full fine
    //                 center grid; each outer level is a ring at double the
    //                 cell size of the level it encloses.
    //   base_resolution
    //                 Quad cells per side of the fine center grid (>= 1; any
    //                 value is accepted). The generator tiles gap-free for any
    //                 resolution — each coarse ring's hole is the finer level's
    //                 footprint snapped INWARD to the coarse grid, so the levels
    //                 meet with a covered overlap rather than a possible gap —
    //                 so there is no multiple-of-4 requirement.
    //   cell_size     World/grid extent of one level-0 cell.
    struct ClipmapLatticeMeshDesc
    {
        std::string name;
        uint32_t level_count = 4u;
        uint32_t base_resolution = 8u;
        float cell_size = 1.0f;
    };

    class MeshAssetModule
    {
    public:
        explicit MeshAssetModule(
            wz::asset::AssetSystem& system,
            MeshTable& table);

        [[nodiscard]] MeshAsset create_procedural_mesh(
            const ProceduralMeshDesc& desc);

        [[nodiscard]] MeshAsset create_glb_mesh(
            const GLBMeshDesc& desc);

        [[nodiscard]] MeshAsset create_decimated_mesh(
            const MeshDecimationAssetDesc& desc);

        [[nodiscard]] MeshAsset create_debug_triangle_stride_mesh(
            const DebugTriangleStrideMeshDesc& desc);

        [[nodiscard]] MeshAsset create_clipmap_lattice_mesh(
            const ClipmapLatticeMeshDesc& desc);

        [[nodiscard]] MeshAsset create_placeholder_mesh(
            std::string name = {});

        [[nodiscard]] MeshHandle get_mesh(
            const MeshAsset& asset) const;

        [[nodiscard]] const MeshData* get_mesh_data(
            MeshHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        MeshTable& table_;
    };
}
