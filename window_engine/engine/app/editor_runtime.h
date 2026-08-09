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
#include <engine/app/runtime_channel.h>  // Mailbox, Request (#300 layer 1)

#include <asset/draft.h>
#include <engine/assets/scene/scene_asset_data.h>  // AuthoredTransform
#include <file/filesystem.h>
#include <logging/logging.h>
#include <scene/scene_ecs.h>  // AuthoredEntityId
#include <time/w_time.h>  // Tick

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wz::app
{
    // A live scene edit posted from the owner thread (editor UI) to the engine
    // thread. Only a node transform today; this is the seam future live edits
    // (visibility, add/delete/reparent) will extend. Unlike bind_asset_graph() it
    // is fire-and-forget and coalescing — no result crosses back.
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

    // A live scene-tree reorder (fire-and-forget, coalesced by id): move `id` to
    // just before `before_id` in the flat draw list (empty before_id => move to
    // the end). Changes draw order only — nesting is parent_id-based. Coalesced
    // like the reparent queue: a drag streams positions for one node and only the
    // latest destination matters.
    struct SceneNodeReorderEdit
    {
        wz::scene::AuthoredEntityId id;
        wz::scene::AuthoredEntityId before_id;
    };

    // A live render_order (draw-order LAYER) edit (fire-and-forget, coalesced by
    // id — a dropdown/drag streams values, latest wins). Changes which layer a
    // node draws in; the engine re-bakes draw order on apply.
    struct SceneNodeRenderOrderEdit
    {
        wz::scene::AuthoredEntityId id;
        int render_order = 0;
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

    // A live edit to a node's AudioSource renderable reference (the audio analog
    // of SceneNodeRenderableEdit): bind the authored audio-renderable asset-graph
    // node id (0 = clear). Creates the AudioSource component on a non-zero pick.
    // Non-blocking, NOT coalesced.
    struct SceneNodeAudioRenderableEdit
    {
        wz::scene::AuthoredEntityId node_id;
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id = 0;
    };

    // A live edit to an AudioSource's per-entity play policy. Non-blocking, NOT
    // coalesced. No-op if the node has no AudioSource component.
    struct SceneNodeAudioSourcePlayEdit
    {
        wz::scene::AuthoredEntityId node_id;
        bool auto_play = true;
        bool enabled = true;
    };

    // A live edit to one ingredient of a node's renderable BINDING (issue #213
    // increment 2): set/clear (id 0) the GEOMETRY asset-graph node or the
    // RENDER-PROGRAM asset-graph node, posted from the owner thread (editor UI)
    // to the engine thread. Non-blocking and NOT coalesced (appended in order),
    // matching the renderable edits. Applied via WozzitsApp_v1's
    // set_node_geometry_asset / set_node_render_program, which re-assemble the
    // binding (program inherited down the tree) on the engine's next frame.
    struct SceneNodeRenderBindingEdit
    {
        enum class Ingredient : uint8_t
        {
            Geometry = 0,
            RenderProgram,
        };

        wz::scene::AuthoredEntityId node_id;
        Ingredient ingredient = Ingredient::Geometry;
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id = 0;
    };

    // A live edit to ONE semantic resource binding of a node's custom-
    // renderable ingredients (issue #229/#230): upsert the row for `semantic`
    // pointing at the authored asset-graph source (0 = remove the row).
    // Non-blocking and NOT coalesced (appended in order), matching the
    // render-binding edits. Applied via WozzitsApp_v1::
    // set_node_renderable_binding, which re-assembles the node's renderable
    // (a binding present makes it the CUSTOM 0x70A form) on the engine's
    // next frame.
    struct SceneNodeRenderableBindingEdit
    {
        wz::scene::AuthoredEntityId node_id;
        std::string semantic;
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id = 0;
    };

    // A live edit to ONE per-instance constant override of a node's custom-
    // renderable ingredients (issue #229/#230): upsert `name` with `value`
    // (clear = remove the override instead). Non-blocking and NOT coalesced
    // (appended in order). Applied via WozzitsApp_v1::
    // set_node_renderable_constant — a PACK-time merge on the node, so the
    // apply neither re-assembles nor recompiles anything (except the
    // custom-form flip on first-add/last-remove, which the app seam handles).
    struct SceneNodeRenderableParamEdit
    {
        wz::scene::AuthoredEntityId node_id;
        std::string name;
        bool clear = false;
        float value[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
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

    // A live edit to a node's Collision component by REFERENCE (issue #216/#217),
    // posted from the owner thread (editor UI) to the engine thread. Carries the
    // authored asset-graph collision node id (0 = clear) and the constrain_movement
    // toggle. Non-blocking and NOT coalesced (appended in order), matching the
    // render-binding edits. Applied via WozzitsApp_v1::set_node_collision_asset,
    // which re-bridges the reference + rebuilds the runtime scene on the next frame.
    struct SceneNodeCollisionEdit
    {
        wz::scene::AuthoredEntityId node_id;
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id = 0;
        bool constrain_movement = false;
    };

    // A live edit to a node's Motion terrain-stick fields (issue #216/#217),
    // posted from the owner thread (editor UI) to the engine thread. Non-blocking
    // and NOT coalesced (appended in order). Applied via WozzitsApp_v1::
    // set_node_motion_terrain_fields, which rebuilds the runtime scene so
    // integrate_motion + apply_terrain_constraints see the change on the next frame.
    struct SceneNodeMotionTerrainEdit
    {
        wz::scene::AuthoredEntityId node_id;
        bool terrain_constrained = false;
        float ride_height = 0.0f;
        float footprint_radius = 0.0f;
        bool align_to_surface = false;
        float alignment_strength = 1.0f;
    };

    // A live edit to a node's Motion Filter component (the whole component per
    // change — it is small, ~20 fields, so the host sends it all rather than
    // per-field). Non-blocking and coalesced by id (a slider drag streams many;
    // only the latest matters). Applied via WozzitsApp_v1::set_node_motion_filter,
    // which rebuilds the runtime scene so apply_motion_filters sees the change.
    struct SceneNodeMotionFilterEdit
    {
        wz::scene::AuthoredEntityId node_id;
        wz::engine::assets::SceneMotionFilterAsset filter;
    };

    // A live edit to a node's Camera component field values (fov_y / near / far /
    // aspect). Like the Motion Filter edit the whole component is sent per change
    // and coalesced by id (an fov/far drag streams many; only the latest matters).
    // Applied via WozzitsApp_v1::set_node_camera, which rebuilds the runtime scene
    // so the view controller re-reads the camera. This carries editable field
    // VALUES, distinct from the presence-only "camera" SceneNodeComponentEdit token.
    struct SceneNodeCameraEdit
    {
        wz::scene::AuthoredEntityId node_id;
        wz::engine::assets::SceneCameraAsset camera;
    };

    // A live edit to a node's Atmosphere component: which Atmosphere asset-graph
    // node the frame's fog reads (atmosphere_asset_node_id) and whether it is
    // enabled. Coalesced by id like the Camera edit; applied via
    // WozzitsApp_v1::set_node_atmosphere, which rebuilds the runtime scene so the
    // renderer re-resolves the frame atmosphere. The resolved atmosphere_asset key
    // is re-bridged on (re)bind, so only the node id + enabled travel here. This is
    // the value edit, distinct from the presence-only "atmosphere" component token.
    struct SceneNodeAtmosphereEdit
    {
        wz::scene::AuthoredEntityId node_id;
        wz::engine::assets::SceneAtmosphereAsset atmosphere;
    };

    // A live edit to a node's FrameEnvironment component: which FrameEnvironment
    // asset-graph node the frame's environment reads (environment_asset_node_id) +
    // enabled. Coalesced by id like the Atmosphere edit; applied via
    // WozzitsApp_v1::set_node_environment, which rebuilds the runtime scene so the
    // renderer re-resolves the frame environment. The resolved environment_asset
    // key re-bridges on (re)bind, so only the node id + enabled travel here. This is
    // the value edit, distinct from the presence-only "environment" component token.
    struct SceneNodeEnvironmentEdit
    {
        wz::scene::AuthoredEntityId node_id;
        wz::engine::assets::SceneEnvironmentAsset environment;
    };

    // A live edit to a node's RenderToTexture component (issue #287): which
    // Texture asset-graph node the subtree draws into (target_node_id) plus the
    // composition switches. Coalesced by id like the Environment edit; applied via
    // WozzitsApp_v1::set_node_render_to_texture, which re-assembles the render
    // bindings so the target key re-resolves. The resolved target key re-bridges
    // on (re)bind, so only the authored node id + switches travel here. This is
    // the value edit, distinct from the presence-only "render_to_texture"
    // component token.
    struct SceneNodeRenderToTextureEdit
    {
        wz::scene::AuthoredEntityId node_id;
        wz::engine::assets::SceneRenderToTextureAsset render_to_texture;
    };

    // The frame loop's timing rules, as a pure function so they can be tested
    // without a device (#313, B4-S2 and B4-C9).
    //
    // `sampled` is the raw clock read. The returned `now` is what the caller must
    // store as its new `last` — it is NOT always `sampled`, because a clock that
    // failed to advance is corrected forward rather than allowed to produce a
    // zero or (through unsigned subtraction) an astronomically large delta.
    struct FrameDelta
    {
        wz::time::Tick now{};
        float dt = 0.0f;
    };

    // Upper bound on a frame delta. The engine thread's only message pump sits
    // inside DispatchMessage, and the Win32 modal move/size loop blocks there
    // for as long as the user drags the window border, so the next frame's raw
    // delta is unbounded and user-controlled. RhiSceneRenderer::simulation_tick
    // holds the same limit for its animation clock; keep the two equal.
    inline constexpr float kMaxFrameSeconds = 0.25f;

    [[nodiscard]] FrameDelta compute_frame_delta(
        wz::time::Tick last,
        wz::time::Tick sampled,
        uint64_t ticks_per_second);

    class EditorRuntimeControl
    {
    public:
        // ─── Shutdown interlock (issue #313, B4-C12) ────────────────────────
        // wz_host_runtime_stop joins the engine thread and then DELETES the
        // runtime, taking this object (and every Request's own mutex/cv) with
        // it. But join() only proves the ENGINE thread is gone — a different
        // owner thread can still be blocked inside one of the blocking verbs, and
        // that is the normal shape rather than a corner case: the editor runs
        // the graph bind on a .NET threadpool thread so its UI stays live, and
        // the UI thread is the one that calls stop.
        //
        // Every blocking verb opens a CallerScope. stop() calls begin_close()
        // and then wait_for_callers_to_exit() before the delete, so it cannot
        // return while a call is still inside.
        //
        // WHAT THIS DOES NOT COVER, and cannot: a call that has not STARTED
        // when stop() runs. The drain sees no caller, the object is destroyed,
        // and the late call then dereferences a dangling handle. That is the
        // ordinary rule for any handle-based C API — the host must not begin a
        // verb concurrently with stop — and no interlock living inside the
        // object can fix it, because checking the flag already means touching
        // freed memory. In the editor this holds: the graph pane serialises its
        // own operations and stop runs from the UI thread at shutdown.
        class CallerScope
        {
        public:
            explicit CallerScope(EditorRuntimeControl& control) noexcept;
            ~CallerScope();

            CallerScope(const CallerScope&) = delete;
            CallerScope& operator=(const CallerScope&) = delete;

            // False once begin_close() has run: the verb must return its
            // not-running failure immediately, touching nothing else.
            [[nodiscard]] bool admitted() const noexcept { return admitted_; }

        private:
            EditorRuntimeControl* control_ = nullptr;
            bool admitted_ = false;
        };

        // Owner thread, during teardown: refuse new callers and wake the ones
        // already inside. Idempotent.
        void begin_close();

        // Owner thread, during teardown: block until every CallerScope has been
        // released. MUST be called before destroying this object.
        void wait_for_callers_to_exit();

        void request_stop();
        [[nodiscard]] bool stop_requested() const;

        // Owner thread: persist the scene, blocking until the engine thread does
        // it (mirrors open_scene / export_subtree). The outcome carries the
        // engine's wz::fs::FileError, or serviced=false when the runtime is not
        // running / stopped first -- so a save that did not happen is never
        // reported as success (#299 A1-C20). Replaces the old fire-and-forget
        // request_save, whose bool result had nowhere to cross back.
        [[nodiscard]] RequestOutcome<wz::fs::FileError> save_scene_blocking();
        // Engine thread: if a save is pending, run `saver` and publish its
        // FileError. Called once per frame from run_project_runtime.
        void service_pending_save(
            const std::function<wz::fs::FileError()>& saver);

        // Owner thread: enable/disable frame profiling (default off). Applied by
        // the engine thread each frame; the app records + writes a CSV only while
        // enabled, and flushes to its own file on the on->off transition.
        void set_frame_profiling(bool enabled);
        // Engine thread: the current requested profiling state.
        [[nodiscard]] bool frame_profiling_enabled() const;

        // Owner thread: pause/resume the simulation. While paused the engine thread keeps
        // rendering the last frame (the viewport stays live) but skips the per-frame
        // simulation tick, freeing that CPU. Default: not paused.
        void set_paused(bool paused);
        // Engine thread: whether the simulation is currently paused.
        [[nodiscard]] bool paused() const;

        // Owner thread: carve the subtree rooted at `root_node_id` out of the
        // running scene and write it to `out_path` as a standalone prefab
        // scene.json, blocking until the engine thread does it (the prefab
        // milestone). Unlike request_save this is a blocking request/response —
        // like add_child — because the caller needs the success/failure back (the
        // engine owns scene_nodes_ and the write). Returns false if the root id
        // doesn't resolve, the write fails, or the runtime stopped first.
        [[nodiscard]] bool export_subtree_as_scene(
            const wz::scene::AuthoredEntityId& root_node_id,
            const wz::fs::Path& out_path);

        // Engine thread: if an export is pending, run `exporter` and publish the
        // result. Called once per frame from run_project_runtime, alongside the
        // other services.
        void service_pending_export_subtree(
            const std::function<bool(
                const wz::scene::AuthoredEntityId&, const wz::fs::Path&)>&
                exporter);

        // Owner thread: swap the working scene to `scene_path` (the prefab editor's
        // "open a scenelet"). Blocks until the engine thread has loaded it; returns
        // whether it loaded. Reuses the project asset graph + modules.
        [[nodiscard]] bool open_scene(const wz::fs::Path& scene_path);

        // Engine thread: if an open-scene is pending, run `opener` and publish the
        // result. Called once per frame from run_project_runtime.
        void service_pending_open_scene(
            const std::function<bool(const wz::fs::Path&)>& opener);

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

        // Published scenelet (prefab) catalog for the editor's scenelet menu (set
        // after load_scene). Same published-snapshot pattern as behavior_modules:
        // owner thread reads a copy any time; guarded by mutex_.
        void set_scenelets(std::vector<SceneletCatalogEntry> scenelets);
        [[nodiscard]] std::vector<SceneletCatalogEntry> scenelets() const;

        // ─── Dropped-edit reporting ─────────────────────────────────────────
        // The scene-mutation verbs are FIRE-AND-FORGET: they validate the handle
        // and the id string, post, and return OK long before the engine thread
        // resolves the id against the live scene. So when the target turns out to
        // be gone -- after a graph swap, a despawn, or an open_scene -- the miss
        // was a log line and a discarded bool, and the CALLER was told the edit
        // succeeded. That is the mechanism behind the recurring "my edit didn't
        // take" reports (#308, A1-C6).
        //
        // This is the outcome channel. The engine thread records a miss as it
        // applies (where the answer is definitive -- no id set to go stale, so no
        // false positives), the editor drains it and attributes it to the node.
        // Deliberately NOT a blocking handshake per verb: post_scene_node_transform
        // is coalesced by id precisely because the inspector spams it during a
        // drag, and a per-edit round-trip to the engine thread would stutter the UI.
        void record_dropped_edit(
            std::string_view verb,
            std::string_view node_id);

        // Take semantics: each drop is reported once. Bounded, so a drag against a
        // stale id cannot grow this without limit; the last entry says how many
        // were discarded.
        [[nodiscard]] std::vector<std::string> take_dropped_edits();

        // Owner thread: submit an ASSET-GRAPH draft to bind; blocks until the
        // engine thread binds it (or the engine stops). The draft is moved to the
        // engine and the bound draft (with resolved keys + validation) is moved
        // back into `draft` in place - AssetGraphDraft is move-only. Returns the
        // result. #194: this binds the asset GRAPH — it is NOT a scene-edit verb.
        // The scene-editing seams are the post_scene_node_* queues + the blocking
        // add_child / add_node_behavior handshakes below; this one crossing is the
        // graph draft (see the class-level note). The ABI export that reaches it
        // (wz_host_runtime_bind_draft) keeps its C name.
        AssetGraphCompileResult bind_asset_graph(wz::asset::AssetGraphDraft& draft);

        // Engine thread: if an asset-graph bind is pending, run `binder` on the
        // draft and publish the result. Called once per frame from
        // run_project_runtime.
        void service_pending_asset_graph_bind(
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

        // Owner thread: queue a scene-tree reorder (non-blocking, coalesced by
        // id), mirroring post_scene_node_reparent.
        void post_scene_node_reorder(SceneNodeReorderEdit edit);

        void service_pending_scene_node_reorders(
            const std::function<void(const SceneNodeReorderEdit&)>& applier);

        // Owner thread: queue a render_order (draw-layer) edit (non-blocking,
        // coalesced by id), mirroring post_scene_node_reorder.
        void post_scene_node_render_order(SceneNodeRenderOrderEdit edit);

        void service_pending_scene_node_render_orders(
            const std::function<void(const SceneNodeRenderOrderEdit&)>& applier);

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

        // Owner thread: queue a set/clear of a node's AudioSource renderable
        // reference, and a set of its play policy (non-blocking; NOT coalesced;
        // applied on the engine thread's next frame, like the renderable edits).
        void post_scene_node_audio_renderable(SceneNodeAudioRenderableEdit edit);

        void service_pending_scene_node_audio_renderables(
            const std::function<
                void(const SceneNodeAudioRenderableEdit&)>& applier);

        void post_scene_node_audio_source_play(
            SceneNodeAudioSourcePlayEdit edit);

        void service_pending_scene_node_audio_source_plays(
            const std::function<
                void(const SceneNodeAudioSourcePlayEdit&)>& applier);

        // Owner thread: queue a set/clear of one ingredient (geometry or render
        // program) of a node's renderable binding (non-blocking; appended in
        // order — NOT coalesced). Applied on the engine thread's next frame, like
        // the renderable edits (issue #213 increment 2).
        void post_scene_node_render_binding(SceneNodeRenderBindingEdit edit);

        void service_pending_scene_node_render_bindings(
            const std::function<
                void(const SceneNodeRenderBindingEdit&)>& applier);

        // Owner thread: queue an upsert/remove of ONE semantic binding of a
        // node's custom-renderable ingredients (non-blocking; appended in
        // order — NOT coalesced). Applied on the engine thread's next frame,
        // like the render-binding edits (issue #229/#230).
        void post_scene_node_renderable_binding(
            SceneNodeRenderableBindingEdit edit);

        void service_pending_scene_node_renderable_bindings(
            const std::function<
                void(const SceneNodeRenderableBindingEdit&)>& applier);

        // Owner thread: queue an upsert/remove of ONE per-instance constant
        // override of a node's custom-renderable ingredients (non-blocking;
        // appended in order — NOT coalesced; a slider drag streams many edits
        // but each apply is a cheap node-vector write, no rebuild). Applied on
        // the engine thread's next frame (issue #229/#230).
        void post_scene_node_renderable_param(
            SceneNodeRenderableParamEdit edit);

        void service_pending_scene_node_renderable_params(
            const std::function<
                void(const SceneNodeRenderableParamEdit&)>& applier);

        // Owner thread: queue a set/clear of a node's Collision reference +
        // constrain_movement toggle (non-blocking; appended in order — NOT
        // coalesced). Applied on the engine thread's next frame, like the
        // render-binding edits (issue #216/#217).
        void post_scene_node_collision(SceneNodeCollisionEdit edit);

        void service_pending_scene_node_collisions(
            const std::function<void(const SceneNodeCollisionEdit&)>& applier);

        // Owner thread: queue a set of a node's Motion terrain-stick fields
        // (non-blocking; appended in order — NOT coalesced). Applied on the
        // engine thread's next frame, like the collision edits (issue #216/#217).
        void post_scene_node_motion_terrain(SceneNodeMotionTerrainEdit edit);

        void service_pending_scene_node_motion_terrains(
            const std::function<
                void(const SceneNodeMotionTerrainEdit&)>& applier);

        // Owner thread: queue a set of a node's Motion Filter component
        // (non-blocking; coalesced by id -- a slider drag streams many, latest
        // wins). Applied on the engine thread's next frame.
        void post_scene_node_motion_filter(SceneNodeMotionFilterEdit edit);

        void service_pending_scene_node_motion_filters(
            const std::function<
                void(const SceneNodeMotionFilterEdit&)>& applier);

        // Owner thread: queue a set of a node's Camera component field values
        // (non-blocking; coalesced by id, like the Motion Filter edit). Applied on
        // the engine thread's next frame.
        void post_scene_node_camera(SceneNodeCameraEdit edit);

        void service_pending_scene_node_cameras(
            const std::function<
                void(const SceneNodeCameraEdit&)>& applier);

        // Owner thread: queue a set of a node's Atmosphere component values
        // (non-blocking; coalesced by id, like the Camera edit). Applied on the
        // engine thread's next frame.
        void post_scene_node_atmosphere(SceneNodeAtmosphereEdit edit);

        void service_pending_scene_node_atmospheres(
            const std::function<
                void(const SceneNodeAtmosphereEdit&)>& applier);

        // Owner thread: queue a set of a node's FrameEnvironment component values
        // (non-blocking; coalesced by id, like the Atmosphere edit). Applied on the
        // engine thread's next frame.
        void post_scene_node_environment(SceneNodeEnvironmentEdit edit);

        void service_pending_scene_node_environments(
            const std::function<
                void(const SceneNodeEnvironmentEdit&)>& applier);

        // Owner thread: queue a set of a node's RenderToTexture component values
        // (non-blocking; coalesced by id, like the Environment edit). Applied on
        // the engine thread's next frame.
        void post_scene_node_render_to_texture(
            SceneNodeRenderToTextureEdit edit);

        void service_pending_scene_node_render_to_textures(
            const std::function<
                void(const SceneNodeRenderToTextureEdit&)>& applier);

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
        // data back, and the engine thread owns scene_nodes_.
        //
        // std::nullopt means NO ANSWER — the caller was not admitted, or the
        // runtime finished before servicing the request. An empty vector means the
        // engine answered and there is genuinely nothing grafted (D3-P039). These
        // used to be the same value, so a read-back that failed was indistinguishable
        // from an empty result and the editor destroyed a live tree over it.
        std::optional<std::vector<wz::engine::assets::SceneNodeAsset>>
        request_grafted_scene_nodes();

        // Engine thread: if a grafted-nodes request is pending, run `provider`
        // (which copies the live grafted nodes) and publish the result. Called
        // once per frame from run_project_runtime, alongside the other services.
        void service_pending_grafted_scene_nodes(
            const std::function<
                std::vector<wz::engine::assets::SceneNodeAsset>()>& provider);

        // Owner thread: blocking fetch of the running scene's AUTHORED nodes, so
        // the editor can rebuild its tree after open_scene swaps the scene.
        // std::nullopt when there was no answer; see request_grafted_scene_nodes.
        std::optional<std::vector<wz::engine::assets::SceneNodeAsset>>
        request_scene_nodes();

        // Engine thread: if a scene-nodes request is pending, run `provider` (copies
        // the authored scene nodes) and publish the result. Called once per frame.
        void service_pending_scene_nodes(
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
        // Deliberately NOT guarded by mutex_ and NOT signalled through cv_: the
        // whole point is that a departing caller's last act must not touch a
        // member that stop() may already have destroyed. See CallerScope's
        // destructor and wait_for_callers_to_exit().
        std::atomic<int> active_callers_{ 0 };
        std::atomic<bool> closing_{ false };

        mutable std::mutex mutex_;
        std::atomic_bool stop_{ false };
        std::atomic_bool reload_behaviors_requested_{ false };
        std::atomic_bool frame_profiling_{ false };
        std::atomic_bool paused_{ false };
        std::vector<std::string> behavior_modules_;  // guarded by mutex_
        std::vector<SceneletCatalogEntry> scenelets_;  // guarded by mutex_
        std::vector<std::string> dropped_edits_;  // guarded by mutex_
        std::size_t dropped_edits_discarded_ = 0;  // guarded by mutex_
        // Set once when the runtime loop ends (surfaced via finished() /
        // wz_host_runtime_is_running). It no longer drives any wait: every
        // blocking verb now lives on the Request primitive (#300 layer 1) and is
        // released at teardown by abandon(), so the shared cv_ that the seven
        // hand-rolled handshakes and their *_cycle_busy_ gates once needed is
        // gone with them.
        bool finished_ = false;

        // Live scene edits queued for the engine thread, each on its own Mailbox
        // (#300 layer 1). Coalesce-by-node collapses a drag's stream to the latest
        // per node (Replace); the append queues keep every distinct op in order
        // (Keep); remove dedupes by id (Drop).
        Mailbox<SceneNodeTransformEdit> transforms_{
            Mailbox<SceneNodeTransformEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeTransformEdit::id)};
        Mailbox<SceneNodePropertiesEdit> properties_{
            Mailbox<SceneNodePropertiesEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodePropertiesEdit::id)};
        Mailbox<SceneNodeReparentEdit> reparents_{
            Mailbox<SceneNodeReparentEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeReparentEdit::id)};
        Mailbox<SceneNodeReorderEdit> reorders_{
            Mailbox<SceneNodeReorderEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeReorderEdit::id)};
        Mailbox<SceneNodeRenderOrderEdit> render_orders_{
            Mailbox<SceneNodeRenderOrderEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeRenderOrderEdit::id)};
        Mailbox<wz::scene::AuthoredEntityId> removes_{
            Mailbox<wz::scene::AuthoredEntityId>::OnDuplicate::Drop,
            [](const wz::scene::AuthoredEntityId& a,
               const wz::scene::AuthoredEntityId& b) { return a == b; }};
        Mailbox<SceneNodeBehaviorEdit> behaviors_;
        Mailbox<SceneNodeComponentEdit> components_;
        Mailbox<SceneNodeRenderableEdit> renderables_;
        Mailbox<SceneNodeAudioRenderableEdit> audio_renderables_;
        Mailbox<SceneNodeAudioSourcePlayEdit> audio_source_plays_;
        Mailbox<SceneNodeRenderBindingEdit> render_bindings_;
        Mailbox<SceneNodeRenderableBindingEdit> renderable_bindings_;
        Mailbox<SceneNodeRenderableParamEdit> renderable_params_;
        Mailbox<SceneNodeCollisionEdit> collisions_;
        Mailbox<SceneNodeMotionTerrainEdit> motion_terrains_;
        Mailbox<SceneNodeMotionFilterEdit> motion_filters_{
            Mailbox<SceneNodeMotionFilterEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeMotionFilterEdit::node_id)};
        Mailbox<SceneNodeCameraEdit> cameras_{
            Mailbox<SceneNodeCameraEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeCameraEdit::node_id)};
        Mailbox<SceneNodeAtmosphereEdit> atmospheres_{
            Mailbox<SceneNodeAtmosphereEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeAtmosphereEdit::node_id)};
        Mailbox<SceneNodeEnvironmentEdit> environments_{
            Mailbox<SceneNodeEnvironmentEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeEnvironmentEdit::node_id)};
        Mailbox<SceneNodeRenderToTextureEdit> render_to_textures_{
            Mailbox<SceneNodeRenderToTextureEdit>::OnDuplicate::Replace,
            coalesce_by(&SceneNodeRenderToTextureEdit::node_id)};
        Mailbox<SceneNodeSceneSourceEdit> scene_sources_;
        Mailbox<SceneNodeGlbSceneSourceEdit> glb_scene_sources_;
        Mailbox<SceneNodeGlbStyleEdit> glb_styles_;

        // Blocking request/response verbs, each on the Request primitive (#300
        // layer 1). Each has its own mutex/cv, so a publish wakes only its
        // caller (the shared-cv cross-talk that forced the *_cycle_busy_ gate
        // cannot occur), and all are abandon()ed in mark_finished() so a parked
        // caller is released at teardown. The no-answer case is Outcome.serviced
        // == false, distinct from a serviced default value (#313 D3-P039).
        //
        // bind_asset_graph uses Request::call_inout: the move-only draft is moved
        // in (the caller's copy becomes the husk AssetGraphEditorSession's
        // draft-loan expects), the binder mutates it in place, and it is moved
        // back -- bound on success, reclaimed unchanged if the engine stopped or
        // the binder threw (Request's Failed state wakes the caller either way).
        // This replaces the old has_/in_flight_/DraftHandback hand-rolling.
        struct AddBehaviorRequest
        {
            wz::scene::AuthoredEntityId node_id;
            std::string module;
        };
        struct ExportSubtreeRequest
        {
            wz::scene::AuthoredEntityId root;
            wz::fs::Path out_path;
        };
        Request<wz::asset::AssetGraphDraft, AssetGraphCompileResult> asset_graph_;
        Request<wz::scene::AuthoredEntityId,
                wz::engine::assets::SceneAddChildResult> add_child_;
        Request<AddBehaviorRequest,
                wz::engine::assets::SceneAddBehaviorResult> add_behavior_;
        Request<ExportSubtreeRequest, bool> export_subtree_;
        Request<wz::fs::Path, bool> open_scene_;
        Request<std::monostate,
                std::vector<wz::engine::assets::SceneNodeAsset>> grafted_nodes_;
        Request<std::monostate,
                std::vector<wz::engine::assets::SceneNodeAsset>> authored_nodes_;
        Request<std::monostate, wz::fs::FileError> save_;
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

        // Baked disk-cache root for a shipped bundle (issue #334). Empty (the
        // default) leaves the cache OFF, so every existing caller — including the
        // editor's in-process engine, which passes a default RuntimeRunOptions —
        // resolves from source exactly as before. Only the standalone bundle sets
        // it (from its wozzits_app.json `cache` block), so the sealed-cache
        // behavior stays scoped to the shipped app.
        wz::fs::Path cache_root;

        // Seal the cache (read-only, sources stripped): a cacheable asset absent
        // from the cache is a fatal miss naming the key, not a recompile from an
        // absent source. Ignored unless cache_root is set.
        bool cache_sealed = false;
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
