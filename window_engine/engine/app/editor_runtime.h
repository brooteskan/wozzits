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
#include <engine/assets/scene/scene_asset_data.h>  // AuthoredTransform
#include <file/filesystem.h>
#include <logging/logging.h>
#include <scene/scene_ecs.h>  // AuthoredEntityId

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace wz::app
{
    // A live scene edit posted from the owner thread (editor UI) to the engine
    // thread. Only a node transform today; this is the seam future live edits
    // (visibility, add/delete/reparent) will extend. Unlike bind() it is
    // fire-and-forget and coalescing — no result crosses back.
    struct SceneNodeTransformEdit
    {
        wz::scene::AuthoredEntityId id;
        wz::engine::assets::AuthoredTransform transform;
    };

    // A live node label/visibility edit (fire-and-forget, coalesced by id) — the
    // properties analogue of SceneNodeTransformEdit.
    struct SceneNodePropertiesEdit
    {
        wz::scene::AuthoredEntityId id;
        std::string name;
        bool visible = true;
    };

    // A live reparent (fire-and-forget, coalesced by id). Empty new_parent_id
    // detaches to the top level.
    struct SceneNodeReparentEdit
    {
        wz::scene::AuthoredEntityId id;
        wz::scene::AuthoredEntityId new_parent_id;
    };

    class EditorRuntimeControl
    {
    public:
        void request_stop();
        [[nodiscard]] bool stop_requested() const;

        // Owner thread: request the engine to persist the scene to disk; the
        // engine thread saves on its next frame. Non-blocking.
        void request_save();
        // Engine thread: consume a pending save request (true once per request).
        [[nodiscard]] bool take_save_request();

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

        // Owner thread: queue a node transform for the engine thread to apply on
        // its next frame. Non-blocking. Coalesces by node id — a drag streams
        // many edits for one node and only the latest matters — so the queue
        // stays small regardless of edit rate.
        void post_scene_node_transform(SceneNodeTransformEdit edit);

        // Engine thread: apply every queued transform via `applier`, then clear
        // the queue. Called once per frame from run_project_runtime, alongside
        // service_pending_bind.
        void service_pending_scene_node_transforms(
            const std::function<void(const SceneNodeTransformEdit&)>& applier);

        // Owner thread: queue a node label/visibility edit (non-blocking,
        // coalesced by id), mirroring post_scene_node_transform.
        void post_scene_node_properties(SceneNodePropertiesEdit edit);

        void service_pending_scene_node_properties(
            const std::function<void(const SceneNodePropertiesEdit&)>& applier);

        // Owner thread: queue a reparent (non-blocking, coalesced by id).
        void post_scene_node_reparent(SceneNodeReparentEdit edit);

        void service_pending_scene_node_reparents(
            const std::function<void(const SceneNodeReparentEdit&)>& applier);

        // Owner thread: queue a node removal (non-blocking; deduped by id).
        void post_scene_node_remove(wz::scene::AuthoredEntityId id);

        void service_pending_scene_node_removes(
            const std::function<
                void(const wz::scene::AuthoredEntityId&)>& applier);

        // Owner thread: add a child node under `parent_id` (empty => top level)
        // in the running scene and block until the engine thread applies it,
        // returning the minted id (or an error). Unlike the transform queue this
        // is a blocking request/response — the caller needs the new id back.
        wz::engine::assets::SceneAddChildResult add_child(
            const wz::scene::AuthoredEntityId& parent_id);

        // Engine thread: if an add-child is pending, run `adder` and publish the
        // result. Called once per frame from run_project_runtime.
        void service_pending_add_child(
            const std::function<wz::engine::assets::SceneAddChildResult(
                const wz::scene::AuthoredEntityId&)>& adder);

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
        std::atomic_bool save_requested_{ false };
        bool has_request_ = false;
        bool has_result_ = false;
        bool finished_ = false;
        wz::asset::AssetGraphDraft pending_draft_;
        wz::asset::AssetGraphDraft result_draft_;
        AssetGraphCompileResult result_;

        // Live scene edits queued for the engine thread (guarded by mutex_,
        // independent of the bind handshake above). Coalesced by node id.
        std::vector<SceneNodeTransformEdit> pending_transforms_;
        std::vector<SceneNodePropertiesEdit> pending_properties_;
        std::vector<SceneNodeReparentEdit> pending_reparents_;
        std::vector<wz::scene::AuthoredEntityId> pending_removes_;

        // Blocking add-child request/response (guarded by mutex_/cv_, mirrors
        // the bind handshake): the owner posts a parent and blocks for the
        // minted-id result the engine thread produces.
        bool has_add_request_ = false;
        bool has_add_result_ = false;
        wz::scene::AuthoredEntityId pending_add_parent_;
        wz::engine::assets::SceneAddChildResult add_result_;
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
