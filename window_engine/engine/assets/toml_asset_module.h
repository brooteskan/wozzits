#pragma once

// engine/assets/toml_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/toml/toml.h>

#include <string>

namespace wz::engine::assets
{
    struct TOMLFileDesc
    {
        std::string  name;
        wz::fs::Path path; // relative to resource_root
    };

    struct TOMLAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct TOMLHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class TOMLAssetModule
    {
    public:
        TOMLAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            FileCarrierAssetModule& files,
            TOMLTable& table
        );

        TOMLAsset       create_toml(const TOMLFileDesc& desc);
        TOMLHandle      get_toml(const TOMLAsset& asset) const;
        const TOMLData* get_toml_data(TOMLHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        FileCarrierAssetModule& files_;
        TOMLTable& table_;
    };

} // namespace wz::engine::assets