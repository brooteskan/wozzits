#pragma once

// engine/assets/csv_asset_module.h

#include <asset/system.h>
#include <asset/types.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/csv/csv.h>

#include <string>

namespace wz::engine::assets
{
    // ─── CSV asset types ──────────────────────────────────────────────────────────

    struct CSVFileDesc
    {
        std::string  name;
        wz::fs::Path path;                               // relative to resource_root
        CSVHeaderMode header_mode = CSVHeaderMode::Auto;
    };

    struct CSVAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct CSVHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };


    // ─── CSVAssetModule ───────────────────────────────────────────────────────────

    class CSVAssetModule
    {
    public:
        CSVAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger&             logger,
            FileCarrierAssetModule& files,
            CSVTable&               table
        );

        CSVAsset       create_csv(const CSVFileDesc& desc);
        CSVHandle      get_csv(const CSVAsset& asset) const;
        const CSVData* get_csv_data(CSVHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        FileCarrierAssetModule& files_;
        CSVTable&               table_;
    };

} // namespace wz::engine::assets
