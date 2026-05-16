// src/engine/assets/diagnostic_timeframe_summary_asset_module.cpp

#include <engine/assets/diagnostic_timeframe_summary_asset_module.h>

#include <engine/assets/key_factories/diagnostic_timeframe_summary.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    DiagnosticTimeframeSummaryAssetModule::DiagnosticTimeframeSummaryAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        DiagnosticTimeframeSummaryTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    DiagnosticTimeframeSummaryAsset
        DiagnosticTimeframeSummaryAssetModule::create_timeframe_summary(
            const DiagnosticTimeframeSummaryDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("timeframe summary has empty name");
            return {};
        }

        if (!desc.source.valid()) {
            logger_.error("timeframe summary has invalid source: " + desc.name);
            return {};
        }

        if (desc.frame_column.empty()) {
            logger_.error("timeframe summary has empty frame column: " + desc.name);
            return {};
        }

        if (desc.metric_columns.empty()) {
            logger_.error("timeframe summary has no metric columns: " + desc.name);
            return {};
        }

        if (desc.frame_end < desc.frame_start) {
            logger_.error("timeframe summary has inverted frame range: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key = make_diagnostic_timeframe_summary_key(
            desc.name,
            desc.source.output,
            desc.frame_column,
            desc.metric_columns,
            desc.frame_start,
            desc.frame_end,
            desc.frames_per_bucket);

        wz::asset::AssetNode node;
        node.key    = key;
        node.type   = kAssetTypeDiagnosticTimeframeSummary;
        node.schema = kDiagnosticTimeframeSummarySchema;
        node.stage  = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta   = DiagnosticTimeframeSummaryCompileDesc{
            .frame_column      = desc.frame_column,
            .metric_columns    = desc.metric_columns,
            .frame_start       = desc.frame_start,
            .frame_end         = desc.frame_end,
            .frames_per_bucket = desc.frames_per_bucket,
        };

        if (!system_.register_asset(std::move(node), { desc.source.output })) {
            logger_.error(
                "failed to register timeframe summary: " + desc.name);
            return {};
        }

        return DiagnosticTimeframeSummaryAsset{ .output = key };
    }

    DiagnosticTimeframeSummaryHandle
        DiagnosticTimeframeSummaryAssetModule::get_summary(
            const DiagnosticTimeframeSummaryAsset& asset) const
    {
        if (!asset.valid())
            return {};

        DiagnosticTimeframeSummaryHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("timeframe summary handle not found");

        return out;
    }

    const DiagnosticTimeframeSummaryData*
        DiagnosticTimeframeSummaryAssetModule::get_summary_data(
            DiagnosticTimeframeSummaryHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}
