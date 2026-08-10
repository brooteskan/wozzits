// src/engine/app/editor_runtime.cpp

#include <engine/app/editor_runtime.h>

#include <engine/app_context.h>
#include <engine/frame_schedule.h>

#include <tasks/task_scheduler.h>
#include <io/io_executor.h>
#include <async/async.h>
#include <diagnostics/logger_service.h>
#include <gpu/gpu_diagnostics.h>

#include <event/event.h>
#include <gpu/gpu.h>
#include <input/input.h>
#include <platform/win32/controller_win32.h>
#include <logging/logger.h>
#include <time/w_time.h>
#include <window/window2.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wz::app
{
    namespace
    {

        AssetGraphCompileResult bind_failed(const std::string& message)
        {
            AssetGraphCompileResult result;
            result.ok = false;
            wz::asset::AssetGraphDraftValidationMessage diagnostic;
            diagnostic.severity =
                wz::asset::AssetGraphDraftValidationSeverity::Error;
            diagnostic.message = message;
            result.diagnostics.push_back(std::move(diagnostic));
            return result;
        }

        void forward_editor_runtime_log(
            const wz::logging::LogRecordView& record,
            void* user)
        {
            auto* sink = static_cast<EditorRuntimeLogSink*>(user);
            if (!sink || !sink->write) {
                return;
            }

            sink->write(
                record.level,
                std::string_view{
                    record.timestamp ? record.timestamp : "",
                    record.timestamp_size,
                },
                std::string_view{
                    record.text ? record.text : "",
                    record.text_size,
                },
                sink->user);
        }
    }

    void EditorRuntimeControl::request_stop()
    {
        // The frame loop polls stop_requested(); nothing blocks on a cv here any
        // more (the hand-rolled handshakes that did are gone with #300 layer 1),
        // so a bare store is all this needs.
        stop_.store(true, std::memory_order_release);
    }

    bool EditorRuntimeControl::stop_requested() const
    {
        return stop_.load(std::memory_order_acquire);
    }

    RequestOutcome<wz::fs::FileError> EditorRuntimeControl::save_scene_blocking()
    {
        // Same lifetime interlock as the other blocking verbs: CallerScope keeps
        // the object alive across the wait; begin_close()/mark_finished() abandon
        // save_ so a parked caller is released. The scope is the last touch of
        // the control by this thread (call() releases its own lock first).
        const CallerScope scope(*this);
        if (!scope.admitted()) {
            return {};  // teardown already started -- not serviced
        }
        return save_.call(std::monostate{});
    }

    bool EditorRuntimeControl::begin_pending_save()
    {
        // Claim the pending save (if any). save_'s payload is std::monostate --
        // the snapshot the write needs comes from app.prepare_scene_save() on the
        // frame thread, not from the Request -- so the claimed-out value is unused.
        std::monostate ignored;
        return save_.claim(ignored);
    }

    void EditorRuntimeControl::complete_pending_save(wz::fs::FileError err)
    {
        save_.publish(err);
    }

    void EditorRuntimeControl::post_async_save_result(AsyncSaveResult result)
    {
        async_save_results_.post(result);
    }

    void EditorRuntimeControl::drain_async_save_results(
        const std::function<void(const AsyncSaveResult&)>& fn)
    {
        async_save_results_.drain(fn);
    }

    void EditorRuntimeControl::set_frame_profiling(bool enabled)
    {
        frame_profiling_.store(enabled, std::memory_order_release);
    }

    bool EditorRuntimeControl::frame_profiling_enabled() const
    {
        return frame_profiling_.load(std::memory_order_acquire);
    }

    void EditorRuntimeControl::set_paused(bool paused)
    {
        paused_.store(paused, std::memory_order_release);
    }

    bool EditorRuntimeControl::paused() const
    {
        return paused_.load(std::memory_order_acquire);
    }

    void EditorRuntimeControl::request_reload_behavior_modules()
    {
        reload_behaviors_requested_.store(true, std::memory_order_release);
    }

    bool EditorRuntimeControl::take_reload_behavior_modules_request()
    {
        return reload_behaviors_requested_.exchange(
            false, std::memory_order_acq_rel);
    }

    void EditorRuntimeControl::set_behavior_modules(
        std::vector<std::string> modules)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        behavior_modules_ = std::move(modules);
    }

    std::vector<std::string> EditorRuntimeControl::behavior_modules() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return behavior_modules_;
    }

    void EditorRuntimeControl::set_scenelets(
        std::vector<SceneletCatalogEntry> scenelets)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scenelets_ = std::move(scenelets);
    }

    std::vector<SceneletCatalogEntry> EditorRuntimeControl::scenelets() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return scenelets_;
    }

    void EditorRuntimeControl::record_dropped_edit(
        std::string_view verb,
        std::string_view node_id)
    {
        // Keep the cap small: these are read by a human, and a drag against a
        // stale id can produce one per frame. Past the cap we only count.
        constexpr std::size_t kMaxDroppedEdits = 32;

        std::lock_guard<std::mutex> lock(mutex_);
        if (dropped_edits_.size() >= kMaxDroppedEdits) {
            ++dropped_edits_discarded_;
            return;
        }
        dropped_edits_.push_back(
            std::string(verb) + ": node '" + std::string(node_id)
            + "' is not in the running scene, so the edit was dropped");
    }

    std::vector<std::string> EditorRuntimeControl::take_dropped_edits()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> out;
        out.swap(dropped_edits_);
        if (dropped_edits_discarded_ > 0) {
            out.push_back(
                "... and " + std::to_string(dropped_edits_discarded_)
                + " more dropped edits not listed");
            dropped_edits_discarded_ = 0;
        }
        return out;
    }

    AssetGraphCompileResult EditorRuntimeControl::bind_asset_graph(
        wz::asset::AssetGraphDraft& draft)
    {
        const CallerScope scope(*this);
        if (!scope.admitted()) {
            return bind_failed("engine runtime is not running");
        }
        // call_inout moves the draft into the request (leaving `draft` a husk for
        // the bind window, as the draft-loan expects) and moves it back before
        // returning -- bound on success, or reclaimed unchanged if the engine
        // stopped / the binder threw. Both non-serviced cases surface as a failed
        // bind with the caller's authored graph intact.
        auto outcome = asset_graph_.call_inout(draft);
        if (!outcome.serviced) {
            return bind_failed("engine runtime stopped before bind completed");
        }
        return std::move(outcome.value);
    }

    void EditorRuntimeControl::service_pending_asset_graph_bind(
        const std::function<
            AssetGraphCompileResult(wz::asset::AssetGraphDraft&)>& binder)
    {
        // The binder mutates the draft in place (resolved keys + validation); the
        // Request passes args_ by reference, so this is that same draft.
        asset_graph_.service(
            [&](wz::asset::AssetGraphDraft& draft) { return binder(draft); });
    }

    void EditorRuntimeControl::post_scene_node_transform(
        SceneNodeTransformEdit edit)
    {
        transforms_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_transforms(
        const std::function<void(const SceneNodeTransformEdit&)>& applier)
    {
        transforms_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_properties(
        SceneNodePropertiesEdit edit)
    {
        properties_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_properties(
        const std::function<void(const SceneNodePropertiesEdit&)>& applier)
    {
        properties_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_reparent(
        SceneNodeReparentEdit edit)
    {
        reparents_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_reparents(
        const std::function<void(const SceneNodeReparentEdit&)>& applier)
    {
        reparents_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_reorder(
        SceneNodeReorderEdit edit)
    {
        reorders_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_reorders(
        const std::function<void(const SceneNodeReorderEdit&)>& applier)
    {
        reorders_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_render_order(
        SceneNodeRenderOrderEdit edit)
    {
        render_orders_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_render_orders(
        const std::function<void(const SceneNodeRenderOrderEdit&)>& applier)
    {
        render_orders_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_remove(
        wz::scene::AuthoredEntityId id)
    {
        removes_.post(std::move(id));
    }

    void EditorRuntimeControl::service_pending_scene_node_removes(
        const std::function<void(const wz::scene::AuthoredEntityId&)>& applier)
    {
        removes_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_behavior(
        SceneNodeBehaviorEdit edit)
    {
        behaviors_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_behaviors(
        const std::function<void(const SceneNodeBehaviorEdit&)>& applier)
    {
        behaviors_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_component(
        SceneNodeComponentEdit edit)
    {
        components_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_components(
        const std::function<void(const SceneNodeComponentEdit&)>& applier)
    {
        components_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_renderable(
        SceneNodeRenderableEdit edit)
    {
        renderables_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_renderables(
        const std::function<void(const SceneNodeRenderableEdit&)>& applier)
    {
        renderables_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_audio_renderable(
        SceneNodeAudioRenderableEdit edit)
    {
        audio_renderables_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_audio_renderables(
        const std::function<void(const SceneNodeAudioRenderableEdit&)>& applier)
    {
        audio_renderables_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_audio_source_play(
        SceneNodeAudioSourcePlayEdit edit)
    {
        audio_source_plays_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_audio_source_plays(
        const std::function<void(const SceneNodeAudioSourcePlayEdit&)>& applier)
    {
        audio_source_plays_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_render_binding(
        SceneNodeRenderBindingEdit edit)
    {
        render_bindings_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_render_bindings(
        const std::function<void(const SceneNodeRenderBindingEdit&)>& applier)
    {
        render_bindings_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_renderable_binding(
        SceneNodeRenderableBindingEdit edit)
    {
        renderable_bindings_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_renderable_bindings(
        const std::function<
            void(const SceneNodeRenderableBindingEdit&)>& applier)
    {
        renderable_bindings_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_renderable_param(
        SceneNodeRenderableParamEdit edit)
    {
        renderable_params_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_renderable_params(
        const std::function<
            void(const SceneNodeRenderableParamEdit&)>& applier)
    {
        renderable_params_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_collision(
        SceneNodeCollisionEdit edit)
    {
        collisions_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_collisions(
        const std::function<void(const SceneNodeCollisionEdit&)>& applier)
    {
        collisions_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_motion_terrain(
        SceneNodeMotionTerrainEdit edit)
    {
        motion_terrains_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_motion_terrains(
        const std::function<void(const SceneNodeMotionTerrainEdit&)>& applier)
    {
        motion_terrains_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_motion_filter(
        SceneNodeMotionFilterEdit edit)
    {
        motion_filters_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_motion_filters(
        const std::function<void(const SceneNodeMotionFilterEdit&)>& applier)
    {
        motion_filters_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_camera(
        SceneNodeCameraEdit edit)
    {
        cameras_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_cameras(
        const std::function<void(const SceneNodeCameraEdit&)>& applier)
    {
        cameras_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_atmosphere(
        SceneNodeAtmosphereEdit edit)
    {
        atmospheres_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_atmospheres(
        const std::function<void(const SceneNodeAtmosphereEdit&)>& applier)
    {
        atmospheres_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_environment(
        SceneNodeEnvironmentEdit edit)
    {
        environments_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_environments(
        const std::function<void(const SceneNodeEnvironmentEdit&)>& applier)
    {
        environments_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_render_to_texture(
        SceneNodeRenderToTextureEdit edit)
    {
        render_to_textures_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_render_to_textures(
        const std::function<void(const SceneNodeRenderToTextureEdit&)>& applier)
    {
        render_to_textures_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_scene_source(
        SceneNodeSceneSourceEdit edit)
    {
        scene_sources_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_scene_sources(
        const std::function<void(const SceneNodeSceneSourceEdit&)>& applier)
    {
        scene_sources_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_glb_scene_source(
        SceneNodeGlbSceneSourceEdit edit)
    {
        glb_scene_sources_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_glb_scene_sources(
        const std::function<void(const SceneNodeGlbSceneSourceEdit&)>& applier)
    {
        glb_scene_sources_.drain(applier);
    }

    void EditorRuntimeControl::post_scene_node_glb_style(
        SceneNodeGlbStyleEdit edit)
    {
        glb_styles_.post(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_glb_styles(
        const std::function<void(const SceneNodeGlbStyleEdit&)>& applier)
    {
        glb_styles_.drain(applier);
    }

    wz::engine::assets::SceneAddChildResult EditorRuntimeControl::add_child(
        const wz::scene::AuthoredEntityId& parent_id)
    {
        const CallerScope scope(*this);
        if (!scope.admitted()) {
            return wz::engine::assets::SceneAddChildResult{
                .ok = false,
                .new_id = {},
                .error = "engine runtime is not running",
            };
        }
        auto outcome = add_child_.call(parent_id);
        if (!outcome.serviced) {
            return wz::engine::assets::SceneAddChildResult{
                .ok = false,
                .new_id = {},
                .error = "engine runtime stopped before add completed",
            };
        }
        return std::move(outcome.value);
    }

    void EditorRuntimeControl::service_pending_add_child(
        const std::function<wz::engine::assets::SceneAddChildResult(
            const wz::scene::AuthoredEntityId&)>& adder)
    {
        add_child_.service(
            [&](wz::scene::AuthoredEntityId parent) { return adder(parent); });
    }

    bool EditorRuntimeControl::export_subtree_as_scene(
        const wz::scene::AuthoredEntityId& root_node_id,
        const wz::fs::Path& out_path)
    {
        const CallerScope scope(*this);
        if (!scope.admitted()) {
            return false;  // runtime is not running — nothing to export
        }
        const auto outcome = export_subtree_.call(
            ExportSubtreeRequest{ root_node_id, out_path });
        return outcome.serviced && outcome.value;  // false if it stopped first
    }

    void EditorRuntimeControl::service_pending_export_subtree(
        const std::function<bool(
            const wz::scene::AuthoredEntityId&, const wz::fs::Path&)>& exporter)
    {
        export_subtree_.service([&](ExportSubtreeRequest req) {
            return exporter(req.root, req.out_path);
        });
    }

    bool EditorRuntimeControl::open_scene(const wz::fs::Path& scene_path)
    {
        const CallerScope scope(*this);
        if (!scope.admitted()) {
            return false;  // runtime is not running
        }
        const auto outcome = open_scene_.call(scene_path);
        return outcome.serviced && outcome.value;  // false if it stopped first
    }

    void EditorRuntimeControl::service_pending_open_scene(
        const std::function<bool(const wz::fs::Path&)>& opener)
    {
        open_scene_.service([&](wz::fs::Path path) { return opener(path); });
    }

    wz::engine::assets::SceneAddBehaviorResult
    EditorRuntimeControl::add_node_behavior(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& module)
    {
        const CallerScope scope(*this);
        if (!scope.admitted()) {
            return wz::engine::assets::SceneAddBehaviorResult{
                .ok = false,
                .binding_id = {},
                .error = "engine runtime is not running",
            };
        }
        auto outcome =
            add_behavior_.call(AddBehaviorRequest{ node_id, module });
        if (!outcome.serviced) {
            return wz::engine::assets::SceneAddBehaviorResult{
                .ok = false,
                .binding_id = {},
                .error = "engine runtime stopped before add completed",
            };
        }
        return std::move(outcome.value);
    }

    void EditorRuntimeControl::service_pending_add_node_behavior(
        const std::function<wz::engine::assets::SceneAddBehaviorResult(
            const wz::scene::AuthoredEntityId&, const std::string&)>& adder)
    {
        add_behavior_.service([&](AddBehaviorRequest req) {
            return adder(req.node_id, req.module);
        });
    }

    std::optional<std::vector<wz::engine::assets::SceneNodeAsset>>
    EditorRuntimeControl::request_grafted_scene_nodes()
    {
        const CallerScope scope(*this);
        if (!scope.admitted()) {
            // NO ANSWER, not "nothing grafted" (D3-P039) -- Outcome.serviced is
            // the same distinction, now carried by the primitive.
            return std::nullopt;
        }
        auto outcome = grafted_nodes_.call(std::monostate{});
        if (!outcome.serviced) {
            return std::nullopt;  // engine stopped before servicing
        }
        return std::move(outcome.value);
    }

    void EditorRuntimeControl::service_pending_grafted_scene_nodes(
        const std::function<
            std::vector<wz::engine::assets::SceneNodeAsset>()>& provider)
    {
        // The provider walks scene_nodes_; Request::service runs it outside its
        // lock, so a request from the owner thread never blocks on it.
        grafted_nodes_.service([&](std::monostate) { return provider(); });
    }

    std::optional<std::vector<wz::engine::assets::SceneNodeAsset>>
    EditorRuntimeControl::request_scene_nodes()
    {
        const CallerScope scope(*this);
        if (!scope.admitted()) {
            return std::nullopt;  // NO ANSWER, not "the scene is empty" (D3-P039)
        }
        auto outcome = authored_nodes_.call(std::monostate{});
        if (!outcome.serviced) {
            return std::nullopt;
        }
        return std::move(outcome.value);
    }

    void EditorRuntimeControl::service_pending_scene_nodes(
        const std::function<
            std::vector<wz::engine::assets::SceneNodeAsset>()>& provider)
    {
        authored_nodes_.service([&](std::monostate) { return provider(); });
    }

    FrameDelta compute_frame_delta(
        wz::time::Tick last,
        wz::time::Tick sampled,
        uint64_t ticks_per_second)
    {
        FrameDelta out{};
        // Tick is UNSIGNED, so a clock that failed to advance -- or went
        // backwards, which QPC can do across a core switch on some hardware --
        // makes `sampled - last` colossal rather than negative. Correct it
        // forward, exactly as the engine's other frame loop has always done
        // (engine.cpp: `if (end <= last) end = last + 1;`). #313, B4-S2.
        out.now = (sampled <= last) ? last + 1 : sampled;

        if (ticks_per_second == 0) {
            return out;  // no usable clock: a zero delta beats a garbage one
        }

        out.dt = static_cast<float>(
            static_cast<double>(out.now - last)
            / static_cast<double>(ticks_per_second));

        // Bound it. See kMaxFrameSeconds: the pump can block for an unbounded,
        // user-controlled interval, and the raw delta went to motion
        // integration and terrain constraints while the renderer clamped its
        // own copy one line away (#313, B4-C9). Physics teleported; animation
        // did not.
        if (!(out.dt > 0.0f)) {
            out.dt = 0.0f;  // also catches NaN, which compares false here
        }
        else if (out.dt > kMaxFrameSeconds) {
            out.dt = kMaxFrameSeconds;
        }
        return out;
    }

    EditorRuntimeControl::CallerScope::CallerScope(
        EditorRuntimeControl& control) noexcept
        : control_(&control)
    {
        // Increment FIRST, then test. begin_close() stores closing_ and then
        // reads the count, so an increment landing before that store is
        // guaranteed visible to the drain (it waits for us), and one landing
        // after sees closing_ and backs straight out. Sequential consistency on
        // both sides: this is a teardown path, not a hot one.
        control_->active_callers_.fetch_add(1);
        admitted_ = !control_->closing_.load();
    }

    EditorRuntimeControl::CallerScope::~CallerScope()
    {
        // THIS MUST BE THE LAST TOUCH OF THE CONTROL BY THIS THREAD, which is
        // why every verb declares its CallerScope BEFORE its unique_lock: the
        // scope then destructs AFTER the lock has been released. Reverse the
        // declarations and stop() can delete between this decrement and
        // ~unique_lock, so the lock's own unlock touches a freed mutex —
        // exactly the bug this exists to prevent, one level down.
        control_->active_callers_.fetch_sub(1);
    }

    void EditorRuntimeControl::begin_close()
    {
        closing_.store(true);
        // Wake anyone already inside; finished_ is what their wait predicates
        // test, and it is idempotent.
        mark_finished();
    }

    void EditorRuntimeControl::wait_for_callers_to_exit()
    {
        // A spin-with-yield rather than a condition variable ON PURPOSE. The
        // signal a departing caller sends must not touch this object, so it is
        // a bare atomic decrement and nothing else — there is no lock to wait
        // under and no cv to notify. Only the thread that is about to delete
        // waits here, and begin_close() has already unblocked every caller, so
        // this is bounded by how long a verb takes to unwind, not by the engine.
        while (active_callers_.load() != 0) {
            std::this_thread::yield();
        }
    }

    void EditorRuntimeControl::mark_finished()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        // Release every caller parked in a blocking verb (each Request on its
        // own cv). Idempotent -- teardown may call this more than once.
        asset_graph_.abandon();
        add_child_.abandon();
        add_behavior_.abandon();
        export_subtree_.abandon();
        open_scene_.abandon();
        grafted_nodes_.abandon();
        authored_nodes_.abandon();
        save_.abandon();
    }

    bool EditorRuntimeControl::finished() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return finished_;
    }

    int run_project_runtime(
        const std::string& window_title,
        const wz::fs::Path& asset_graph,
        const wz::fs::Path& scene,
        const wz::fs::Path& resource_root,
        EditorRuntimeControl* control,
        EditorRuntimeLogSink log_sink,
        const wz::fs::Path& behavior_module_folder,
        const RuntimeRunOptions& run_options)
    {
        wz::engine::AppContext ctx;

        wz::engine::AppDesc desc;
        desc.window = { window_title.c_str(), 1280, 720, true, false };
        desc.resource_root = resource_root;
        // Enable the baked disk cache only when a bundle asked for it (issue
        // #334). An empty cache_root leaves asset_cache default (enabled but with
        // an empty root => the provider serves nothing), so the editor's resident
        // engine — which passes a default RuntimeRunOptions — is unaffected.
        desc.asset_cache.root = run_options.cache_root;
        desc.asset_cache.enabled = !run_options.cache_root.empty();
        desc.asset_cache.sealed = run_options.cache_sealed;

        // Spin up the worker pool + IO lane BEFORE creating the GPU device (#305).
        // The thread-creation storm (up to ~16 threads) then settles -- the workers
        // find no work and park -- while the system is calm, instead of firing the
        // instant create_device returns, which was observed to upset the AMD driver
        // once the pool went live. Symmetrically they are torn down AFTER engine
        // shutdown below, so GPU teardown also runs while the lanes are quiescent.
        // Nothing dispatches to either during init, so installing the globals here
        // is safe; the logger is not up until init, so the "installed" lines are
        // logged just after it.
        //
        // Compute pool: runs the cognition workload the behaviours ABI drives --
        // bit-identical to serial (the #293 S3 determinism oracle), only faster.
        // WZ_TASKS_POOL=0 skips it (zero threads) for A/B; WZ_TASKS_FORCE_SERIAL
        // keeps it up but dispatches serially.
        const bool task_pool_enabled = [] {
#pragma warning(push)
#pragma warning(disable : 4996)  // std::getenv is the correct, portable call
            const char* v = std::getenv("WZ_TASKS_POOL");
#pragma warning(pop)
            return !(v != nullptr && v[0] == '0' && v[1] == '\0');  // default ON
        }();
        std::unique_ptr<wz::tasks::TaskScheduler> task_pool;
        if (task_pool_enabled) {
            task_pool = std::make_unique<wz::tasks::TaskScheduler>();
            wz::tasks::set_task_scheduler(task_pool.get());
        }
        // IO lane: dedicated blocking-I/O threads backing the wz::IAsyncExecutor
        // seam, so wz::fs::async_read_file / async_write_file run off the frame
        // thread instead of returning IOError (inert until an executor is installed).
        auto io_executor = std::make_unique<wz::io::IoExecutor>();
        wz::set_async_executor(io_executor.get());

        // LoggerService lane: a cold thread that turns published diagnostic state
        // into log lines off the hot lanes (#291 / #305 step 4d). Spun up here with
        // the other lanes -- before create_device, per the AMD constraint. The D3D12
        // debug-layer producer (publish_debug_diagnostics, on the render thread)
        // publishes into it; this lane keeps the exact repeat count and reports on
        // its cadence with the debug-layer reporter (category name + main/offscreen
        // label). The reporter captures ctx.logger BY REFERENCE and dereferences it
        // only on the cadence, after init has brought the logger up; quiesce() below
        // stops that before the logger is torn down.
        auto logger_service = std::make_unique<wz::diag::LoggerServiceLane>(
            wz::gpu::make_debug_layer_reporter(ctx.logger));
        wz::diag::set_diagnostic_sink(logger_service.get());

        // Uninstall + join the lanes. The LoggerService lane is quiesced before
        // engine::shutdown (see the guard below), so this only joins its
        // already-silent thread.
        const auto teardown_lanes = [&]
        {
            wz::diag::set_diagnostic_sink(nullptr);
            logger_service.reset();
            wz::set_async_executor(nullptr);
            io_executor.reset();
            if (task_pool) {
                wz::tasks::set_task_scheduler(nullptr);
                task_pool.reset();
            }
        };

        // Ordered teardown on EVERY exit from here on, including an exception.
        // Nothing below is noexcept -- Request::service deliberately rethrows so
        // the loop can log a throwing bind, and any allocation can throw -- and an
        // unwind that skipped this ran the destructors in declaration order:
        // io_executor and task_pool would join their threads while the GPU device
        // was still alive, which is precisely the inversion the AMD-driver
        // ordering exists to prevent, and g_scheduler / g_executor / g_sink would
        // be left pointing at destroyed objects while the ABI catches the
        // exception and keeps the process running.
        //
        // Constructed BEFORE engine::init, not after it: init allocates after
        // create_device succeeds (the EngineGpuContext / EngineAssetLibrary
        // make_uniques and their ostringstream logging), so a throw in that window
        // left a live device with no destroy_device -- the lanes then joined their
        // threads under it, the exact AMD inversion above. It also covers the
        // globals installed just above, which were unguarded until now.
        //
        // Referencing the lambda's own type rather than copying it into a
        // std::function: the copy was itself an allocation that could throw, and
        // it sat in the one spot no guard covered.
        using TeardownLanesFn = decltype(teardown_lanes);
        struct RuntimeTeardown
        {
            wz::diag::LoggerServiceLane& logger_service;
            wz::engine::AppContext& ctx;
            const TeardownLanesFn& teardown_lanes;

            ~RuntimeTeardown()
            {
                // Quiesce the LoggerService lane while the logger is STILL ALIVE:
                // a final report, then it stops touching the logger (#291 / #305
                // step 4d). This must precede engine::shutdown, which frees the
                // logger -- reporting across that free would be a data race. The
                // lane thread stays alive but logger-silent, joined below after
                // the device is gone. quiesce() is idempotent, so the normal path
                // having its own reasons to call it early stays safe.
                logger_service.quiesce();

                // Tear the GPU/engine down FIRST, while the lanes are still alive
                // but quiescent (the app scope has closed, so nothing is in
                // flight), THEN join the lane threads -- the mirror of spinning
                // them up before init, keeping the thread-join storm clear of GPU
                // teardown (#305).
                //
                // Safe on every state init can leave behind, which is why the
                // init-failure path below needs no teardown of its own: wait_idle
                // and destroy_device are gated on device.valid(), the assets/gpu
                // resets are no-ops on the null unique_ptrs a failed init leaves,
                // destroy_window is gated on window.valid(), and shutdown_logger
                // is null-safe and idempotent (as is Logger::info, which no-ops
                // when state is null). init() leaves the logger UP on its failure
                // paths and the diagnostics lane holds ctx.logger by reference, so
                // routing those exits through here frees it in the same order the
                // success path uses -- without it the logger's own worker thread
                // outlived the runtime on every no-device run.
                wz::engine::shutdown(ctx);
                teardown_lanes();
            }
        };
        RuntimeTeardown runtime_teardown{ *logger_service, ctx, teardown_lanes };

        if (!wz::engine::init(ctx, desc)) {
            // Bounded (verification) runs distinguish "no GPU device" from a real
            // failure so a separate-process test can be SKIPPED rather than
            // failed on a machine without a device. The guard above tears down.
            return run_options.max_frames > 0 ? kRuntimeNoDeviceExitCode : 1;
        }

        // Logger is up now -- report what was spun up before init.
        if (task_pool)
            ctx.logger.info(
                "wz::tasks pool installed, workers="
                + std::to_string(task_pool->worker_count()));
        else
            ctx.logger.info(
                "wz::tasks pool disabled (WZ_TASKS_POOL=0); serial-inline backend");
        ctx.logger.info(
            "IO lane installed, threads="
            + std::to_string(io_executor->thread_count()));
        ctx.logger.info(
            "LoggerService lane installed (D3D12 debug-layer diagnostics)");

        if (log_sink.write) {
            wz::logging::set_log_sink(
                ctx.logger,
                forward_editor_runtime_log,
                &log_sink);
            ctx.logger.info("editor resident engine log sink attached");
        }

        // Bounded-run exit code, computed inside the app scope below (it needs
        // the live app to query resolved renderables) and returned after
        // shutdown. Stays 0 for the unbounded interactive loop.
        int exit_code = 0;
        {
            wz::app::WozzitsApp_v1 app(ctx);

            // Standalone/play (no editor control) prefers a scene-authored camera
            // once selected on load; the editor edit viewport (non-null control)
            // stays on the free-fly camera so you can navigate the scene.
            app.set_prefer_scene_camera(control == nullptr);

            const auto binder =
                [&app](wz::asset::AssetGraphDraft& draft)
            {
                return app.bind_asset_graph(draft);
            };

            // Present one cleared frame before the (possibly multi-second) asset
            // compile, so the window shows the background instead of an
            // uninitialized backbuffer while load_scene blocks.
            if (wz::gpu::begin_frame(ctx.device)) {
                wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
                wz::gpu::end_frame(ctx.device);
                wz::gpu::present(ctx.device);
            }

            const bool scene_loaded =
                app.load_scene(wz::app::WozzitsAppSceneLoadDesc{
                    .asset_graph = asset_graph,
                    .scene = scene,
                    .behavior_module_folder = behavior_module_folder,
                });
            if (!scene_loaded) {
                ctx.logger.error("load scene failed");
            }

            // Publish the loaded behavior modules so the owner (editor) can offer
            // them for binding (the registry is engine-thread-owned).
            if (control) {
                control->set_behavior_modules(app.behavior_module_names());
                control->set_scenelets(app.scenelet_catalog());
            }

            // Free-fly camera input: raw keyboard/mouse feed the global input
            // event queue, drained per frame into an InputState that drives the
            // app's camera (and any future sim) via simulation_tick.
            wz::input::init_raw_input();
            // Sample game controllers (XInput) each frame so controller-driven
            // behaviors (e.g. a tank_controller reading the stick axes) receive
            // input — without this the InputState has no controller signal and
            // input.* behaviors never fire.
            wz::platform::win32::controller_init();
            wz::input::InputState input{};
            wz::input::InputState prev_input{};
            wz::time::Tick last_ticks = wz::time::TimeSource::now_ticks();

            // Fly-cam enable state, toggled by ESC (see the in-loop toggle below).
            // Starts disabled: the runtime window receives GLOBAL raw input
            // (RIDEV_INPUTSINK), so without an explicit arm the camera would react
            // to keystrokes meant for the editor window.
            bool camera_enabled = false;

            // Bounded-run accounting (run_options.max_frames > 0): count
            // presented frames + note any frame failure, so the exit code can
            // report whether the project actually rendered.
            uint32_t frames_presented = 0;
            bool frame_error = false;

            // Per-frame phase DAG (#305): the frame's phases run through
            // DagScheduler instead of an inline sequence, so per-phase timing +
            // critical-path come from live frames. Behaviour-preserving -- same
            // phases, same order, same per-phase failure logs, same
            // error->abort. Built once; frame_drive carries the per-frame inputs
            // the callbacks read (updated at the top of each iteration).
            wz::engine::FrameSchedule frame_schedule;

            // WZ_FRAME_PROFILING=1 forces per-phase profiling on for standalone /
            // headless runs, where there is no editor UI to toggle it (mirrors
            // WZ_TASKS_FORCE_SERIAL). Read once; OR'd into the per-frame gate below.
            const bool env_frame_profiling = [] {
#pragma warning(push)
#pragma warning(disable : 4996)  // std::getenv is the correct, portable call
                const char* v = std::getenv("WZ_FRAME_PROFILING");
#pragma warning(pop)
                return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
            }();

            // WZ_CPU_LOAD_MS: inject N ms of synthetic per-frame CPU work (a busy
            // spin) to measure the frames-in-flight headroom (#306) -- how much CPU
            // the pipeline absorbs before the frame period grows. Default 0 = off.
            const double env_cpu_load_ms = [&ctx] {
#pragma warning(push)
#pragma warning(disable : 4996)
                const char* v = std::getenv("WZ_CPU_LOAD_MS");
#pragma warning(pop)
                if (v == nullptr || v[0] == '\0') {
                    return 0.0;
                }
                const double ms = std::atof(v);
                // atof yields 0.0 for anything unparseable, which is also the
                // "off" value -- so a typo silently measured the un-loaded case.
                // A negative value is equally meaningless here. Either way, say so.
                if (!(ms > 0.0)) {
                    ctx.logger.warn(
                        std::string("ignoring WZ_CPU_LOAD_MS='") + v
                        + "' (expected a positive number of milliseconds); "
                          "no synthetic CPU load");
                    return 0.0;
                }
                return ms;
            }();

            // WZ_NO_VSYNC=1 presents with sync_interval 0 (uncapped), so raw frame
            // throughput -- not the 60 Hz cap -- is what the profiler reports.
            const bool env_no_vsync = [] {
#pragma warning(push)
#pragma warning(disable : 4996)
                const char* v = std::getenv("WZ_NO_VSYNC");
#pragma warning(pop)
                return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
            }();

            struct FrameDriveInputs {
                wz::input::InputState input{};
                float                 dt           = 0.0f;
                bool                  drive_camera = false;
            } frame_drive;

            wz::engine::FramePhaseOps frame_ops;
            // Paused freezes the SCENE, not the VIEWPORT (#313, B4-C8): the
            // aspect + fly-cam live in update_view, which paused_frame_tick still
            // runs, so resizing/looking around while paused keeps working.
            frame_ops.simulate = [&] {
                if (!control || !control->paused())
                    app.simulation_tick(frame_drive.input, frame_drive.dt, frame_drive.drive_camera);
                else
                    app.paused_frame_tick(frame_drive.input, frame_drive.dt, frame_drive.drive_camera);
                if (env_cpu_load_ms > 0.0) {
                    // Synthetic CPU load (#306 headroom measurement): busy-spin so
                    // it counts as real per-frame CPU work that overlaps the GPU.
                    const uint64_t hz = wz::time::TimeSource::ticks_per_second();
                    const uint64_t target = static_cast<uint64_t>(
                        env_cpu_load_ms * 1e-3 * static_cast<double>(hz));
                    const uint64_t start = wz::time::TimeSource::now_ticks();
                    while (wz::time::TimeSource::now_ticks() - start < target) { }
                }
                return true;
            };
            frame_ops.begin_frame = [&] {
                if (!wz::gpu::begin_frame(ctx.device)) { ctx.logger.error("begin_frame failed"); return false; }
                return true;
            };
            frame_ops.clear = [&] {
                wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
                return true;
            };
            frame_ops.render_scene = [&] {
                return app.render_scene();
            };
            // S6 3D-mesh consumer: puppet(s) rendered to an offscreen texture and
            // shown on a spinning card over the scene (no-op when off / no puppet).
            frame_ops.render_overlay = [&] {
                if (!app.render_puppet_showcase()) { ctx.logger.error("render_puppet_showcase failed"); return false; }
                return true;
            };
            frame_ops.end_frame = [&] {
                if (!wz::gpu::end_frame(ctx.device)) { ctx.logger.error("end_frame failed"); return false; }
                return true;
            };
            frame_ops.present = [&] {
                const bool ok = env_no_vsync
                    ? wz::gpu::present(ctx.device, 0u)
                    : wz::gpu::present(ctx.device);
                if (!ok) { ctx.logger.error("present failed"); return false; }
                return true;
            };

            while (!wz::window::window_should_close(ctx.window)
                && !(control && control->stop_requested()))
            {
                wz::window::pump_messages();

                bool closing = false;
                PlatformEvent event{};
                while (wz::window::poll_event(ctx.window, event)) {
                    if (event.type == PlatformEvent::Type::Close) {
                        closing = true;
                        break;
                    }
                    if (event.type == PlatformEvent::Type::Resize) {
                        wz::gpu::resize(
                            ctx.device,
                            event.resize.width,
                            event.resize.height);
                    }
                }
                if (closing) {
                    break;
                }

                // Service editor edits (compile/swap, then live scene-node
                // transforms) before sim + render so this frame reflects them.
                if (control) {
                    // Apply the editor's frame-profiling toggle each frame
                    // (idempotent: the app acts only on an on->off transition).
                    app.set_frame_profiling_enabled(
                        control->frame_profiling_enabled());
                    control->service_pending_asset_graph_bind(binder);
                    control->service_pending_scene_node_transforms(
                        [&app, control](const SceneNodeTransformEdit& edit) {
                            if (!app.set_node_transform(edit.id, edit.transform)) {
                                control->record_dropped_edit(
                                    "set_node_transform", edit.id);
                            }
                        });
                    control->service_pending_scene_node_properties(
                        [&app, control](const SceneNodePropertiesEdit& edit) {
                            if (!app.set_node_properties(
                                edit.id, edit.name, edit.visible)) {
                                control->record_dropped_edit(
                                    "set_node_properties", edit.id);
                            }
                        });
                    control->service_pending_scene_node_reparents(
                        [&app, control](const SceneNodeReparentEdit& edit) {
                            if (!app.reparent_node(edit.id, edit.new_parent_id)) {
                                control->record_dropped_edit(
                                    "reparent_node", edit.id);
                            }
                        });
                    control->service_pending_scene_node_reorders(
                        [&app, control](const SceneNodeReorderEdit& edit) {
                            if (!app.reorder_node(edit.id, edit.before_id)) {
                                control->record_dropped_edit(
                                    "reorder_node", edit.id);
                            }
                        });
                    control->service_pending_scene_node_render_orders(
                        [&app, control](const SceneNodeRenderOrderEdit& edit) {
                            if (!app.set_node_render_order(edit.id, edit.render_order)) {
                                control->record_dropped_edit(
                                    "set_node_render_order", edit.id);
                            }
                        });
                    control->service_pending_scene_node_removes(
                        [&app, control](const wz::scene::AuthoredEntityId& id) {
                            if (!app.remove_node(id)) {
                                control->record_dropped_edit(
                                    "remove_node", id);
                            }
                        });
                    control->service_pending_scene_node_behaviors(
                        [&app](const SceneNodeBehaviorEdit& edit) {
                            // Translate the seam edit into a granular app call
                            // (WozzitsApp_v1 stays ignorant of the seam struct,
                            // mirroring set_node_properties/reparent_node above).
                            using Op = SceneNodeBehaviorEdit::Op;
                            switch (edit.op) {
                                case Op::Remove:
                                    app.remove_node_behavior(
                                        edit.node_id, edit.binding_id);
                                    break;
                                case Op::SetEnabled:
                                    app.set_node_behavior_enabled(
                                        edit.node_id,
                                        edit.binding_id,
                                        edit.enabled);
                                    break;
                                case Op::SetFields:
                                    app.set_node_behavior_fields(
                                        edit.node_id,
                                        edit.binding_id,
                                        edit.label,
                                        edit.module);
                                    break;
                                case Op::SetEvents:
                                    app.set_node_behavior_events(
                                        edit.node_id,
                                        edit.binding_id,
                                        edit.events);
                                    break;
                                case Op::SetConfig:
                                    app.set_node_behavior_config(
                                        edit.node_id,
                                        edit.binding_id,
                                        edit.config_value);
                                    break;
                                case Op::ClearConfig:
                                    app.clear_node_behavior_config(
                                        edit.node_id,
                                        edit.binding_id,
                                        edit.config_key);
                                    break;
                            }
                        });
                    control->service_pending_scene_node_components(
                        [&app](const SceneNodeComponentEdit& edit) {
                            // Translate the seam edit into a granular app call
                            // (WozzitsApp_v1 stays ignorant of the seam struct,
                            // mirroring the behavior switch above).
                            using Op = SceneNodeComponentEdit::Op;
                            switch (edit.op) {
                                case Op::Add:
                                    app.add_node_component(
                                        edit.node_id, edit.kind);
                                    break;
                                case Op::Remove:
                                    app.remove_node_component(
                                        edit.node_id, edit.kind);
                                    break;
                            }
                        });
                    control->service_pending_scene_node_renderables(
                        [&app, control](const SceneNodeRenderableEdit& edit) {
                            // Author the preferred asset-graph renderable (or
                            // clear it when asset_graph_node_id == 0).
                            if (!app.set_node_renderable_asset(
                                edit.node_id, edit.asset_graph_node_id)) {
                                control->record_dropped_edit(
                                    "set_node_renderable_asset", edit.node_id);
                            }
                        });
                    control->service_pending_scene_node_audio_renderables(
                        [&app, control](const SceneNodeAudioRenderableEdit& edit) {
                            // Author the AudioSource's renderable reference (or
                            // clear it when asset_graph_node_id == 0).
                            if (!app.set_node_audio_renderable(
                                edit.node_id, edit.asset_graph_node_id)) {
                                control->record_dropped_edit(
                                    "set_node_audio_renderable", edit.node_id);
                            }
                        });
                    control->service_pending_scene_node_audio_source_plays(
                        [&app, control](const SceneNodeAudioSourcePlayEdit& edit) {
                            if (!app.set_node_audio_source_play(
                                edit.node_id, edit.auto_play, edit.enabled)) {
                                control->record_dropped_edit(
                                    "set_node_audio_source_play", edit.node_id);
                            }
                        });
                    control->service_pending_scene_node_render_bindings(
                        [&app](const SceneNodeRenderBindingEdit& edit) {
                            // Author one ingredient of the node's renderable
                            // binding (or clear it when asset_graph_node_id == 0);
                            // the apply re-assembles the renderable, the program
                            // inherited down the tree (#213 increment 2).
                            if (edit.ingredient
                                == SceneNodeRenderBindingEdit::Ingredient::
                                       Geometry)
                            {
                                app.set_node_geometry_asset(
                                    edit.node_id, edit.asset_graph_node_id);
                            }
                            else {
                                app.set_node_render_program(
                                    edit.node_id, edit.asset_graph_node_id);
                            }
                        });
                    control->service_pending_scene_node_renderable_bindings(
                        [&app](const SceneNodeRenderableBindingEdit& edit) {
                            // Upsert/remove one semantic binding of the node's
                            // custom-renderable ingredients (#229/#230); the
                            // apply re-assembles the node's renderable (a
                            // binding present makes it the custom 0x70A form).
                            app.set_node_renderable_binding(
                                edit.node_id,
                                edit.semantic,
                                edit.asset_graph_node_id);
                        });
                    control->service_pending_scene_node_renderable_params(
                        [&app](const SceneNodeRenderableParamEdit& edit) {
                            // Upsert/remove one per-instance constant override
                            // (#229/#230): a pack-time merge on the node — the
                            // apply neither re-assembles nor recompiles (the
                            // custom-form flip on first-add/last-remove is
                            // handled inside the seam).
                            app.set_node_renderable_constant(
                                edit.node_id,
                                edit.name,
                                edit.clear ? nullptr : edit.value);
                        });
                    control->service_pending_scene_node_collisions(
                        [&app](const SceneNodeCollisionEdit& edit) {
                            // Author the node's Collision reference (or clear it
                            // when asset_graph_node_id == 0) + constrain_movement;
                            // the apply re-bridges + rebuilds the runtime scene so
                            // the constraint surface takes effect (#216/#217).
                            app.set_node_collision_asset(
                                edit.node_id,
                                edit.asset_graph_node_id,
                                edit.constrain_movement);
                        });
                    control->service_pending_scene_node_motion_terrains(
                        [&app](const SceneNodeMotionTerrainEdit& edit) {
                            // Set the Motion terrain-stick fields; the apply
                            // rebuilds the runtime scene so the constraint loop
                            // sees them (#216/#217).
                            app.set_node_motion_terrain_fields(
                                edit.node_id,
                                edit.terrain_constrained,
                                edit.ride_height,
                                edit.footprint_radius,
                                edit.align_to_surface,
                                edit.alignment_strength);
                        });
                    control->service_pending_scene_node_motion_filters(
                        [&app](const SceneNodeMotionFilterEdit& edit) {
                            // Set the whole Motion Filter component; the apply
                            // rebuilds the runtime scene so apply_motion_filters
                            // sees the new fields next frame.
                            app.set_node_motion_filter(edit.node_id, edit.filter);
                        });
                    control->service_pending_scene_node_cameras(
                        [&app](const SceneNodeCameraEdit& edit) {
                            // Set the whole Camera component; the apply rebuilds the
                            // runtime scene so the view controller re-reads it, and
                            // marks the scene dirty so Save All persists the fields.
                            app.set_node_camera(edit.node_id, edit.camera);
                        });
                    control->service_pending_scene_node_atmospheres(
                        [&app](const SceneNodeAtmosphereEdit& edit) {
                            // Set the whole Atmosphere component; the apply rebuilds
                            // the runtime scene so the renderer re-resolves the frame
                            // atmosphere, and marks the scene dirty so Save All
                            // persists the reference + enabled flag.
                            app.set_node_atmosphere(edit.node_id, edit.atmosphere);
                        });
                    control->service_pending_scene_node_environments(
                        [&app](const SceneNodeEnvironmentEdit& edit) {
                            // Set the whole FrameEnvironment component; the apply
                            // rebuilds the runtime scene so the renderer re-resolves
                            // the frame environment, and marks the scene dirty so
                            // Save All persists the reference + enabled flag.
                            app.set_node_environment(
                                edit.node_id, edit.environment);
                        });
                    control->service_pending_scene_node_render_to_textures(
                        [&app](const SceneNodeRenderToTextureEdit& edit) {
                            // Set the whole RenderToTexture component; the apply
                            // re-assembles the render bindings so the target key
                            // re-resolves, and marks the scene dirty so Save All
                            // persists the reference + switches.
                            app.set_node_render_to_texture(
                                edit.node_id, edit.render_to_texture);
                        });
                    control->service_pending_scene_node_scene_sources(
                        [&app](const SceneNodeSceneSourceEdit& edit) {
                            // Author the preferred Scene source (or clear it
                            // when asset_graph_node_id == 0); the apply re-grafts
                            // the referenced sub-scene under the host (#213).
                            app.set_node_scene_source(
                                edit.node_id, edit.asset_graph_node_id);
                            // Flatten mode: immediately expand the just-set
                            // reference into authored nodes and drop the link.
                            if (edit.asset_graph_node_id != 0
                                && edit.consume_mode
                                       == SceneNodeSceneSourceEdit::ConsumeMode::
                                              Flatten)
                            {
                                app.flatten_scene_source(edit.node_id);
                            }
                        });
                    control->service_pending_scene_node_glb_scene_sources(
                        [&app](const SceneNodeGlbSceneSourceEdit& edit) {
                            // Author the GLB scene-source DESCRIPTOR (or clear it
                            // when the path is empty); the apply re-resolves +
                            // re-grafts the host's children (#213). The descriptor
                            // carries consume_mode so a Flatten-authored load bakes
                            // it (load_scene's flatten loop); flatten-on-set here
                            // is deferred to 3b (Instance is the 3a path).
                            app.set_node_glb_scene_source(
                                edit.node_id,
                                wz::engine::assets::SceneGLBSceneSource{
                                    .path = edit.path,
                                    .scene_index = edit.scene_index,
                                    .consume_mode = edit.consume_mode,
                                });
                        });
                    control->service_pending_scene_node_glb_styles(
                        [&app](const SceneNodeGlbStyleEdit& edit) {
                            // Author per-component GLB styling into the node's
                            // descriptor (set base / set-or-replace per-mesh
                            // override / clear per-mesh override); the apply re-
                            // resolves + re-grafts the re-keyed Scene (#213 3b-2).
                            if (edit.is_base) {
                                app.set_node_glb_base_style(
                                    edit.node_id, edit.style);
                            }
                            else if (edit.clear) {
                                app.clear_node_glb_mesh_style(
                                    edit.node_id, edit.mesh_index);
                            }
                            else {
                                app.set_node_glb_mesh_style(
                                    edit.node_id, edit.mesh_index, edit.style);
                            }
                        });
                    control->service_pending_add_child(
                        [&app](const wz::scene::AuthoredEntityId& parent_id) {
                            return app.add_child_node(parent_id);
                        });
                    control->service_pending_add_node_behavior(
                        [&app](
                            const wz::scene::AuthoredEntityId& node_id,
                            const std::string& module) {
                            return app.add_node_behavior(node_id, module);
                        });
                    control->service_pending_grafted_scene_nodes(
                        [&app]() {
                            // Hand back a copy of the runtime-only grafted nodes
                            // so the editor can merge them under their hosts
                            // (#213); the host node only stores the reference.
                            return app.grafted_scene_nodes();
                        });
                    control->service_pending_scene_nodes(
                        [&app]() {
                            // The authored scene (for the editor's tree refresh
                            // after open_scene swaps the working scene).
                            return app.authored_scene_nodes();
                        });
                    // Interactive save, off the frame thread (#305 step 4b / #300
                    // layer 2). First finalize any async save that finished since
                    // last frame -- draining BEFORE dispatching a new one so a prior
                    // save's dirty-flag restore lands before the next snapshot.
                    control->drain_async_save_results(
                        [&app, &ctx](const wz::app::AsyncSaveResult& r) {
                            app.finalize_scene_save(
                                r.err, r.restore_scene_dirty,
                                r.restore_camera_dirty);
                            if (r.err != wz::fs::FileError::None) {
                                // Parity with the teardown save's log; the ABI also
                                // surfaces this to the caller.
                                ctx.logger.error(
                                    std::string("save_scene (async) failed: ")
                                    + wz::fs::to_string(r.err));
                            }
                        });
                    // Then service a newly requested save: prepare the snapshot on
                    // this (frame) thread -- it reads live scene state -- and run the
                    // blocking read-modify-write on the IO lane so the frame loop
                    // does not stall on disk. The caller blocked in
                    // save_scene_blocking() wakes with the FileError the IO lane
                    // publishes; a failed write restores the dirty flags via the
                    // AsyncSaveResult drained above.
                    if (control->begin_pending_save()) {
                        // The request is now CLAIMED, and a claim obliges exactly
                        // one publish: a caller parked in save_scene_blocking()
                        // wakes only on the InFlight->Published transition, and
                        // abandon() deliberately will not wake it (runtime_channel.h
                        // -- the servicer owns the payload). So every exit from
                        // this block must answer, including an exception thrown by
                        // prepare_scene_save() or by commit inline below. Miss it
                        // and the caller waits forever, wait_for_callers_to_exit()
                        // spins forever behind it, and wz_host_runtime_stop never
                        // returns -- an unkillable editor, not a crash.
                        //
                        // `answered` is set by whichever path took responsibility;
                        // the guard publishes IOError on any path that did not.
                        bool answered = false;
                        wz::app::PendingSaveGuard save_guard(control, answered);

                        wz::app::WozzitsApp_v1::SceneSavePreparation prep =
                            app.prepare_scene_save();
                        if (!prep.needs_write) {
                            // Not dirty, or no source path -- answer the caller now.
                            control->complete_pending_save(prep.early_out);
                            answered = true;
                        }
                        else if (wz::IAsyncExecutor* io =
                                     wz::get_async_executor()) {
                            // Keep the restore flags before the snapshot moves into
                            // the closure -- the refusal path below still needs
                            // them, and `prep.work` is gone by then.
                            const bool restore_scene = prep.work.scene_dirty;
                            const bool restore_camera =
                                prep.work.want_editor_camera;
                            const bool accepted = io->post(
                                [control, work = std::move(prep.work)] {
                                    // The IO worker loop does not catch, so a save
                                    // closure must never escape it: a throw becomes
                                    // an IOError the caller sees, and finalize
                                    // restores the scene dirty.
                                    wz::fs::FileError err =
                                        wz::fs::FileError::IOError;
                                    try {
                                        err = wz::app::WozzitsApp_v1::
                                            commit_scene_save(work);
                                    }
                                    catch (...) {
                                        err = wz::fs::FileError::IOError;
                                    }
                                    // Post the finalize result BEFORE waking the
                                    // caller. complete_pending_save returns the
                                    // request to Idle, so the frame thread can
                                    // claim and prepare the NEXT save the moment
                                    // it lands -- and prepare clears the dirty
                                    // flags. Waking first let save #2's snapshot
                                    // be taken before save #1's restore was even
                                    // queued, inverting the finalize-before-next-
                                    // snapshot ordering the drain site documents.
                                    control->post_async_save_result(
                                        { err, work.scene_dirty,
                                          work.want_editor_camera });
                                    control->complete_pending_save(err);
                                });
                            if (accepted) {
                                answered = true;  // the closure owns the publish
                            }
                            else {
                                // The lane refused it: teardown has begun and its
                                // threads are draining to a join. Nothing will run
                                // that closure, so answer here and put back the
                                // dirty flags prepare() optimistically cleared --
                                // the teardown save then re-attempts the write
                                // instead of skipping it as "not dirty".
                                control->complete_pending_save(
                                    wz::fs::FileError::IOError);
                                answered = true;
                                app.finalize_scene_save(
                                    wz::fs::FileError::IOError, restore_scene,
                                    restore_camera);
                            }
                        }
                        else {
                            // No IO lane installed (the live runtime always installs
                            // one before init; this is the belt-and-braces path):
                            // commit inline so the save still happens. Catches for
                            // the same reason the IO closure does -- an escaping
                            // throw here would skip the publish below and park the
                            // caller forever.
                            wz::fs::FileError err = wz::fs::FileError::IOError;
                            try {
                                err = wz::app::WozzitsApp_v1::commit_scene_save(
                                    prep.work);
                            }
                            catch (...) {
                                err = wz::fs::FileError::IOError;
                            }
                            control->complete_pending_save(err);
                            answered = true;
                            app.finalize_scene_save(
                                err, prep.work.scene_dirty,
                                prep.work.want_editor_camera);
                        }
                    }
                    control->service_pending_export_subtree(
                        [&app](
                            const wz::scene::AuthoredEntityId& root_node_id,
                            const wz::fs::Path& out_path) {
                            return app.export_subtree_as_scene(
                                root_node_id, out_path);
                        });
                    control->service_pending_open_scene(
                        [&app, control](const wz::fs::Path& scene_path) {
                            const bool ok = app.open_scene(scene_path);
                            // Republish the module + scenelet catalogs for the
                            // swapped-in scene (same project, so usually identical).
                            control->set_behavior_modules(
                                app.behavior_module_names());
                            control->set_scenelets(app.scenelet_catalog());
                            return ok;
                        });
                    if (control->take_reload_behavior_modules_request()) {
                        app.reload_behavior_modules(behavior_module_folder);
                        control->set_behavior_modules(app.behavior_module_names());
                    }
                }

                // Build this frame's input snapshot from the global input event
                // queue + elapsed dt, then drive the app (camera, sim).
                std::vector<wz::event::Event> frame_events;
                wz::event::Event input_event;
                while (wz::event::event_queue.try_pop(input_event)) {
                    frame_events.push_back(std::move(input_event));
                }
                prev_input = input;
                wz::input::ControllerInputSample controller_sample{};
                wz::platform::win32::controller_sample(controller_sample);
                wz::input::build_input(
                    input,
                    prev_input,
                    frame_events.data(),
                    frame_events.size(),
                    wz::time::Frame{},
                    controller_sample);

                // All the loop's timing rules live in compute_frame_delta so
                // they can be tested without a device: the monotonicity guard
                // (#313, B4-S2) and the clamp that keeps an unbounded pump stall
                // out of physics (#313, B4-C9 / B4-C7).
                const FrameDelta frame = compute_frame_delta(
                    last_ticks,
                    wz::time::TimeSource::now_ticks(),
                    wz::time::TimeSource::ticks_per_second());
                const float dt = frame.dt;
                last_ticks = frame.now;

                // Fly-cam enable toggle (ESC), driven entirely by the frame input
                // snapshot (no direct platform calls): ESC enables the camera only
                // while this window is focused (you can't arm it from the editor),
                // and ESC disables it from anywhere. The camera then consumes input
                // only while enabled AND focused, so clicking back to the editor
                // pauses it (never steals editor keystrokes) and re-focusing the
                // viewport resumes it.
                constexpr int kKeyEscape = 0x1B;  // VK_ESCAPE
                if (input.keyboard.pressed[kKeyEscape]) {
                    if (camera_enabled) {
                        camera_enabled = false;
                    }
                    else if (input.window.focused) {
                        camera_enabled = true;
                    }
                }

                // Behaviors get the real frame input whenever the viewport is
                // FOCUSED — so a controller/keyboard drives scene behaviors (e.g.
                // a tank) independent of the fly-cam. The fly-cam is a separate
                // opt-in (ESC): it only gates whether the CAMERA also consumes
                // that input, so panning the view and driving a behavior don't
                // fight. Unfocused -> a neutral snapshot carrying just the window
                // dimensions, so aspect still tracks resizes and the scene ignores
                // input meant for the editor.
                // This frame's inputs for the phase DAG's callbacks (frame_ops,
                // built above the loop). drive_camera / frame_input keep their
                // exact former meaning; the simulate-vs-paused choice and the
                // dt-recomputed-every-frame rule moved into frame_ops.simulate.
                frame_drive.drive_camera = camera_enabled && input.window.focused;
                frame_drive.input = wz::input::InputState{};
                if (input.window.focused) {
                    frame_drive.input = input;
                }
                else {
                    frame_drive.input.window = input.window;
                }
                frame_drive.dt = dt;

                // Run the frame as a job graph: simulate -> begin -> clear ->
                // render_scene -> render_overlay -> end -> present, stopping at the
                // first phase that fails (mirrors the former per-step break).
                frame_schedule.set_profiling(
                    env_frame_profiling || (control && control->frame_profiling_enabled()));
                const wz::engine::FrameSchedule::Result frame_result =
                    frame_schedule.run(frame_ops);
                if (!frame_result.ok) {
                    frame_error = true;
                    break;
                }

                // Live per-phase critical-path from real frames (#305), only while
                // the editor's frame-profiling toggle is on.
                if (frame_schedule.profiling() && (frames_presented % 120u == 0u)) {
                    ctx.logger.info(wz::jobs::format_frame_report_line(
                        frame_schedule.last_profile(),
                        frame_schedule.last_analysis(),
                        wz::time::TimeSource::ticks_per_second()));
                }

                // Bounded (verification) run: stop after the requested number of
                // presented frames.
                ++frames_presented;
                if (run_options.max_frames > 0
                    && frames_presented >= run_options.max_frames)
                {
                    break;
                }
            }

            // Bounded (verification) run: report whether the project actually
            // rendered — the scene loaded, at least one renderable resolved, and
            // all requested frames presented without error.
            if (run_options.max_frames > 0) {
                const bool rendered_ok =
                    scene_loaded
                    && !frame_error
                    && frames_presented >= run_options.max_frames
                    && app.resolved_renderable_node_count() > 0;
                exit_code = rendered_ok ? 0 : 1;
                if (!rendered_ok) {
                    ctx.logger.error(
                        "bounded run did not render: scene_loaded="
                        + std::to_string(scene_loaded ? 1 : 0)
                        + " frames=" + std::to_string(frames_presented) + "/"
                        + std::to_string(run_options.max_frames)
                        + " resolved_renderables="
                        + std::to_string(app.resolved_renderable_node_count())
                        + " frame_error="
                        + std::to_string(frame_error ? 1 : 0));
                }
            }

            // Quiesce the IO lane FIRST: stop accepting saves and wait out any
            // write still in flight. Everything below writes the scene file from
            // this thread, and commit_scene_save is a read-modify-write with no
            // lock and no atomic rename, so an interactive save still running on
            // the lane would be a second, unsynchronized writer of the same file.
            //
            // It also fixes the ordering: before this, a save still QUEUED at
            // teardown ran during teardown_lanes -- i.e. AFTER the teardown save
            // below -- and its older snapshot overwrote the newer file. And a save
            // completing after the drain below posted its result into a mailbox
            // with no consumer left, so a FAILED one never restored the dirty
            // flags prepare_scene_save() had already cleared, and the teardown save
            // then skipped as "not dirty": edits silently lost.
            //
            // Not a join -- the lane threads must outlive destroy_device (see
            // IoExecutor::quiesce and the AMD ordering note on teardown_lanes).
            io_executor->quiesce();

            // Now finalize every interactive save that finished, including any the
            // quiesce just waited out (#305 step 4b): a failed one restores the
            // scene dirty so the teardown save just below re-attempts it. Only the
            // editor path has a control; standalone play saves synchronously below.
            if (control) {
                control->drain_async_save_results(
                    [&app](const wz::app::AsyncSaveResult& r) {
                        app.finalize_scene_save(
                            r.err, r.restore_scene_dirty, r.restore_camera_dirty);
                    });
            }

            // Persist any unsaved edits before the runtime tears down (covers
            // the viewport window closing and the editor stopping the runtime).
            // Nothing waits on this result, so a failure is logged, not returned.
            if (const wz::fs::FileError err = app.save_scene();
                err != wz::fs::FileError::None) {
                ctx.logger.error(
                    std::string("save_scene (runtime teardown) failed: ")
                    + wz::fs::to_string(err));
            }

            wz::platform::win32::controller_shutdown();
            wz::input::shutdown_raw_input();
        }

        // The ordered teardown (logger quiesce -> engine shutdown -> lane join)
        // runs in ~RuntimeTeardown above, so it happens on this return and on an
        // unwind alike.
        return exit_code;
    }
}
