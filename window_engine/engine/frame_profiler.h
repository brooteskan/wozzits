#pragma once

// engine/frame_profiler.h
//
// FrameProfiler -- the per-frame structural-work profiler (#252), extracted from
// WozzitsApp_v1 into a reusable engine service (#219 avenue 5). It owns the
// per-frame counters, the opt-in capture buffer, and the CSV flush; a host feeds
// it a few cheap hooks per frame (begin_frame / record_rematerialize /
// record_rebuild / end_frame) and toggles capture. Capture is off by default, so
// a normal editor/play session records nothing and writes no file.

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string>
#include <vector>

namespace wz { struct Logger; }
namespace wz::engine::assets { class EngineAssetLibrary; }

namespace wz::engine
{
    class FrameProfiler
    {
    public:
        // Frame boundary: clear this frame's structural-work accumulators. Call at
        // the top of the sim tick, before any rematerialize/rebuild can happen.
        void begin_frame();

        // Count one render-binding rematerialize this frame and record its call
        // site (the CSV "remat_callers" column). Cheap; always safe to call.
        void record_rematerialize(std::source_location caller);

        // Count one behavior-scene rebuild this frame.
        void record_rebuild();

        // Frame boundary: warn (via `logger`) when this frame did redundant
        // structural work (>1 rematerialize or >1 rebuild), and, while capturing,
        // append a CSV sample. `dt_ms` / `sim_ms` / `scene_nodes` are the host's
        // frame metrics; `frame_index` labels the row.
        void end_frame(
            std::uint64_t frame_index,
            double dt_ms,
            double sim_ms,
            std::size_t scene_nodes,
            wz::Logger& logger);

        // Toggle capture. Enabling starts a fresh capture; disabling flushes what
        // was captured (through `assets`, when present) then resets, so a later
        // enable starts clean. No-op when the state is unchanged.
        void set_enabled(
            bool enabled,
            wz::engine::assets::EngineAssetLibrary* assets,
            wz::Logger& logger);
        [[nodiscard]] bool enabled() const noexcept { return enabled_; }

        // Write the accumulated capture to <resource_root>/frame_profile_<tag>.csv
        // through the data_table -> csv_export chain. No-op without an asset library
        // or any recorded samples. Runs at save / shutdown.
        void flush_csv(
            wz::engine::assets::EngineAssetLibrary* assets,
            wz::Logger& logger);

    private:
        struct Sample
        {
            std::uint64_t frame = 0;
            double        dt_ms = 0.0;
            double        sim_ms = 0.0;
            std::size_t   scene_nodes = 0;
            std::uint32_t rematerialize = 0;
            std::uint32_t rebuild = 0;
            std::string   callers{};  // ";"-joined seam labels for the remat calls
        };

        bool                enabled_ = false;
        std::vector<Sample> samples_{};
        // A wall-clock tag (YYYYMMDD_HHMMSS) minted once per capture so each play
        // session writes its OWN frame_profile_<tag>.csv -- successive play/stop
        // cycles no longer clobber one file (#252).
        std::string         run_tag_{};
        std::uint32_t       rematerialize_count_ = 0;
        std::uint32_t       rebuild_count_ = 0;
        std::string         remat_callers_{};
    };
}
