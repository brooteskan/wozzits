// src/engine/assets/gaussian_splat_color_lod_asset_module.cpp

#include <engine/assets/gaussian_splat_color_lod_asset_module.h>

#include <engine/assets/key_factories/gaussian_splat_color_lod.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    GaussianSplatColorLODAssetModule::GaussianSplatColorLODAssetModule(
        wz::asset::AssetSystem&     system,
        wz::Logger&                 logger,
        GaussianSplatColorLODTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    GaussianSplatColorLODAsset
        GaussianSplatColorLODAssetModule::create_from_cloud(
            const GaussianSplatColorLODFromCloudDesc& desc)
    {
        GaussianSplatColorLODAsset out{};

        if (!desc.source_cloud.valid()) {
            logger_.error(
                "gaussian splat color LOD: empty source cloud asset: " + desc.name);
            return out;
        }

        if (desc.compile.neighbor_radius_world <= 0.0f) {
            logger_.error(
                "gaussian splat color LOD: non-positive neighbor_radius_world: "
                + desc.name);
            return out;
        }

        if (desc.compile.gaussian_sigma_world <= 0.0f) {
            logger_.error(
                "gaussian splat color LOD: non-positive gaussian_sigma_world: "
                + desc.name);
            return out;
        }

        const wz::asset::AssetKey key =
            make_gaussian_splat_color_lod_key(
                desc.source_cloud.output, desc.compile);

        wz::asset::AssetNode node{};
        node.key     = key;
        node.type    = kAssetTypeGaussianSplatColorLOD;
        node.schema  = kGaussianSplatColorLODSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = desc.compile;

        if (!system_.register_asset(
                std::move(node), { desc.source_cloud.output })) {
            logger_.error(
                "gaussian splat color LOD: failed to register asset: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    GaussianSplatColorLODHandle
        GaussianSplatColorLODAssetModule::get_lod(
            const GaussianSplatColorLODAsset& asset) const
    {
        if (!asset.valid())
            return {};

        GaussianSplatColorLODHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("gaussian splat color LOD: handle not found");

        return out;
    }

    const GaussianSplatColorLODData*
        GaussianSplatColorLODAssetModule::get_lod_data(
            GaussianSplatColorLODHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}
