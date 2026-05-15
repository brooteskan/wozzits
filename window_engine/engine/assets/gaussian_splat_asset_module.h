#pragma once

// engine/assets/gaussian_splat_asset_module.h

#include <asset/system.h>
#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <logging/logger.h>

namespace wz::engine::assets
{
    struct GaussianSplatCloudAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct GaussianSplatCloudHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class GaussianSplatAssetModule
    {
    public:
        GaussianSplatAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            GaussianSplatCloudTable& table);

        GaussianSplatCloudAsset create_procedural_cloud(
            const ProceduralGaussianSplatCloudDesc& desc);

        GaussianSplatCloudHandle get_cloud(
            const GaussianSplatCloudAsset& asset) const;

        const GaussianSplatCloudData* get_cloud_data(
            GaussianSplatCloudHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        GaussianSplatCloudTable& table_;
    };
}