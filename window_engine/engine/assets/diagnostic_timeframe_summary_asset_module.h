#pragma once

// engine/assets/diagnostic_timeframe_summary_asset_module.h

#include <asset/system.h>
#include <engine/assets/data_table_asset_module.h>
#include <engine/assets/diagnostics/diagnostic_timeframe_summary.h>
#include <logging/logger.h>

#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct DiagnosticTimeframeSummaryDesc
    {
        std::string name;

        DataTableAsset source{};

        std::string              frame_column;
        std::vector<std::string> metric_columns;

        uint32_t frame_start       = 0;
        uint32_t frame_end         = 0;
        uint32_t frames_per_bucket = 0;  // 0 = entire range is one bucket
    };

    struct DiagnosticTimeframeSummaryAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct DiagnosticTimeframeSummaryHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class DiagnosticTimeframeSummaryAssetModule
    {
    public:
        DiagnosticTimeframeSummaryAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            DiagnosticTimeframeSummaryTable& table);

        DiagnosticTimeframeSummaryAsset create_timeframe_summary(
            const DiagnosticTimeframeSummaryDesc& desc);

        DiagnosticTimeframeSummaryHandle get_summary(
            const DiagnosticTimeframeSummaryAsset& asset) const;

        const DiagnosticTimeframeSummaryData* get_summary_data(
            DiagnosticTimeframeSummaryHandle handle) const;

    private:
        wz::asset::AssetSystem&          system_;
        wz::Logger&                      logger_;
        DiagnosticTimeframeSummaryTable& table_;
    };
}
