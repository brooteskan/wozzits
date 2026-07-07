#pragma once

// engine/assets/sky_gaussian_asset_module.h

#include <asset/system.h>
#include <asset/types.h>
#include <engine/assets/sky_gaussian/sky_gaussian_table.h>
#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct SkyGaussianAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct SkyGaussianHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    // Sky-gaussian set compiled from a baked .sky_gaussian.json document. The
    // json_key must name a compiled JSONDocument (create it via
    // JSONAssetModule::create_json). Produces a kAssetTypeSkyGaussian asset.
    struct SkyGaussianFromJSONDesc
    {
        std::string         name;
        wz::asset::AssetKey json_key{};  // from JSONAsset::output
    };

    class SkyGaussianAssetModule
    {
    public:
        SkyGaussianAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            SkyGaussianTable& table);

        SkyGaussianAsset create_from_json(const SkyGaussianFromJSONDesc& desc);

        SkyGaussianHandle get_set(const SkyGaussianAsset& asset) const;

        const sky::SkyGaussianSet* get_set_data(SkyGaussianHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        SkyGaussianTable&       table_;
    };
}
