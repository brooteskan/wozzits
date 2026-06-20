#pragma once

// engine/app/editor_runtime.h
//
// run_project_runtime — the WozzitsApp_v1 runtime loop (init device+window,
// load the scene+graph, render until the window closes or should_stop()). It is
// the single implementation shared by the standalone runtime executable
// (src/app/wozzits_app_v1) and the editor's in-process engine ABI (Option Y,
// issue #189), so the loop is not duplicated. The caller owns the thread; the
// engine owns its window + device for the duration of the call.

#include <file/filesystem.h>

#include <functional>
#include <string>

namespace wz::app
{
    // Runs a blocking render loop on the calling thread. should_stop may be
    // empty (then only closing the window ends the loop). Returns a process-
    // style exit code (0 = ok, non-zero = init/runtime failure).
    int run_project_runtime(
        const std::string& window_title,
        const wz::fs::Path& asset_graph,
        const wz::fs::Path& scene,
        const wz::fs::Path& resource_root,
        const std::function<bool()>& should_stop);
}
