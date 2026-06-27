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
#include <cstdint>
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

    // A live edit to one of a node's behavior bindings, posted from the owner
    // thread to the engine thread (fire-and-forget). Unlike the transform queue
    // these are NOT coalesced: each op (set-enabled, set-fields, set-events,
    // set/clear one config entry, remove the binding) is distinct and must apply
    // in order — coalescing by id would drop a needed mutation. `add` is not
    // here: the host needs the minted id back, so it is a blocking handshake
    // (mirrors add_child) rather than a fire-and-forget post.
    struct SceneNodeBehaviorEdit
    {
        enum class Op : uint8_t
        {
            Remove = 0,
            SetEnabled,
            SetFields,
            SetEvents,
            SetConfig,
            ClearConfig,
        };

        Op op = Op::Remove;
        wz::scene::AuthoredEntityId node_id;
        std::string binding_id;

        // SetEnabled
        bool enabled = true;
        // SetFields
        std::string label;
        std::string module;
        // SetEvents (already parsed from the newline-delimited ABI string)
        std::vector<std::string> events;
        // SetConfig / ClearConfig
        std::string config_key;
        wz::engine::assets::SceneBehaviorConfigValue config_value;
    };

    // A live add/remove of one of a node's optional components, posted from the
    // owner thread (editor UI) to the engine thread (fire-and-forget). `kind` is
    // one of the five editor-managed component tokens ("camera", "renderable",
    // "proximity", "collision", "motion"). Both ops are non-blocking — neither
    // returns a result — so unlike add_node_behavior (which mints an id) this is
    // a plain post, NOT coalesced: an add then a remove of the same kind must
    // apply in order.
    struct SceneNodeComponentEdit
    {
        enum class Op : uint8_t
        {
            Add = 0,
            Remove,
        };

        Op op = Op::Add;
        wz::scene::AuthoredEntityId node_id;
        std::string kind;
    };

    // A live edit to a node's PREFERRED asset-graph-backed Renderable component,
    // posted from the owner thread (editor UI) to the engine thread. Carries the
    // authored asset-graph node id to bind (0 = clear/remove). Non-blocking and
    // NOT coalesced (appended in order), matching the component edits. This only
    // touches renderable_asset_node_id; the legacy embedded renderable slot is
    // never affected (that compat slot is not editor-authorable).
    struct SceneNodeRenderableEdit
    {
        wz::scene::AuthoredEntityId node_id;
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id = 0;
    };

    // A live edit to a node's PREFERRED asset-graph-backed Scene-source
    // component (issue #213), posted from the owner thread (editor UI) to the
    // engine thread. Carries the authored asset-graph node id of a "Scene from
    // GLB" node to bind (0 = clear/remove). Non-blocking and NOT coalesced
    // (appended in order), matching the renderable/component edits. The engine
    // applies it via WozzitsApp_v1::set_node_scene_source, which re-grafts the
    // referenced sub-scene's children under the host on its next frame.
    struct SceneNodeSceneSourceEdit
    {
        // How the engine consumes the reference once set (mirrors the ABI's
        // WZ_SCENE_SOURCE_* tokens): Instance grafts a live sub-tree; Flatten
        // expands once into authored nodes and drops the reference. Ignored when
        // clearing (asset_graph_node_id == 0).
        enum class ConsumeMode : uint8_t
        {
            Instance = 0,
            Flatten,
        };

        wz::scene::AuthoredEntityId node_id;
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id = 0;
        ConsumeMode consume_mode = ConsumeMode::Instance;
    };

    // A live edit to a node's GLB scene-source DESCRIPTOR (issue #213, the
    // asset-graph-INDEPENDENT route), posted from the owner thread (editor UI) to
    // the engine thread. Carries a self-contained GLB descriptor — a resource-
    // relative path + scene_index + consume_mode (an empty `path` = clear). Phase
    // 3a authors a single default style only, so no base_style/style_overrides
    // ride the seam. Non-blocking and NOT coalesced (appended in order), matching
    // the node-ref scene-source edit. The engine applies it via
    // WozzitsApp_v1::set_node_glb_scene_source, which re-resolves the descriptor
    // into a Scene and re-grafts the host's children on its next frame.
    struct SceneNodeGlbSceneSourceEdit
    {
        wz::scene::AuthoredEntityId node_id;
        std::string path;   // empty => clear the descriptor
        uint32_t scene_index = 0;
        wz::engine::assets::SceneSourceConsumeMode consume_mode =
            wz::engine::assets::SceneSourceConsumeMode::Instance;
    };

    // A live edit to the per-component render styling inside a node's GLB
    // scene-source DESCRIPTOR (issue #213 Phase 3b-2), posted from the owner
    // thread (editor UI) to the engine thread. Either sets the base style
    // (is_base), sets a per-mesh-index override, or clears a per-mesh override
    // (clear). The full MeshRenderStyleData rides the seam (built from the ABI's
    // surface/wireframe subset on the host side, defaults elsewhere) — it is
    // trivially copyable, so it crosses the boundary by value. Non-blocking and
    // NOT coalesced (appended in order): two assigns to different meshes, or an
    // assign then a clear, must all land. The engine applies it via the matching
    // WozzitsApp_v1 style mutator, which re-resolves + re-grafts on its next frame.
    struct SceneNodeGlbStyleEdit
    {
        wz::scene::AuthoredEntityId node_id;
        bool is_base = false;       // true => set base; false => per-mesh override
        bool clear = false;         // true => clear the per-mesh override
        uint32_t mesh_index = 0;    // ignored when is_base
        wz::engine::assets::MeshRenderStyleData style{};
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

        // Owner thread: request the engine to reload the project's behavior-
        // module DLLs (after the editor recompiled them) on its next frame.
        // Non-blocking and coalescing (a flag): per-module results are logged.
        void request_reload_behavior_modules();
        // Engine thread: consume a pending reload request (true once per request).
        [[nodiscard]] bool take_reload_behavior_modules_request();

        // Engine thread: publish the names of the currently registered behavior
        // modules (after load_scene and after a reload) so the owner can offer
        // them for binding. Owner thread: read a copy any time. Guarded by mutex_;
        // this is a small, rarely-changing list, so a published snapshot avoids a
        // blocking cross-thread query.
        void set_behavior_modules(std::vector<std::string> modules);
        [[nodiscard]] std::vector<std::string> behavior_modules() const;

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

        // Owner thread: queue a behavior-binding edit (non-blocking). Appended
        // in order — NOT coalesced (every op is a distinct mutation that must
        // land). Applied on the engine thread's next frame, like the other
        // live edits. The add-a-binding op is the separate blocking
        // add_node_behavior() below (it returns a minted id).
        void post_scene_node_behavior(SceneNodeBehaviorEdit edit);

        void service_pending_scene_node_behaviors(
            const std::function<void(const SceneNodeBehaviorEdit&)>& applier);

        // Owner thread: queue an add/remove of an optional node component
        // (non-blocking). Appended in order — NOT coalesced (an add then a
        // remove of the same kind must both land). Applied on the engine
        // thread's next frame, like the other live edits.
        void post_scene_node_component(SceneNodeComponentEdit edit);

        void service_pending_scene_node_components(
            const std::function<void(const SceneNodeComponentEdit&)>& applier);

        // Owner thread: queue a set/clear of a node's preferred asset-graph
        // renderable (non-blocking). Appended in order — NOT coalesced — and
        // applied on the engine thread's next frame, like the component edits.
        void post_scene_node_renderable(SceneNodeRenderableEdit edit);

        void service_pending_scene_node_renderables(
            const std::function<void(const SceneNodeRenderableEdit&)>& applier);

        // Owner thread: queue a set/clear of a node's preferred Scene source
        // (non-blocking; appended in order — NOT coalesced). Applied on the
        // engine thread's next frame, like the renderable edits (issue #213).
        void post_scene_node_scene_source(SceneNodeSceneSourceEdit edit);

        void service_pending_scene_node_scene_sources(
            const std::function<void(const SceneNodeSceneSourceEdit&)>& applier);

        // Owner thread: queue a set/clear of a node's GLB scene-source DESCRIPTOR
        // (non-blocking; appended in order — NOT coalesced). Applied on the engine
        // thread's next frame, like the node-ref scene-source edits (issue #213).
        void post_scene_node_glb_scene_source(SceneNodeGlbSceneSourceEdit edit);

        void service_pending_scene_node_glb_scene_sources(
            const std::function<
                void(const SceneNodeGlbSceneSourceEdit&)>& applier);

        // Owner thread: queue an edit to a node's GLB scene-source per-component
        // STYLE (set base / set per-mesh override / clear per-mesh override),
        // non-blocking; appended in order — NOT coalesced. Applied on the engine
        // thread's next frame, like the GLB descriptor edits (issue #213 3b-2).
        void post_scene_node_glb_style(SceneNodeGlbStyleEdit edit);

        void service_pending_scene_node_glb_styles(
            const std::function<void(const SceneNodeGlbStyleEdit&)>& applier);

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

        // Owner thread: add a behavior binding (the given module) to `node_id`
        // and block until the engine thread applies it, returning the minted
        // binding id (or an error). Like add_child this is a blocking
        // request/response because the host UI needs the new id back — and it is
        // safe BECAUSE the caller is the host's UI thread (the editor), never a
        // behavior running on the engine thread, so the engine thread is free to
        // service it without a self-deadlock.
        wz::engine::assets::SceneAddBehaviorResult add_node_behavior(
            const wz::scene::AuthoredEntityId& node_id,
            const std::string& module);

        // Engine thread: if an add-behavior is pending, run `adder` and publish
        // the result. Called once per frame from run_project_runtime.
        void service_pending_add_node_behavior(
            const std::function<wz::engine::assets::SceneAddBehaviorResult(
                const wz::scene::AuthoredEntityId&, const std::string&)>& adder);

        // Owner thread: request the runtime-only grafted scene nodes (issue
        // #213) and block until the engine thread produces a COPY of them,
        // returning it. Like add_child this is a blocking request/response — the
        // caller (the editor, reading them to merge under their hosts) needs the
        // data back, and the engine thread owns scene_nodes_. Returns an empty
        // vector if the runtime is not running (it never started, or stopped
        // before servicing) so the editor degrades to its JSON tree.
        std::vector<wz::engine::assets::SceneNodeAsset>
        request_grafted_scene_nodes();

        // Engine thread: if a grafted-nodes request is pending, run `provider`
        // (which copies the live grafted nodes) and publish the result. Called
        // once per frame from run_project_runtime, alongside the other services.
        void service_pending_grafted_scene_nodes(
            const std::function<
                std::vector<wz::engine::assets::SceneNodeAsset>()>& provider);

        // Engine thread: mark the runtime done so a blocked bind fails instead
        // of hanging. Called after run_project_runtime returns (incl. the init-
        // failure path where the loop never ran).
        void mark_finished();

        // Any thread: true once the runtime loop has exited (window closed or
        // stop serviced) and mark_finished() ran. The ABI surfaces this as
        // wz_host_runtime_is_running so the editor can detect a closed viewport
        // and offer to restart instead of holding a dead handle.
        [[nodiscard]] bool finished() const;

    private:
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::atomic_bool stop_{ false };
        std::atomic_bool save_requested_{ false };
        std::atomic_bool reload_behaviors_requested_{ false };
        std::vector<std::string> behavior_modules_;  // guarded by mutex_
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
        std::vector<SceneNodeBehaviorEdit> pending_behavior_edits_;
        std::vector<SceneNodeComponentEdit> pending_component_edits_;
        std::vector<SceneNodeRenderableEdit> pending_renderable_edits_;
        std::vector<SceneNodeSceneSourceEdit> pending_scene_source_edits_;
        std::vector<SceneNodeGlbSceneSourceEdit> pending_glb_scene_source_edits_;
        std::vector<SceneNodeGlbStyleEdit> pending_glb_style_edits_;

        // Blocking add-child request/response (guarded by mutex_/cv_, mirrors
        // the bind handshake): the owner posts a parent and blocks for the
        // minted-id result the engine thread produces.
        bool has_add_request_ = false;
        bool has_add_result_ = false;
        wz::scene::AuthoredEntityId pending_add_parent_;
        wz::engine::assets::SceneAddChildResult add_result_;

        // Blocking add-behavior request/response (mirrors the add-child
        // handshake): the owner posts a node id + module and blocks for the
        // minted binding-id result.
        bool has_add_behavior_request_ = false;
        bool has_add_behavior_result_ = false;
        wz::scene::AuthoredEntityId pending_add_behavior_node_;
        std::string pending_add_behavior_module_;
        wz::engine::assets::SceneAddBehaviorResult add_behavior_result_;

        // Blocking grafted-scene-nodes request/response (issue #213, mirrors the
        // add-child handshake but with no request payload — the engine just
        // copies its grafted nodes): the owner sets the request flag and blocks
        // for the copied node list.
        bool has_grafted_request_ = false;
        bool has_grafted_result_ = false;
        std::vector<wz::engine::assets::SceneNodeAsset> grafted_result_;
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

    // Options for a BOUNDED, automatable runtime run (the standalone-render
    // verification / app-shipping seed). Default (max_frames 0) keeps the
    // unbounded interactive loop, so existing callers are unchanged.
    struct RuntimeRunOptions
    {
        // 0  = run until the window closes / stop is requested (interactive).
        // >0 = render exactly this many frames, then exit with a verification
        //      exit code (see run_project_runtime). Used by a separate-process
        //      test that loads a project and asserts it renders.
        uint32_t max_frames = 0;
    };

    // Bounded-mode exit code meaning "the GPU device could not be created", so a
    // separate-process test can treat it as SKIPPED (CTest SKIP_RETURN_CODE) on
    // a machine with no GPU — distinct from a real render failure (1).
    inline constexpr int kRuntimeNoDeviceExitCode = 3;

    // Runs a blocking render loop on the calling thread. `control` may be null
    // (standalone runtime: only closing the window stops it, no binds).
    //
    // Returns a process-style exit code. Unbounded (run_options.max_frames == 0):
    // 0 on clean exit, non-zero on init/runtime failure. Bounded (max_frames>0):
    // 0 only if the scene loaded, at least one renderable resolved, AND all
    // requested frames presented without error; 1 on a load/render failure;
    // kRuntimeNoDeviceExitCode if the device is unavailable.
    //
    // `behavior_module_folder` (the project manifest's behavior_module_folder)
    // is the directory of compiled behavior-module DLLs to load so the scene's
    // behavior bindings run; pass empty for built-in-only behaviors.
    int run_project_runtime(
        const std::string& window_title,
        const wz::fs::Path& asset_graph,
        const wz::fs::Path& scene,
        const wz::fs::Path& resource_root,
        EditorRuntimeControl* control,
        EditorRuntimeLogSink log_sink = {},
        const wz::fs::Path& behavior_module_folder = {},
        const RuntimeRunOptions& run_options = {});
}
