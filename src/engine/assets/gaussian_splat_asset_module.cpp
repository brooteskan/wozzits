// src/engine/assets/gaussian_splat_asset_module.cpp

#include <engine/assets/gaussian_splat_asset_module.h>

#include <engine/assets/key_factories/gaussian_splat.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    GaussianSplatAssetModule::GaussianSplatAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        GaussianSplatCloudTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    GaussianSplatCloudAsset GaussianSplatAssetModule::create_procedural_cloud(
        const ProceduralGaussianSplatCloudDesc& desc)
    {
        GaussianSplatCloudAsset out{};

        if (desc.count == 0) {
            logger_.error("procedural gaussian splat cloud has zero count: " + desc.name);
            return out;
        }

        if (desc.radius <= 0.0f) {
            logger_.error("procedural gaussian splat cloud has non-positive radius: " + desc.name);
            return out;
        }

        if (desc.splat_scale <= 0.0f) {
            logger_.error("procedural gaussian splat cloud has non-positive splat_scale: " + desc.name);
            return out;
        }

        const ProceduralGaussianSplatCloudCompileDesc compile_desc{
            .count = desc.count,
            .radius = desc.radius,
            .splat_scale = desc.splat_scale,
        };

        const wz::asset::AssetKey key =
            make_procedural_gaussian_splat_cloud_key(
                desc.name,
                compile_desc.count,
                compile_desc.radius,
                compile_desc.splat_scale);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeGaussianSplatCloud;
        node.schema = kProceduralGaussianSplatCloudSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = compile_desc;

        if (!system_.register_asset(std::move(node), {})) {
            logger_.error("failed to register procedural gaussian splat cloud: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    GaussianSplatCloudHandle GaussianSplatAssetModule::get_cloud(
        const GaussianSplatCloudAsset& asset) const
    {
        if (!asset.valid())
            return {};

        GaussianSplatCloudHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("gaussian splat cloud handle not found");

        return out;
    }

    const GaussianSplatCloudData* GaussianSplatAssetModule::get_cloud_data(
        GaussianSplatCloudHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}