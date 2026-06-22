#pragma once

// engine/app/editor_runtime.h
//
// run_project_runtime - the WozzitsApp_v1 runtime loop (init device+window,
// load the scene+graph, render until the window closes or stop is requested).
// It is the single implementation shared by the standalone runtime executable
// (src/app/wozzits_app_v1) and the editor's in-process engine ABI (Option Y,
// issue #189), so the loop is not duplicated.
//
// EditorRuntimeControl is the cross-thread seam: the owner (the editor ABI, on
// the UI thread) posts an AssetGraphDraft to bind and blocks for the result;
// the engine thread services the request inside its render loop. The
// AssetGraphDraft is the only thing that crosses the thread boundary.

#include <engine/app/wozzits_app_v1.h>  // AssetGraphCompileResult

#include <asset/draft.h>
#include <file/filesystem.h>
#include <logging/logging.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace wz::app
{
    class EditorRuntimeControl
    {
    public:
        void request_stop();
        [[nodiscard]] bool stop_requested() const;

        // Owner thread: submit a draft to bind; blocks until the engine thread
        // binds it (or the engine stops). The draft is moved to the engine and
        // the bound draft (with resolved keys + validation) is moved back into
        // `draft` in place - AssetGraphDraft is move-only. Returns the result.
        AssetGraphCompileResult bind(wz::asset::AssetGraphDraft& draft);

        // Engine thread: if a bind is pending, run `binder` on the draft and
        // publish the result. Called once per frame from run_project_runtime.
        void service_pending_bind(
            const std::function<
                AssetGraphCompileResult(wz::asset::AssetGraphDraft&)>& binder);

        // Engine thread: mark the runtime done so a blocked bind fails instead
        // of hanging. Called after run_project_runtime returns (incl. the init-
        // failure path where the loop never ran).
        void mark_finished();

        // Any thread: true once the runtime loop has exited (window closed or
        // stop serviced) and mark_finished() ran. The ABI surfaces this as
        // wz_editor_runtime_is_running so the editor can detect a closed viewport
        // and offer to restart instead of holding a dead handle.
        [[nodiscard]] bool finished() const;

    private:
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic_bool stop_{ false };
        bool has_request_ = false;
        bool has_result_ = false;
        bool finished_ = false;
        wz::asset::AssetGraphDraft pending_draft_;
        wz::asset::AssetGraphDraft result_draft_;
        AssetGraphCompileResult result_;
    };

    struct EditorRuntimeLogSink
    {
        void (*write)(
            wz::LogLevel level,
            std::string_view timestamp,
            std::string_view text,
            void* user) = nullptr;
        void* user = nullptr;
    };

    // Runs a blocking render loop on the calling thread. `control` may be null
    // (standalone runtime: only closing the window stops it, no binds). Returns
    // a process-style exit code (0 = ok, non-zero = init/runtime failure).
    int run_project_runtime(
        const std::string& window_title,
        const wz::fs::Path& asset_graph,
        const wz::fs::Path& scene,
        const wz::fs::Path& resource_root,
        EditorRuntimeControl* control,
        EditorRuntimeLogSink log_sink = {});
}
