// src/engine/frame_profiler.cpp

#include <engine/frame_profiler.h>

#include <engine/assets/data_table_csv_export.h>
#include <engine/assets/engine_asset_library.h>
#include <file/filesystem.h>
#include <logging/logger.h>

#include <ctime>
#include <string>

namespace wz::engine
{
    namespace
    {
        // Extract a short seam identifier from a source_location function name --
        // "void wz::app::WozzitsApp_v1::set_node_render_program(...)" -> the bare
        // "set_node_render_program". CSV-safe (a bare identifier: no comma) and
        // stable across line shifts, so it names WHICH call site forced a
        // rematerialize in the frame_profile "remat_callers" column (#252).
        std::string short_render_caller(const char* fn)
        {
            std::string s = fn ? fn : "?";
            if (const std::size_t paren = s.find('('); paren != std::string::npos) {
                s.resize(paren);
            }
            if (const std::size_t colons = s.rfind("::");
                colons != std::string::npos) {
                s.erase(0, colons + 2);
            }
            if (const std::size_t space = s.rfind(' '); space != std::string::npos) {
                s.erase(0, space + 1);  // drop any leftover return-type prefix
            }
            return s;
        }
    }

    void FrameProfiler::begin_frame()
    {
        rematerialize_count_ = 0;
        rebuild_count_ = 0;
        remat_callers_.clear();
    }

    void FrameProfiler::record_rematerialize(std::source_location caller)
    {
        ++rematerialize_count_;
        // Accumulate a short caller label per frame for the frame_profile
        // "remat_callers" CSV column -- the CSV is the artifact the user hands
        // back (the editor never writes the play log to a file), so the
        // WHICH-seam answer to the spurious burst must live in the CSV (#252).
        if (!remat_callers_.empty()) {
            remat_callers_ += ";";
        }
        remat_callers_ += short_render_caller(caller.function_name());
    }

    void FrameProfiler::record_rebuild()
    {
        ++rebuild_count_;
    }

    void FrameProfiler::end_frame(
        std::uint64_t frame_index,
        double dt_ms,
        double sim_ms,
        std::size_t scene_nodes,
        wz::Logger& logger)
    {
        if (rematerialize_count_ > 1 || rebuild_count_ > 1) {
            logger.warn(
                "[perf] frame " + std::to_string(frame_index)
                + ": redundant structural rebuilds -- rematerialize x"
                + std::to_string(rematerialize_count_)
                + " rebuild_behavior_scene x"
                + std::to_string(rebuild_count_)
                + " (" + std::to_string(sim_ms) + " ms)");
        }
        if (enabled_ && samples_.size() < 200000u) {
            samples_.push_back(Sample{
                .frame = frame_index,
                .dt_ms = dt_ms,
                .sim_ms = sim_ms,
                .scene_nodes = scene_nodes,
                .rematerialize = rematerialize_count_,
                .rebuild = rebuild_count_,
                .callers = remat_callers_,
            });
        }
    }

    void FrameProfiler::set_enabled(
        bool enabled,
        wz::engine::assets::EngineAssetLibrary* assets,
        wz::Logger& logger)
    {
        if (enabled == enabled_) {
            return;
        }
        if (enabled) {
            // Start a fresh capture. The run tag is minted at flush time, so a
            // cleared tag + empty buffer means this on->off session writes its
            // OWN frame_profile_<tag>.csv.
            samples_.clear();
            run_tag_.clear();
            enabled_ = true;
            logger.info("frame profiling: enabled");
        }
        else {
            // Flush what was captured to its own file, then stop and reset so a
            // later enable starts clean.
            enabled_ = false;
            flush_csv(assets, logger);
            samples_.clear();
            run_tag_.clear();
            logger.info("frame profiling: disabled");
        }
    }

    void FrameProfiler::flush_csv(
        wz::engine::assets::EngineAssetLibrary* assets,
        wz::Logger& logger)
    {
        if (!assets || samples_.empty()) {
            return;
        }

        // Mint a wall-clock run tag ONCE per capture so each play session writes
        // its OWN frame_profile_<tag>.csv. Successive play/stop cycles are separate
        // host processes that all flushed one fixed filename before, so a later
        // (e.g. no-spawn) run silently clobbered an earlier spawn run (#252).
        if (run_tag_.empty()) {
            const std::time_t now = std::time(nullptr);
            std::tm lt{};
#ifdef _WIN32
            localtime_s(&lt, &now);
#else
            localtime_r(&now, &lt);
#endif
            char stamp[24] = {};
            std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &lt);
            run_tag_ = stamp;
        }

        // Rows = frames, columns = the recorded metrics. The schema is
        // app-specific (the #252 structural-work metrics); the shared engine helper
        // owns the data_table -> csv_export -> file plumbing. Runs at save/shutdown.
        wz::engine::assets::DataTableData table;
        table.columns.push_back({ .name = "frame" });
        table.columns.push_back({ .name = "dt_ms" });
        table.columns.push_back({ .name = "sim_ms" });
        table.columns.push_back({ .name = "scene_nodes" });
        table.columns.push_back({ .name = "rematerialize" });
        table.columns.push_back({ .name = "rebuild_behavior_scene" });
        table.columns.push_back({ .name = "remat_callers" });
        table.rows.reserve(samples_.size());
        for (const Sample& s : samples_) {
            table.rows.push_back({ .cells = {
                std::to_string(s.frame),
                std::to_string(s.dt_ms),
                std::to_string(s.sim_ms),
                std::to_string(s.scene_nodes),
                std::to_string(s.rematerialize),
                std::to_string(s.rebuild),
                s.callers,
            } });
        }

        const wz::fs::Path path =
            wz::fs::join(
                assets->resource_root(),
                "frame_profile_" + run_tag_ + ".csv");

        using Status = wz::engine::assets::DataTableCsvExportStatus;
        switch (wz::engine::assets::write_data_table_to_csv_file(
                    *assets, "profile/frame_profile", std::move(table), path))
        {
        case Status::TableInvalid:
            logger.warn("flush_frame_profile_csv: data table invalid");
            break;
        case Status::ExportInvalid:
            logger.warn("flush_frame_profile_csv: csv export invalid");
            break;
        case Status::ExportUnresolved:
            logger.warn("flush_frame_profile_csv: csv export unresolved");
            break;
        case Status::WriteFailed:
            logger.warn("flush_frame_profile_csv: write failed for " + path);
            break;
        case Status::Ok:
            logger.info(
                "flush_frame_profile_csv: wrote " + path + " ("
                + std::to_string(samples_.size()) + " frames)");
            break;
        }
    }
}
