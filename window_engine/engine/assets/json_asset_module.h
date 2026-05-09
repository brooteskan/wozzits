#pragma once

// engine/assets/json_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/json/json.h>

#include <string>

namespace wz::engine::assets
{
    struct JSONFileDesc
    {
        std::string  name;
        wz::fs::Path path; // relative to resource_root
    };

    struct JSONAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct JSONHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class JSONAssetModule
    {
    public:
        JSONAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            FileCarrierAssetModule& files,
            JSONTable& table
        );

        JSONAsset       create_json(const JSONFileDesc& desc);
        JSONHandle      get_json(const JSONAsset& asset) const;
        const JSONData* get_json_data(JSONHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        FileCarrierAssetModule& files_;
        JSONTable& table_;
    };

} // namespace wz::engine::assets