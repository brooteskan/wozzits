#pragma once

// engine/assets/csv_export_asset_module.h

#include <asset/system.h>
#include <engine/assets/csv_export/csv_export.h>
#include <logging/logger.h>

namespace wz::engine::assets
{
    struct CSVExportAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct CSVExportHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class CSVExportAssetModule
    {
    public:
        CSVExportAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            CSVExportTable& table);

        CSVExportAsset create_csv_export(
            const CSVExportDesc& desc);

        CSVExportHandle get_export(
            const CSVExportAsset& asset) const;

        const CSVExportData* get_export_data(
            CSVExportHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        CSVExportTable& table_;
    };
}
