// src/engine/assets/diagnostics/diagnostic_timeframe_summary.cpp

#include <engine/assets/diagnostics/diagnostic_timeframe_summary.h>

#include <engine/assets/type_extensions.h>

#include <cstdio>

namespace wz::engine::assets
{
    // ─── DiagnosticTimeframeSummaryData ──────────────────────────────────────────

    bool DiagnosticTimeframeSummaryData::valid() const noexcept
    {
        return !frame_column.empty()
            && !metric_columns.empty()
            && frame_end >= frame_start;
    }

    uint32_t DiagnosticTimeframeSummaryData::bucket_count() const noexcept
    {
        return static_cast<uint32_t>(buckets.size());
    }

    uint32_t DiagnosticTimeframeSummaryData::metric_count() const noexcept
    {
        return static_cast<uint32_t>(metric_columns.size());
    }


    // ─── DiagnosticTimeframeSummaryTable ─────────────────────────────────────────

    DiagnosticTimeframeSummaryTable::DiagnosticTimeframeSummaryTable()
    {
        summaries_.emplace_back();
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle DiagnosticTimeframeSummaryTable::add(
        DiagnosticTimeframeSummaryData data)
    {
        const uint32_t id = static_cast<uint32_t>(summaries_.size());
        summaries_.push_back(std::move(data));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id    = id,
            .epoch = epochs_[id],
            .type  = kAssetTypeDiagnosticTimeframeSummary,
        };
    }

    const DiagnosticTimeframeSummaryData* DiagnosticTimeframeSummaryTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeDiagnosticTimeframeSummary)
            return nullptr;

        if (handle.id >= summaries_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &summaries_[handle.id];
    }

    void DiagnosticTimeframeSummaryTable::destroy()
    {
        summaries_.clear();
        epochs_.clear();

        summaries_.emplace_back();
        epochs_.push_back(0);
    }
}
