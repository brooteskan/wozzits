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
                desc.mesh.output);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kMeshWireframeRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = MeshWireframeRenderableCompileDesc{
            .mesh_asset = desc.mesh.output,
        };

        if (!system_.register_asset(std::move(node), { desc.mesh.output })) {
            logger_.error("failed to register mesh wireframe renderable: " + desc.name);
            return {};
        }

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

        const wz::asset::AssetKey key =
            make_gaussian_splat_debug_renderable_key(
                desc.name,
                desc.splat_cloud.output);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeRenderable;
        node.schema = kGaussianSplatDebugRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = GaussianSplatDebugRenderableCompileDesc{
            .splat_cloud_asset = desc.splat_cloud.output,
        };

        if (!system_.register_asset(std::move(node), { desc.splat_cloud.output })) {
            logger_.error("failed to register gaussian splat debug renderable: " + desc.name);
            return {};
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