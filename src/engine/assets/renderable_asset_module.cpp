// src/engine/assets/renderable_asset_module.cpp

#include <engine/assets/renderable_asset_module.h>

#include <engine/assets/key_factories/renderable.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    RenderableAssetModule::RenderableAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        RenderableAssetTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    RenderableAsset RenderableAssetModule::create_mesh_wireframe(
        const MeshWireframeRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("mesh wireframe renderable has empty name");
            return {};
        }

        if (!desc.mesh.valid()) {
            logger_.error("mesh wireframe renderable has invalid mesh: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_mesh_wireframe_renderable_key(
                desc.name,
                desc.mesh.output,
                desc.program,
                desc.policy_flags,
                desc.domain);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kMeshWireframeRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = MeshWireframeRenderableCompileDesc{
            .mesh_asset = desc.mesh.output,
            .program = desc.program,
            .domain = desc.domain,
            .policy_flags = desc.policy_flags,
        };

        if (!system_.register_asset(std::move(node), { desc.mesh.output }))
            return RenderableAsset{ .output = key };

        return RenderableAsset{
            .output = key,
        };
    }

    RenderableAsset RenderableAssetModule::create_gaussian_splat_debug(
        const GaussianSplatDebugRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("gaussian splat debug renderable has empty name");
            return {};
        }

        if (!desc.splat_cloud.valid()) {
            logger_.error("gaussian splat debug renderable has invalid splat cloud: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey color_lod_key =
            desc.color_lod.valid() ? desc.color_lod.output : wz::asset::AssetKey{};

        const wz::asset::AssetKey key =
            make_gaussian_splat_debug_renderable_key(
                desc.name,
                desc.splat_cloud.output,
                color_lod_key);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kGaussianSplatDebugRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = GaussianSplatDebugRenderableCompileDesc{
            .splat_cloud_asset = desc.splat_cloud.output,
            .color_lod_asset   = color_lod_key,
        };

        // Cloud is always a dep; the second slot is an explicit optional LOD.
        const std::vector<wz::asset::AssetKey> deps{
            desc.splat_cloud.output,
            color_lod_key,
        };

        if (!system_.register_asset(std::move(node), deps))
            return RenderableAsset{ .output = key };

        return RenderableAsset{
            .output = key,
        };
    }

    RenderableAsset RenderableAssetModule::create_scalar_field_debug(
        const ScalarFieldDebugRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("scalar field debug renderable has empty name");
            return {};
        }

        if (!desc.scalar_field.valid()) {
            logger_.error("scalar field debug renderable has invalid scalar field: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_scalar_field_debug_renderable_key(
                desc.name,
                desc.scalar_field.output);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kScalarFieldDebugRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = ScalarFieldDebugRenderableCompileDesc{
            .scalar_field_asset = desc.scalar_field.output,
        };

        if (!system_.register_asset(std::move(node), { desc.scalar_field.output }))
            return RenderableAsset{ .output = key };

        return RenderableAsset{
            .output = key,
        };
    }

    RenderableAsset RenderableAssetModule::create_mesh_styled(
        const MeshStyledRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("mesh styled renderable has empty name");
            return {};
        }

        if (!desc.mesh.valid()) {
            logger_.error("mesh styled renderable has invalid mesh: "
                + desc.name);
            return {};
        }

        if (!desc.style.valid()) {
            logger_.error("mesh styled renderable has invalid style: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey mesh_field_visualization_key =
            desc.mesh_field_visualization.valid()
                ? desc.mesh_field_visualization.output
                : wz::asset::AssetKey{};

        const wz::asset::AssetKey key =
            make_mesh_styled_renderable_key(
                desc.name,
                desc.mesh.output,
                desc.style.output,
                mesh_field_visualization_key);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kMeshStyledRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = MeshStyledRenderableCompileDesc{
            .mesh_asset = desc.mesh.output,
            .style_asset = desc.style.output,
            .mesh_field_visualization_asset =
                mesh_field_visualization_key,
        };

        std::vector<wz::asset::AssetKey> deps{
            desc.mesh.output,
            desc.style.output,
        };
        if (!(mesh_field_visualization_key == wz::asset::AssetKey{})) {
            deps.push_back(mesh_field_visualization_key);
        }

        if (!system_.register_asset(std::move(node), std::move(deps)))
        {
            return RenderableAsset{ .output = key };
        }

        return RenderableAsset{ .output = key };
    }

    RenderableAsset RenderableAssetModule::create_terrain_debug(
        const TerrainDebugRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("terrain debug renderable has empty name");
            return {};
        }

        if (!desc.terrain.valid()) {
            logger_.error("terrain debug renderable has invalid terrain: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_terrain_debug_renderable_key(
                desc.name,
                desc.terrain.output,
                desc.mesh_program,
                desc.mesh_policy_flags,
                desc.domain);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kTerrainDebugRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = TerrainDebugRenderableCompileDesc{
            .terrain_asset = desc.terrain.output,
            .mesh_program = desc.mesh_program,
            .domain = desc.domain,
            .mesh_policy_flags = desc.mesh_policy_flags,
        };

        if (!system_.register_asset(std::move(node), { desc.terrain.output }))
            return RenderableAsset{ .output = key };

        return RenderableAsset{
            .output = key,
        };
    }

    RenderableAsset RenderableAssetModule::create_terrain_surface(
        const TerrainSurfaceRenderableDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("terrain surface renderable has empty name");
            return {};
        }

        if (!desc.terrain.valid()) {
            logger_.error("terrain surface renderable has invalid terrain: "
                + desc.name);
            return {};
        }

        if (!desc.visual_proxy.valid()) {
            logger_.error("terrain surface renderable has invalid visual proxy: "
                + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_terrain_surface_renderable_key(
                desc.name,
                desc.terrain.output,
                desc.visual_proxy.output,
                desc.mesh_program,
                desc.mesh_policy_flags,
                desc.domain,
                desc.lighting,
                desc.target_pixels_per_triangle);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kTerrainSurfaceRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = TerrainSurfaceRenderableCompileDesc{
            .terrain_asset = desc.terrain.output,
            .visual_proxy_asset = desc.visual_proxy.output,
            .mesh_program = desc.mesh_program,
            .domain = desc.domain,
            .mesh_policy_flags = desc.mesh_policy_flags,
            .lighting = desc.lighting,
            .target_pixels_per_triangle =
                desc.target_pixels_per_triangle,
        };

        if (!system_.register_asset(
                std::move(node),
                { desc.terrain.output, desc.visual_proxy.output }))
        {
            return RenderableAsset{ .output = key };
        }

        return RenderableAsset{
            .output = key,
        };
    }

    RenderableHandle RenderableAssetModule::get_renderable(
        const RenderableAsset& asset) const
    {
        if (!asset.valid())
            return {};

        RenderableHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("renderable handle not found");

        return out;
    }

    const RenderableAssetData* RenderableAssetModule::get_renderable_data(
        RenderableHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}
