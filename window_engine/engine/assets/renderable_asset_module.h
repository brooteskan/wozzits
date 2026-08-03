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
#include <engine/assets/puppet_asset_module.h>
#include <engine/assets/star_catalog_asset_module.h>
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

    // ClipmapLandscapeRenderableDesc + create_clipmap_landscape were retired
    // with the 0x708 schema (issue #234): the clipmap is now authored as a
    // 0x70A custom renderable (CameraSnappedTerrain head + a scalar_field_texture
    // binding + a Placement port), so it goes through create_custom_renderable /
    // the graph path like any other custom look.

    struct GaussianSplatCloudRhiRenderableDesc
    {
        std::string name;
        // Splat cloud, resident as a decoded splat StructuredBuffer (#208).
        GaussianSplatCloudAsset splat_cloud{};
        // SplatPull render program.
        RenderProgramAsset program{};
        GaussianSplatCloudRenderSettings settings{};
    };

    struct PuppetRhiRenderableDesc
    {
        std::string name;
        // Inochi2D puppet, resident as atlases + per-Part pull buffers.
        PuppetAsset puppet{};
        // Puppet render program (MeshVertexPull binding model).
        RenderProgramAsset program{};
    };

    struct StarFieldRhiRenderableDesc
    {
        std::string name;
        // Star catalog, resident as a decoded point StructuredBuffer (#266).
        StarCatalogAsset star_catalog{};
        // SplatPull render program.
        RenderProgramAsset program{};
        StarFieldRenderSettings settings{};
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

        RenderableAsset create_rhi_pull_mesh(
            const RhiPullMeshRenderableDesc& desc);

        RenderableAsset create_gpu_sparse_mesh_renderable(
            const GpuSparseMeshRenderableDesc& desc);

        RenderableAsset create_gaussian_splat_cloud_rhi(
            const GaussianSplatCloudRhiRenderableDesc& desc);

        RenderableAsset create_puppet_rhi(
            const PuppetRhiRenderableDesc& desc);

        RenderableAsset create_star_field_rhi(
            const StarFieldRhiRenderableDesc& desc);

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
