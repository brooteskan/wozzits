#pragma once

// engine/assets/renderable_asset_module.h

#include <asset/system.h>

#include <engine/assets/renderable/renderable.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_render_style_asset_module.h>
#include <engine/assets/mesh_derived_field_asset_module.h>
#include <engine/assets/gpu_sparse_mesh_asset_module.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/gaussian_splat_color_lod_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/placement_asset_module.h>
#include <engine/assets/terrain_asset_module.h>
#include <engine/assets/terrain_visual_proxy_asset_module.h>
#include <engine/assets/render_program/render_program_asset_module.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct ScalarFieldDebugRenderableDesc
    {
        std::string name;
        ScalarFieldAsset scalar_field{};
    };

    struct TerrainDebugRenderableDesc
    {
        std::string name;
        TerrainAsset terrain{};
        BuiltinRenderProgram mesh_program =
            BuiltinRenderProgram::MeshWireframeDepthDebug;
        RenderDomain domain = RenderDomain::Debug;
        uint32_t mesh_policy_flags =
            RenderPolicy_Wireframe
            | RenderPolicy_DepthTest
            | RenderPolicy_DepthWrite;
    };

    struct TerrainSurfaceRenderableDesc
    {
        std::string name;
        TerrainAsset terrain{};
        TerrainVisualProxyAsset visual_proxy{};
        BuiltinRenderProgram mesh_program =
            BuiltinRenderProgram::TerrainMeshSurface;
        RenderDomain domain = RenderDomain::Opaque;
        uint32_t mesh_policy_flags =
            RenderPolicy_DepthTest
            | RenderPolicy_DepthWrite;
        TerrainLightingData lighting{};
        float target_pixels_per_triangle = 0.0f;
    };

    struct RhiPullMeshRenderableDesc
    {
        std::string name;
        MeshAsset mesh{};
        RenderProgramAsset program{};
        // Optional MeshRenderStyle whose SHADING constants are baked into the
        // recipe (issue #195 slice A). Default (invalid) = no style.
        MeshRenderStyleAsset style{};
    };

    struct GpuSparseMeshRenderableDesc
    {
        std::string name;
        GpuSparseMeshAsset sparse_mesh{};
        RenderProgramAsset program{};
    };

    struct ClipmapLandscapeRenderableDesc
    {
        std::string name;
        // Clipmap lattice geometry (the kProceduralClipmapLatticeMeshSchema
        // mesh, or any kAssetTypeMesh).
        MeshAsset lattice_mesh{};
        // Height source — a scalar field resident as an R32 Texture2D (#197).
        ScalarFieldAsset height_field{};
        RenderProgramAsset program{};
        // Optional world-space frame (issue #218 Phase 2). When valid, it is
        // connected as a 4th graph dependency and becomes authoritative for the
        // texture->world footprint (world_origin/world_size/vertical_scale/
        // base_height) at compile time, overriding the corresponding fields in
        // settings below. lattice_world_cell_size is unaffected. When invalid
        // (default), settings are used exactly as before (back-compatible).
        PlacementAsset placement{};
        ClipmapLandscapeRenderSettings settings{};
    };

    struct GaussianSplatCloudRhiRenderableDesc
    {
        std::string name;
        // Splat cloud, resident as a decoded splat StructuredBuffer (#208).
        GaussianSplatCloudAsset splat_cloud{};
        // SplatPull render program.
        RenderProgramAsset program{};
        GaussianSplatCloudRenderSettings settings{};
    };

    // Custom renderable (issue #229): the programmatic form of a 0x70A node.
    // Bindings/constants are AUTHORED rows validated at compile against the
    // program's layout (#228); rows pass through even half-authored so the
    // compile can NAME a failure instead of silently dropping it. This is the
    // desc the per-scene-node synthesis fills (assemble_render_bindings).
    struct CustomRenderableDesc
    {
        std::string name;
        MeshAsset mesh{};
        RenderProgramAsset program{};
        std::vector<CustomRenderableCompileDesc::Binding> bindings;
        std::vector<CustomRenderableCompileDesc::Constant> constants;
    };

    struct RenderableAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct RenderableHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class RenderableAssetModule
    {
    public:
        RenderableAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            RenderableAssetTable& table,
            RhiRenderableTable& rhi_table);

        RenderableAsset create_scalar_field_debug(
            const ScalarFieldDebugRenderableDesc& desc);

        RenderableAsset create_terrain_debug(
            const TerrainDebugRenderableDesc& desc);

        RenderableAsset create_terrain_surface(
            const TerrainSurfaceRenderableDesc& desc);

        RenderableAsset create_rhi_pull_mesh(
            const RhiPullMeshRenderableDesc& desc);

        RenderableAsset create_gpu_sparse_mesh_renderable(
            const GpuSparseMeshRenderableDesc& desc);

        RenderableAsset create_clipmap_landscape(
            const ClipmapLandscapeRenderableDesc& desc);

        RenderableAsset create_gaussian_splat_cloud_rhi(
            const GaussianSplatCloudRhiRenderableDesc& desc);

        RenderableAsset create_custom_renderable(
            const CustomRenderableDesc& desc);

        RenderableHandle get_renderable(
            const RenderableAsset& asset) const;

        const RenderableAssetData* get_renderable_data(
            RenderableHandle handle) const;

        const RhiRenderableRecipe* get_rhi_renderable_recipe(
            const RenderableAsset& asset) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        RenderableAssetTable& table_;
        RhiRenderableTable& rhi_table_;
    };
}
