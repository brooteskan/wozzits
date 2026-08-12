// tests/engine/frame_profiler_tests.cpp
//
// Unit coverage for the per-frame structural-work profiler (FrameProfiler, #252
// / #219 avenue 5). The live host feeds it hooks from the frame loop, which needs
// a GPU device and is not unit-tested; this suite carries the device-free logic:
// the redundant-work warning, the begin_frame counter reset, and the opt-in
// capture -> CSV flush chain (including that capture is OFF by default, so a
// normal session writes nothing). The CSV flush runs through the real
// data_table -> csv_export asset chain with a default (device-free) GPU device,
// the same construction the data_table_csv_export suite uses.

#include <gtest/gtest.h>

#include <engine/frame_profiler.h>

#include <engine/assets/engine_asset_library.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>
#include <logging/logging.h>

#include <source_location>
#include <string>
#include <utility>
#include <vector>

using wz::engine::FrameProfiler;

// A named seam so record_rematerialize's caller label is a known, stable
// identifier we can assert on in the CSV "remat_callers" column. Deliberately at
// namespace scope (not an anonymous namespace): short_render_caller truncates at
// the first '(', and an anonymous-namespace function name carries a literal
// "(anonymous namespace)" that would swallow the identifier -- production callers
// are WozzitsApp_v1 member functions, which this mirrors.
namespace seam
{
    void record_from_named_seam(FrameProfiler& profiler)
    {
        profiler.record_rematerialize(std::source_location::current());
    }
}

namespace
{
    // ── log capture ─────────────────────────────────────────────────────────
    //
    // FrameProfiler reports the redundant-work warning and the flush result
    // through a wz::Logger, so a real (async) logger with a capturing sink is the
    // only way to observe them. Mirrors the capture harness in
    // tests/diagnostics/logger_service_tests.cpp.
    struct CapturedLog
    {
        std::vector<std::pair<wz::LogLevel, std::string>> lines;

        [[nodiscard]] bool has(wz::LogLevel level, std::string_view needle) const
        {
            for (const auto& [lvl, text] : lines) {
                if (lvl == level && text.find(needle) != std::string::npos) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::size_t count(wz::LogLevel level) const
        {
            std::size_t n = 0;
            for (const auto& [lvl, text] : lines) {
                if (lvl == level) {
                    ++n;
                }
            }
            return n;
        }
    };

    void capture_sink(const wz::logging::LogRecordView& record, void* user)
    {
        auto* cap = static_cast<CapturedLog*>(user);
        cap->lines.emplace_back(
            record.level, std::string(record.text, record.text_size));
    }

    // A logger whose output is captured into `cap`, torn down on scope exit.
    struct CapturingLogger
    {
        wz::Logger   logger;
        CapturedLog& cap;

        explicit CapturingLogger(CapturedLog& c) : cap(c)
        {
            EXPECT_TRUE(wz::logging::init_logger(logger, {}));
            wz::logging::set_log_sink(logger, capture_sink, &cap);
        }

        ~CapturingLogger()
        {
            wz::logging::wait_until_idle(logger);
            wz::logging::shutdown_logger(logger);
        }

        // Drain pending async records so `cap` is up to date before asserting.
        void sync() { wz::logging::wait_until_idle(logger); }
    };

    // ── filesystem helpers ──────────────────────────────────────────────────
    // A per-test temp root, purged of any frame_profile_*.csv left by an earlier
    // run -- the flush filename is wall-clock-timestamped and accumulates in the
    // reused temp dir otherwise, which would break the per-test file-count
    // assertions below.
    wz::fs::Path make_root(const std::string& leaf)
    {
        const wz::fs::Path root =
            wz::fs::join(wz::fs::temp_directory_path(), leaf);
        EXPECT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

        const auto entries = wz::fs::list_directory(root);
        if (entries.error == wz::fs::FileError::None) {
            for (const wz::fs::DirEntry& e : entries.value) {
                if (!e.is_directory && e.name.rfind("frame_profile_", 0) == 0) {
                    wz::fs::remove_file(wz::fs::join(root, e.name));
                }
            }
        }
        return root;
    }

    // The flush mints a wall-clock run tag, so the file is frame_profile_<tag>.csv
    // -- find the single one in a per-test-unique root.
    std::vector<std::string> profile_csvs(const wz::fs::Path& root)
    {
        std::vector<std::string> out;
        const auto entries = wz::fs::list_directory(root);
        EXPECT_EQ(entries.error, wz::fs::FileError::None);
        for (const wz::fs::DirEntry& e : entries.value) {
            if (!e.is_directory
                && e.name.rfind("frame_profile_", 0) == 0
                && e.name.size() >= 4
                && e.name.substr(e.name.size() - 4) == ".csv") {
                out.push_back(e.name);
            }
        }
        return out;
    }

    constexpr char kHeader[] =
        "frame,dt_ms,sim_ms,scene_nodes,rematerialize,"
        "rebuild_behavior_scene,remat_callers\r\n";
}

// ── the redundant-work warning (device-free, logger only) ───────────────────

TEST(FrameProfiler, WarnsWhenAFrameRematerializesMoreThanOnce)
{
    CapturedLog cap;
    {
        CapturingLogger log(cap);

        FrameProfiler profiler;
        profiler.begin_frame();
        profiler.record_rematerialize(std::source_location::current());
        profiler.record_rematerialize(std::source_location::current());
        profiler.end_frame(/*frame=*/7, /*dt_ms=*/16.0, /*sim_ms=*/2.0,
                           /*scene_nodes=*/10, log.logger);
        log.sync();
    }

    EXPECT_TRUE(cap.has(wz::LogLevel::Warning, "rematerialize x2"));
    EXPECT_TRUE(cap.has(wz::LogLevel::Warning, "frame 7"));
}

TEST(FrameProfiler, WarnsWhenAFrameRebuildsMoreThanOnce)
{
    CapturedLog cap;
    {
        CapturingLogger log(cap);

        FrameProfiler profiler;
        profiler.begin_frame();
        profiler.record_rebuild();
        profiler.record_rebuild();
        profiler.end_frame(3, 16.0, 2.0, 5, log.logger);
        log.sync();
    }

    EXPECT_TRUE(cap.has(wz::LogLevel::Warning, "rebuild_behavior_scene x2"));
}

TEST(FrameProfiler, SingleUnitOfWorkPerFrameDoesNotWarn)
{
    CapturedLog cap;
    {
        CapturingLogger log(cap);

        FrameProfiler profiler;
        profiler.begin_frame();
        profiler.record_rematerialize(std::source_location::current());
        profiler.record_rebuild();
        profiler.end_frame(1, 16.0, 2.0, 5, log.logger);
        log.sync();
    }

    // Exactly one of each is the normal, non-redundant case -- the threshold is
    // strictly > 1.
    EXPECT_EQ(cap.count(wz::LogLevel::Warning), 0u);
}

TEST(FrameProfiler, BeginFrameResetsTheRedundancyCounters)
{
    CapturedLog cap;
    {
        CapturingLogger log(cap);

        FrameProfiler profiler;

        // First frame accrues two rematerializes...
        profiler.begin_frame();
        profiler.record_rematerialize(std::source_location::current());
        profiler.record_rematerialize(std::source_location::current());
        // ...but a new begin_frame must clear them before end_frame sees them.
        profiler.begin_frame();
        profiler.record_rematerialize(std::source_location::current());
        profiler.end_frame(2, 16.0, 2.0, 5, log.logger);
        log.sync();
    }

    EXPECT_EQ(cap.count(wz::LogLevel::Warning), 0u);
}

// ── capture is opt-in ───────────────────────────────────────────────────────

TEST(FrameProfiler, CaptureIsOffByDefaultAndFlushWritesNothing)
{
    const wz::fs::Path root = make_root("wozzits_frame_profiler_off_tests");

    wz::Logger           logger;   // default (null) logger: log calls no-op
    wz::gpu::Device      device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    FrameProfiler profiler;
    EXPECT_FALSE(profiler.enabled());

    // Run frames without enabling capture; none should be retained.
    for (std::uint64_t f = 0; f < 3; ++f) {
        profiler.begin_frame();
        profiler.record_rematerialize(std::source_location::current());
        profiler.end_frame(f, 16.0, 2.0, 5, logger);
    }
    profiler.flush_csv(&assets, logger);

    EXPECT_TRUE(profile_csvs(root).empty())
        << "capture off must write no frame_profile CSV";
}

TEST(FrameProfiler, EnableCapturesFramesAndFlushWritesACsv)
{
    const wz::fs::Path root = make_root("wozzits_frame_profiler_capture_tests");

    CapturedLog cap;
    CapturingLogger log(cap);
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, log.logger, root };

    FrameProfiler profiler;
    profiler.set_enabled(true, &assets, log.logger);
    EXPECT_TRUE(profiler.enabled());

    // Three captured frames; the middle one carries a named remat seam so the
    // "remat_callers" column has a known label to assert on.
    for (std::uint64_t f = 0; f < 3; ++f) {
        profiler.begin_frame();
        if (f == 1) {
            seam::record_from_named_seam(profiler);
        }
        profiler.end_frame(f, 16.0, 2.0, /*scene_nodes=*/42, log.logger);
    }

    profiler.flush_csv(&assets, log.logger);
    log.sync();

    const std::vector<std::string> csvs = profile_csvs(root);
    ASSERT_EQ(csvs.size(), 1u);

    const wz::fs::Path out = wz::fs::join(root, csvs.front());
    const auto text = wz::fs::read_file_text(out);
    ASSERT_TRUE(text);

    // Header shape is the #252 structural-work schema, comma-separated, CRLF.
    EXPECT_EQ(text.value.rfind(kHeader, 0), 0u) << text.value;
    // One line per captured frame plus the header (each row ends CRLF).
    std::size_t newlines = 0;
    for (const char c : text.value) {
        if (c == '\n') {
            ++newlines;
        }
    }
    EXPECT_EQ(newlines, 4u) << text.value;  // header + 3 frames

    // The named seam's short identifier reached the CSV (short_render_caller
    // strips namespaces/params down to the bare function name).
    EXPECT_NE(text.value.find("record_from_named_seam"), std::string::npos)
        << text.value;
    // The scene_nodes metric round-tripped into the rows.
    EXPECT_NE(text.value.find(",42,"), std::string::npos) << text.value;

    // The Ok path logs the frame count it wrote.
    EXPECT_TRUE(cap.has(wz::LogLevel::Info, "3 frames"));
}

TEST(FrameProfiler, DisableFlushesThenResetsTheBuffer)
{
    const wz::fs::Path root = make_root("wozzits_frame_profiler_cycle_tests");

    wz::Logger           logger;
    wz::gpu::Device      device{};
    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    FrameProfiler profiler;

    // First capture: two frames, then disable -- which flushes and resets.
    profiler.set_enabled(true, &assets, logger);
    for (std::uint64_t f = 0; f < 2; ++f) {
        profiler.begin_frame();
        profiler.end_frame(f, 16.0, 2.0, 5, logger);
    }
    profiler.set_enabled(false, &assets, logger);
    EXPECT_FALSE(profiler.enabled());

    std::vector<std::string> after_first = profile_csvs(root);
    ASSERT_EQ(after_first.size(), 1u)
        << "disable must flush the captured frames to their own file";
    {
        const auto text = wz::fs::read_file_text(
            wz::fs::join(root, after_first.front()));
        ASSERT_TRUE(text);
        std::size_t newlines = 0;
        for (const char c : text.value) {
            if (c == '\n') {
                ++newlines;
            }
        }
        EXPECT_EQ(newlines, 3u) << "header + 2 frames\n" << text.value;
    }

    // Second capture: a single frame. If disable truly reset the buffer, the
    // flushed file holds exactly one frame -- not the earlier two as well. The
    // run tag has one-second resolution, so this second flush may reuse or mint a
    // new filename depending on wall-clock timing; assert on CONTENT, not the
    // file count -- find the file carrying frame 99 and confirm it has just the
    // one row.
    profiler.set_enabled(true, &assets, logger);
    profiler.begin_frame();
    profiler.end_frame(99, 16.0, 2.0, 5, logger);
    profiler.flush_csv(&assets, logger);

    std::string second_text;
    for (const std::string& name : profile_csvs(root)) {
        const auto text = wz::fs::read_file_text(wz::fs::join(root, name));
        ASSERT_TRUE(text);
        if (text.value.find("99,") != std::string::npos) {
            second_text = text.value;
        }
    }
    ASSERT_FALSE(second_text.empty())
        << "the re-enabled capture must have flushed frame 99";

    std::size_t newlines = 0;
    for (const char c : second_text) {
        if (c == '\n') {
            ++newlines;
        }
    }
    EXPECT_EQ(newlines, 2u) << "header + 1 frame (buffer was reset, not "
                               "carrying the earlier two)\n"
                            << second_text;
}

TEST(FrameProfiler, FlushWithoutAssetLibraryIsANoOp)
{
    wz::Logger logger;

    FrameProfiler profiler;
    // No EngineAssetLibrary: flush has nowhere to write and must not crash.
    profiler.flush_csv(nullptr, logger);
    SUCCEED();
}
