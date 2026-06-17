#pragma once

// engine/assets/renderable_asset_module.h

#include <asset/system.h>

#include <engine/assets/renderable/renderable.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_render_style_asset_module.h>
#include <engine/assets/mesh_derived_field_asset_module.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/gaussian_splat_color_lod_asset_module.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/terrain_asset_module.h>
#include <engine/assets/terrain_visual_proxy_asset_module.h>
#include <engine/assets/render_program/render_program_asset_module.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct MeshWireframeRenderableDesc
    {
        std::string name;
        MeshAsset mesh{};
        BuiltinRenderProgram program = BuiltinRenderProgram::MeshWireframeDebug;
        RenderDomain domain = RenderDomain::Debug;
        uint32_t policy_flags = RenderPolicy_Wireframe;
    };

    struct MeshStyledRenderableDesc
    {
        std::string name;
        MeshAsset mesh{};
        MeshRenderStyleAsset style{};
        MeshDerivedFieldAsset mesh_field_visualization{};
        wz::asset::AssetKey render_program_asset{};
    };

    struct GaussianSplatDebugRenderableDesc
    {
        std::string name;
        GaussianSplatCloudAsset splat_cloud{};

        // Optional derived color-LOD product.  When set, the upload path
        // packs per-splat neighborhood color + confidence into the GPU
        // vertex.  When unset, the GPU vertex's LOD slot falls back to the
        // base color with confidence = 0 — no behavioural change versus the
        // pre-LOD renderer.
        GaussianSplatColorLODAsset color_lod{};
    };

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

        RenderableAsset create_mesh_wireframe(
            const MeshWireframeRenderableDesc& desc);

        RenderableAsset create_mesh_styled(
            const MeshStyledRenderableDesc& desc);

        RenderableAsset create_gaussian_splat_debug(
            const GaussianSplatDebugRenderableDesc& desc);

        RenderableAsset create_scalar_field_debug(
            const ScalarFieldDebugRenderableDesc& desc);

        RenderableAsset create_terrain_debug(
            const TerrainDebugRenderableDesc& desc);

        RenderableAsset create_terrain_surface(
            const TerrainSurfaceRenderableDesc& desc);

        RenderableAsset create_rhi_pull_mesh(
            const RhiPullMeshRenderableDesc& desc);

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
