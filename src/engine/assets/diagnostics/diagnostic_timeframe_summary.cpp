// src/engine/assets/diagnostics/diagnostic_timeframe_summary.cpp

#include <engine/assets/diagnostics/diagnostic_timeframe_summary.h>

#include <engine/assets/type_extensions.h>

#include <cstdio>
#include <cstdlib>
#include <string>

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


    // ─── make_data_table ─────────────────────────────────────────────────────────

    DataTableData make_data_table(const DiagnosticTimeframeSummaryData& summary)
    {
        DataTableData out;

        out.columns.push_back({ .name = "bucket_index" });
        out.columns.push_back({ .name = "frame_start"  });
        out.columns.push_back({ .name = "frame_end"    });
        out.columns.push_back({ .name = "row_count"    });

        for (const std::string& m : summary.metric_columns) {
            out.columns.push_back({ .name = m + "_sample_count" });
            out.columns.push_back({ .name = m + "_min"          });
            out.columns.push_back({ .name = m + "_max"          });
            out.columns.push_back({ .name = m + "_mean"         });
            out.columns.push_back({ .name = m + "_first"        });
            out.columns.push_back({ .name = m + "_last"         });
            out.columns.push_back({ .name = m + "_delta"        });
        }

        const uint32_t n = summary.bucket_count();
        out.rows.reserve(n);

        for (uint32_t bi = 0; bi < n; ++bi) {
            const DiagnosticTimeframeBucket& b = summary.buckets[bi];

            DataTableRow row;
            row.cells.reserve(out.columns.size());

            row.cells.push_back(std::to_string(bi));
            row.cells.push_back(std::to_string(b.frame_start));
            row.cells.push_back(std::to_string(b.frame_end));
            row.cells.push_back(std::to_string(b.row_count));

            char buf[32];
            for (const DiagnosticTimeframeMetricStats& s : b.metrics) {
                row.cells.push_back(std::to_string(s.sample_count));

                auto push = [&](double v) {
                    std::snprintf(buf, sizeof(buf), "%.6g", v);
                    row.cells.emplace_back(buf);
                };

                push(s.min);
                push(s.max);
                push(s.mean);
                push(s.first);
                push(s.last);
                push(s.delta);
            }

            out.rows.push_back(std::move(row));
        }

        return out;
    }
}
