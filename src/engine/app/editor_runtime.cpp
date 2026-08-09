// src/engine/app/editor_runtime.cpp

#include <engine/app/editor_runtime.h>

#include <engine/app_context.h>

#include <event/event.h>
#include <gpu/gpu.h>
#include <input/input.h>
#include <platform/win32/controller_win32.h>
#include <logging/logger.h>
#include <time/w_time.h>
#include <window/window2.h>

#include <string_view>
#include <utility>
#include <vector>

namespace wz::app
{
    namespace
    {
        // Holds a blocking handshake's cycle open until the caller has TAKEN
        // its result, then wakes the next caller (#313, B4-C2 — see the
        // *_cycle_busy_ comment in editor_runtime.h for what goes wrong
        // without it).
        //
        // A guard rather than an explicit clear at each return because these
        // functions have three exits apiece — success, engine-stopped-before-
        // claiming, engine-stopped-mid-flight — and leaking the flag on any one
        // of them wedges that verb for the rest of the session, which is a
        // worse failure than the bug being fixed.
        //
        // Runs while the caller's unique_lock is still held: the lock is
        // declared before the guard, so it is destroyed after it. Notifying
        // under the lock is intentional and harmless here.
        struct HandshakeCycle
        {
            bool& busy;
            std::condition_variable& cv;

            HandshakeCycle(bool& busy_flag, std::condition_variable& cond)
                : busy(busy_flag)
                , cv(cond)
            {
                busy = true;
            }

            HandshakeCycle(const HandshakeCycle&) = delete;
            HandshakeCycle& operator=(const HandshakeCycle&) = delete;

            ~HandshakeCycle()
            {
                busy = false;
                cv.notify_all();
            }
        };

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
        stop_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(mutex_);
        cv_.notify_all();
    }

    bool EditorRuntimeControl::stop_requested() const
    {
        return stop_.load(std::memory_order_acquire);
    }

    void EditorRuntimeControl::request_save()
    {
        save_requested_.store(true, std::memory_order_release);
    }

    bool EditorRuntimeControl::take_save_request()
    {
        return save_requested_.exchange(false, std::memory_order_acq_rel);
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
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock,
            [this] { return !asset_graph_cycle_busy_ || finished_; });
        if (!scope.admitted() || finished_) {
            return bind_failed("engine runtime is not running");
        }

        const HandshakeCycle cycle(asset_graph_cycle_busy_, cv_);
        pending_asset_graph_draft_ = std::move(draft);
        has_asset_graph_request_ = true;
        has_asset_graph_result_ = false;
        cv_.notify_all();

        // A stopping engine must not release this wait while the draft is still
        // in flight on the engine thread — there would be nothing to hand back.
        cv_.wait(lock,
            [this] {
                return has_asset_graph_result_
                    || (finished_ && !asset_graph_draft_in_flight_);
            });
        if (!has_asset_graph_result_) {
            // The engine stopped. Reclaim the draft from whichever slot holds it
            // so the caller's authoring state survives: pending_ when the engine
            // never claimed the request, result_ when it claimed the draft and
            // handed it back unbound. Returning with the caller's draft still
            // moved-from loses the whole authored graph the next time the
            // session saves.
            if (has_asset_graph_request_) {
                draft = std::move(pending_asset_graph_draft_);
                has_asset_graph_request_ = false;
            }
            else {
                draft = std::move(result_asset_graph_draft_);
            }
            return bind_failed("engine runtime stopped before bind completed");
        }

        has_asset_graph_result_ = false;
        draft = std::move(result_asset_graph_draft_);
        return std::move(asset_graph_result_);
    }

    void EditorRuntimeControl::service_pending_asset_graph_bind(
        const std::function<
            AssetGraphCompileResult(wz::asset::AssetGraphDraft&)>& binder)
    {
        wz::asset::AssetGraphDraft draft;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!has_asset_graph_request_) {
                return;
            }
            draft = std::move(pending_asset_graph_draft_);
            has_asset_graph_request_ = false;
            asset_graph_draft_in_flight_ = true;
        }

        // From here the caller's draft exists ONLY in `draft`, on this thread:
        // the request slot is empty and no result is published yet. If binder
        // throws, or the runtime tears down before the publish below, this guard
        // is what returns the authored graph to the waiting caller.
        struct DraftHandback
        {
            EditorRuntimeControl* control;
            wz::asset::AssetGraphDraft* draft;

            ~DraftHandback()
            {
                {
                    std::lock_guard<std::mutex> lock(control->mutex_);
                    if (!control->asset_graph_draft_in_flight_) {
                        return;  // the publish already handed it back
                    }
                    control->result_asset_graph_draft_ = std::move(*draft);
                    control->asset_graph_draft_in_flight_ = false;
                }
                control->cv_.notify_all();
            }
        } handback{ this, &draft };

        // Bind outside the lock - it can take seconds (GPU resolve). binder
        // mutates `draft` in place (resolved keys + validation messages).
        AssetGraphCompileResult bound = binder(draft);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            asset_graph_result_ = std::move(bound);
            result_asset_graph_draft_ = std::move(draft);
            has_asset_graph_result_ = true;
            asset_graph_draft_in_flight_ = false;
        }
        cv_.notify_all();
    }

    void EditorRuntimeControl::post_scene_node_transform(
        SceneNodeTransformEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (SceneNodeTransformEdit& pending : pending_transforms_) {
            if (pending.id == edit.id) {
                pending.transform = edit.transform;  // coalesce: latest wins
                return;
            }
        }
        pending_transforms_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_transforms(
        const std::function<void(const SceneNodeTransformEdit&)>& applier)
    {
        std::vector<SceneNodeTransformEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_transforms_.empty()) {
                return;
            }
            edits.swap(pending_transforms_);
        }

        // Apply outside the lock: applier mutates the app/renderer and a post
        // from the owner thread must never block on it.
        for (const SceneNodeTransformEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_properties(
        SceneNodePropertiesEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (SceneNodePropertiesEdit& pending : pending_properties_) {
            if (pending.id == edit.id) {
                pending.name = std::move(edit.name);  // coalesce: latest wins
                pending.visible = edit.visible;
                return;
            }
        }
        pending_properties_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_properties(
        const std::function<void(const SceneNodePropertiesEdit&)>& applier)
    {
        std::vector<SceneNodePropertiesEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_properties_.empty()) {
                return;
            }
            edits.swap(pending_properties_);
        }

        for (const SceneNodePropertiesEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_reparent(
        SceneNodeReparentEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (SceneNodeReparentEdit& pending : pending_reparents_) {
            if (pending.id == edit.id) {
                pending.new_parent_id = std::move(edit.new_parent_id);
                return;
            }
        }
        pending_reparents_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_reparents(
        const std::function<void(const SceneNodeReparentEdit&)>& applier)
    {
        std::vector<SceneNodeReparentEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_reparents_.empty()) {
                return;
            }
            edits.swap(pending_reparents_);
        }

        for (const SceneNodeReparentEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_reorder(
        SceneNodeReorderEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (SceneNodeReorderEdit& pending : pending_reorders_) {
            if (pending.id == edit.id) {
                pending.before_id = std::move(edit.before_id);
                return;
            }
        }
        pending_reorders_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_reorders(
        const std::function<void(const SceneNodeReorderEdit&)>& applier)
    {
        std::vector<SceneNodeReorderEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_reorders_.empty()) {
                return;
            }
            edits.swap(pending_reorders_);
        }

        for (const SceneNodeReorderEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_render_order(
        SceneNodeRenderOrderEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (SceneNodeRenderOrderEdit& pending : pending_render_orders_) {
            if (pending.id == edit.id) {
                pending.render_order = edit.render_order;
                return;
            }
        }
        pending_render_orders_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_render_orders(
        const std::function<void(const SceneNodeRenderOrderEdit&)>& applier)
    {
        std::vector<SceneNodeRenderOrderEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_render_orders_.empty()) {
                return;
            }
            edits.swap(pending_render_orders_);
        }

        for (const SceneNodeRenderOrderEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_remove(
        wz::scene::AuthoredEntityId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const wz::scene::AuthoredEntityId& pending : pending_removes_) {
            if (pending == id) {
                return;  // dedup
            }
        }
        pending_removes_.push_back(std::move(id));
    }

    void EditorRuntimeControl::service_pending_scene_node_removes(
        const std::function<void(const wz::scene::AuthoredEntityId&)>& applier)
    {
        std::vector<wz::scene::AuthoredEntityId> removes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_removes_.empty()) {
                return;
            }
            removes.swap(pending_removes_);
        }

        for (const wz::scene::AuthoredEntityId& id : removes) {
            applier(id);
        }
    }

    void EditorRuntimeControl::post_scene_node_behavior(
        SceneNodeBehaviorEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: each op is a distinct mutation.
        pending_behavior_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_behaviors(
        const std::function<void(const SceneNodeBehaviorEdit&)>& applier)
    {
        std::vector<SceneNodeBehaviorEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_behavior_edits_.empty()) {
                return;
            }
            edits.swap(pending_behavior_edits_);
        }

        for (const SceneNodeBehaviorEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_component(
        SceneNodeComponentEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: each op is a distinct mutation.
        pending_component_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_components(
        const std::function<void(const SceneNodeComponentEdit&)>& applier)
    {
        std::vector<SceneNodeComponentEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_component_edits_.empty()) {
                return;
            }
            edits.swap(pending_component_edits_);
        }

        for (const SceneNodeComponentEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_renderable(
        SceneNodeRenderableEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: a set then a clear must both land.
        pending_renderable_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_renderables(
        const std::function<void(const SceneNodeRenderableEdit&)>& applier)
    {
        std::vector<SceneNodeRenderableEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_renderable_edits_.empty()) {
                return;
            }
            edits.swap(pending_renderable_edits_);
        }

        for (const SceneNodeRenderableEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_audio_renderable(
        SceneNodeAudioRenderableEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_audio_renderable_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_audio_renderables(
        const std::function<void(const SceneNodeAudioRenderableEdit&)>& applier)
    {
        std::vector<SceneNodeAudioRenderableEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_audio_renderable_edits_.empty()) {
                return;
            }
            edits.swap(pending_audio_renderable_edits_);
        }

        for (const SceneNodeAudioRenderableEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_audio_source_play(
        SceneNodeAudioSourcePlayEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_audio_source_play_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_audio_source_plays(
        const std::function<void(const SceneNodeAudioSourcePlayEdit&)>& applier)
    {
        std::vector<SceneNodeAudioSourcePlayEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_audio_source_play_edits_.empty()) {
                return;
            }
            edits.swap(pending_audio_source_play_edits_);
        }

        for (const SceneNodeAudioSourcePlayEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_render_binding(
        SceneNodeRenderBindingEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: a set then a clear must both land.
        pending_render_binding_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_render_bindings(
        const std::function<void(const SceneNodeRenderBindingEdit&)>& applier)
    {
        std::vector<SceneNodeRenderBindingEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_render_binding_edits_.empty()) {
                return;
            }
            edits.swap(pending_render_binding_edits_);
        }

        for (const SceneNodeRenderBindingEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_renderable_binding(
        SceneNodeRenderableBindingEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: an upsert then a remove of the
        // same semantic must both land.
        pending_renderable_binding_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_renderable_bindings(
        const std::function<
            void(const SceneNodeRenderableBindingEdit&)>& applier)
    {
        std::vector<SceneNodeRenderableBindingEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_renderable_binding_edits_.empty()) {
                return;
            }
            edits.swap(pending_renderable_binding_edits_);
        }

        for (const SceneNodeRenderableBindingEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_renderable_param(
        SceneNodeRenderableParamEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: an upsert then a remove of the
        // same name must both land.
        pending_renderable_param_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_renderable_params(
        const std::function<
            void(const SceneNodeRenderableParamEdit&)>& applier)
    {
        std::vector<SceneNodeRenderableParamEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_renderable_param_edits_.empty()) {
                return;
            }
            edits.swap(pending_renderable_param_edits_);
        }

        for (const SceneNodeRenderableParamEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_collision(
        SceneNodeCollisionEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: a set then a clear must both land.
        pending_collision_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_collisions(
        const std::function<void(const SceneNodeCollisionEdit&)>& applier)
    {
        std::vector<SceneNodeCollisionEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_collision_edits_.empty()) {
                return;
            }
            edits.swap(pending_collision_edits_);
        }

        for (const SceneNodeCollisionEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_motion_terrain(
        SceneNodeMotionTerrainEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced.
        pending_motion_terrain_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_motion_terrains(
        const std::function<void(const SceneNodeMotionTerrainEdit&)>& applier)
    {
        std::vector<SceneNodeMotionTerrainEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_motion_terrain_edits_.empty()) {
                return;
            }
            edits.swap(pending_motion_terrain_edits_);
        }

        for (const SceneNodeMotionTerrainEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_motion_filter(
        SceneNodeMotionFilterEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Coalesce by id -- a slider drag streams many, only the latest matters.
        for (SceneNodeMotionFilterEdit& pending : pending_motion_filter_edits_) {
            if (pending.node_id == edit.node_id) {
                pending.filter = edit.filter;
                return;
            }
        }
        pending_motion_filter_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_motion_filters(
        const std::function<void(const SceneNodeMotionFilterEdit&)>& applier)
    {
        std::vector<SceneNodeMotionFilterEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_motion_filter_edits_.empty()) {
                return;
            }
            edits.swap(pending_motion_filter_edits_);
        }

        for (const SceneNodeMotionFilterEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_camera(
        SceneNodeCameraEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Coalesce by id -- an fov/far drag streams many, only the latest matters.
        for (SceneNodeCameraEdit& pending : pending_camera_edits_) {
            if (pending.node_id == edit.node_id) {
                pending.camera = edit.camera;
                return;
            }
        }
        pending_camera_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_cameras(
        const std::function<void(const SceneNodeCameraEdit&)>& applier)
    {
        std::vector<SceneNodeCameraEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_camera_edits_.empty()) {
                return;
            }
            edits.swap(pending_camera_edits_);
        }

        for (const SceneNodeCameraEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_atmosphere(
        SceneNodeAtmosphereEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Coalesce by id -- toggling enabled / re-picking the asset streams a few,
        // only the latest matters.
        for (SceneNodeAtmosphereEdit& pending : pending_atmosphere_edits_) {
            if (pending.node_id == edit.node_id) {
                pending.atmosphere = edit.atmosphere;
                return;
            }
        }
        pending_atmosphere_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_atmospheres(
        const std::function<void(const SceneNodeAtmosphereEdit&)>& applier)
    {
        std::vector<SceneNodeAtmosphereEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_atmosphere_edits_.empty()) {
                return;
            }
            edits.swap(pending_atmosphere_edits_);
        }

        for (const SceneNodeAtmosphereEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_environment(
        SceneNodeEnvironmentEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Coalesce by id -- toggling enabled / re-picking the asset streams a few,
        // only the latest matters.
        for (SceneNodeEnvironmentEdit& pending : pending_environment_edits_) {
            if (pending.node_id == edit.node_id) {
                pending.environment = edit.environment;
                return;
            }
        }
        pending_environment_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_environments(
        const std::function<void(const SceneNodeEnvironmentEdit&)>& applier)
    {
        std::vector<SceneNodeEnvironmentEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_environment_edits_.empty()) {
                return;
            }
            edits.swap(pending_environment_edits_);
        }

        for (const SceneNodeEnvironmentEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_render_to_texture(
        SceneNodeRenderToTextureEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Coalesce by id -- toggling a switch / re-picking the target streams a
        // few, only the latest matters.
        for (SceneNodeRenderToTextureEdit& pending :
             pending_render_to_texture_edits_)
        {
            if (pending.node_id == edit.node_id) {
                pending.render_to_texture = edit.render_to_texture;
                return;
            }
        }
        pending_render_to_texture_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_render_to_textures(
        const std::function<void(const SceneNodeRenderToTextureEdit&)>& applier)
    {
        std::vector<SceneNodeRenderToTextureEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_render_to_texture_edits_.empty()) {
                return;
            }
            edits.swap(pending_render_to_texture_edits_);
        }

        for (const SceneNodeRenderToTextureEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_scene_source(
        SceneNodeSceneSourceEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: a set then a clear must both land.
        pending_scene_source_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_scene_sources(
        const std::function<void(const SceneNodeSceneSourceEdit&)>& applier)
    {
        std::vector<SceneNodeSceneSourceEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_scene_source_edits_.empty()) {
                return;
            }
            edits.swap(pending_scene_source_edits_);
        }

        for (const SceneNodeSceneSourceEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_glb_scene_source(
        SceneNodeGlbSceneSourceEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: a set then a clear must both land.
        pending_glb_scene_source_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_glb_scene_sources(
        const std::function<void(const SceneNodeGlbSceneSourceEdit&)>& applier)
    {
        std::vector<SceneNodeGlbSceneSourceEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_glb_scene_source_edits_.empty()) {
                return;
            }
            edits.swap(pending_glb_scene_source_edits_);
        }

        for (const SceneNodeGlbSceneSourceEdit& edit : edits) {
            applier(edit);
        }
    }

    void EditorRuntimeControl::post_scene_node_glb_style(
        SceneNodeGlbStyleEdit edit)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Appended in order, never coalesced: distinct meshes / set-then-clear
        // must all land.
        pending_glb_style_edits_.push_back(std::move(edit));
    }

    void EditorRuntimeControl::service_pending_scene_node_glb_styles(
        const std::function<void(const SceneNodeGlbStyleEdit&)>& applier)
    {
        std::vector<SceneNodeGlbStyleEdit> edits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_glb_style_edits_.empty()) {
                return;
            }
            edits.swap(pending_glb_style_edits_);
        }

        for (const SceneNodeGlbStyleEdit& edit : edits) {
            applier(edit);
        }
    }

    wz::engine::assets::SceneAddChildResult EditorRuntimeControl::add_child(
        const wz::scene::AuthoredEntityId& parent_id)
    {
        const CallerScope scope(*this);
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !add_cycle_busy_ || finished_; });
        if (!scope.admitted() || finished_) {
            return wz::engine::assets::SceneAddChildResult{
                .ok = false,
                .new_id = {},
                .error = "engine runtime is not running",
            };
        }

        pending_add_parent_ = parent_id;
        const HandshakeCycle cycle(add_cycle_busy_, cv_);
        has_add_request_ = true;
        has_add_result_ = false;
        cv_.notify_all();

        cv_.wait(lock, [this] { return has_add_result_ || finished_; });
        if (!has_add_result_) {
            has_add_request_ = false;
            return wz::engine::assets::SceneAddChildResult{
                .ok = false,
                .new_id = {},
                .error = "engine runtime stopped before add completed",
            };
        }

        has_add_result_ = false;
        return std::move(add_result_);
    }

    void EditorRuntimeControl::service_pending_add_child(
        const std::function<wz::engine::assets::SceneAddChildResult(
            const wz::scene::AuthoredEntityId&)>& adder)
    {
        wz::scene::AuthoredEntityId parent;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!has_add_request_) {
                return;
            }
            parent = std::move(pending_add_parent_);
            has_add_request_ = false;
        }

        wz::engine::assets::SceneAddChildResult applied = adder(parent);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            add_result_ = std::move(applied);
            has_add_result_ = true;
        }
        cv_.notify_all();
    }

    bool EditorRuntimeControl::export_subtree_as_scene(
        const wz::scene::AuthoredEntityId& root_node_id,
        const wz::fs::Path& out_path)
    {
        const CallerScope scope(*this);
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !export_cycle_busy_ || finished_; });
        if (!scope.admitted() || finished_) {
            return false;  // runtime is not running — nothing to export
        }

        pending_export_root_ = root_node_id;
        pending_export_path_ = out_path;
        const HandshakeCycle cycle(export_cycle_busy_, cv_);
        has_export_request_ = true;
        has_export_result_ = false;
        cv_.notify_all();

        cv_.wait(lock, [this] { return has_export_result_ || finished_; });
        if (!has_export_result_) {
            has_export_request_ = false;
            return false;  // stopped before the export completed
        }

        has_export_result_ = false;
        return export_result_;
    }

    void EditorRuntimeControl::service_pending_export_subtree(
        const std::function<bool(
            const wz::scene::AuthoredEntityId&, const wz::fs::Path&)>& exporter)
    {
        wz::scene::AuthoredEntityId root;
        wz::fs::Path path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!has_export_request_) {
                return;
            }
            root = std::move(pending_export_root_);
            path = std::move(pending_export_path_);
            has_export_request_ = false;
        }

        const bool applied = exporter(root, path);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            export_result_ = applied;
            has_export_result_ = true;
        }
        cv_.notify_all();
    }

    bool EditorRuntimeControl::open_scene(const wz::fs::Path& scene_path)
    {
        const CallerScope scope(*this);
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !open_scene_cycle_busy_ || finished_; });
        if (!scope.admitted() || finished_) {
            return false;  // runtime is not running
        }

        pending_open_scene_path_ = scene_path;
        const HandshakeCycle cycle(open_scene_cycle_busy_, cv_);
        has_open_scene_request_ = true;
        has_open_scene_result_ = false;
        cv_.notify_all();

        cv_.wait(lock, [this] { return has_open_scene_result_ || finished_; });
        if (!has_open_scene_result_) {
            has_open_scene_request_ = false;
            return false;  // stopped before the open completed
        }

        has_open_scene_result_ = false;
        return open_scene_result_;
    }

    void EditorRuntimeControl::service_pending_open_scene(
        const std::function<bool(const wz::fs::Path&)>& opener)
    {
        wz::fs::Path path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!has_open_scene_request_) {
                return;
            }
            path = std::move(pending_open_scene_path_);
            has_open_scene_request_ = false;
        }

        const bool loaded = opener(path);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            open_scene_result_ = loaded;
            has_open_scene_result_ = true;
        }
        cv_.notify_all();
    }

    wz::engine::assets::SceneAddBehaviorResult
    EditorRuntimeControl::add_node_behavior(
        const wz::scene::AuthoredEntityId& node_id,
        const std::string& module)
    {
        const CallerScope scope(*this);
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(
            lock, [this] { return !add_behavior_cycle_busy_ || finished_; });
        if (!scope.admitted() || finished_) {
            return wz::engine::assets::SceneAddBehaviorResult{
                .ok = false,
                .binding_id = {},
                .error = "engine runtime is not running",
            };
        }

        pending_add_behavior_node_ = node_id;
        pending_add_behavior_module_ = module;
        const HandshakeCycle cycle(add_behavior_cycle_busy_, cv_);
        has_add_behavior_request_ = true;
        has_add_behavior_result_ = false;
        cv_.notify_all();

        cv_.wait(
            lock, [this] { return has_add_behavior_result_ || finished_; });
        if (!has_add_behavior_result_) {
            has_add_behavior_request_ = false;
            return wz::engine::assets::SceneAddBehaviorResult{
                .ok = false,
                .binding_id = {},
                .error = "engine runtime stopped before add completed",
            };
        }

        has_add_behavior_result_ = false;
        return std::move(add_behavior_result_);
    }

    void EditorRuntimeControl::service_pending_add_node_behavior(
        const std::function<wz::engine::assets::SceneAddBehaviorResult(
            const wz::scene::AuthoredEntityId&, const std::string&)>& adder)
    {
        wz::scene::AuthoredEntityId node;
        std::string module;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!has_add_behavior_request_) {
                return;
            }
            node = std::move(pending_add_behavior_node_);
            module = std::move(pending_add_behavior_module_);
            has_add_behavior_request_ = false;
        }

        wz::engine::assets::SceneAddBehaviorResult applied =
            adder(node, module);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            add_behavior_result_ = std::move(applied);
            has_add_behavior_result_ = true;
        }
        cv_.notify_all();
    }

    std::optional<std::vector<wz::engine::assets::SceneNodeAsset>>
    EditorRuntimeControl::request_grafted_scene_nodes()
    {
        const CallerScope scope(*this);
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !grafted_cycle_busy_ || finished_; });
        if (!scope.admitted() || finished_) {
            // NO ANSWER, not "nothing grafted" (D3-P039). Both used to return an
            // empty vector, so the caller could not tell them apart.
            return std::nullopt;
        }

        const HandshakeCycle cycle(grafted_cycle_busy_, cv_);
        has_grafted_request_ = true;
        has_grafted_result_ = false;
        cv_.notify_all();

        cv_.wait(lock, [this] { return has_grafted_result_ || finished_; });
        if (!has_grafted_result_) {
            has_grafted_request_ = false;
            return std::nullopt;  // engine stopped before servicing
        }

        has_grafted_result_ = false;
        return std::move(grafted_result_);
    }

    void EditorRuntimeControl::service_pending_grafted_scene_nodes(
        const std::function<
            std::vector<wz::engine::assets::SceneNodeAsset>()>& provider)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!has_grafted_request_) {
                return;
            }
            has_grafted_request_ = false;
        }

        // Build the copy outside the lock (it walks scene_nodes_); a request
        // from the owner thread must never block on it.
        std::vector<wz::engine::assets::SceneNodeAsset> nodes = provider();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            grafted_result_ = std::move(nodes);
            has_grafted_result_ = true;
        }
        cv_.notify_all();
    }

    std::optional<std::vector<wz::engine::assets::SceneNodeAsset>>
    EditorRuntimeControl::request_scene_nodes()
    {
        const CallerScope scope(*this);
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !scene_nodes_cycle_busy_ || finished_; });
        if (!scope.admitted() || finished_) {
            // NO ANSWER, not "the scene is empty" (D3-P039).
            return std::nullopt;
        }

        const HandshakeCycle cycle(scene_nodes_cycle_busy_, cv_);
        has_scene_nodes_request_ = true;
        has_scene_nodes_result_ = false;
        cv_.notify_all();

        cv_.wait(lock, [this] { return has_scene_nodes_result_ || finished_; });
        if (!has_scene_nodes_result_) {
            has_scene_nodes_request_ = false;
            return std::nullopt;
        }

        has_scene_nodes_result_ = false;
        return std::move(scene_nodes_result_);
    }

    void EditorRuntimeControl::service_pending_scene_nodes(
        const std::function<
            std::vector<wz::engine::assets::SceneNodeAsset>()>& provider)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!has_scene_nodes_request_) {
                return;
            }
            has_scene_nodes_request_ = false;
        }

        std::vector<wz::engine::assets::SceneNodeAsset> nodes = provider();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            scene_nodes_result_ = std::move(nodes);
            has_scene_nodes_result_ = true;
        }
        cv_.notify_all();
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
        cv_.notify_all();
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

        if (!wz::engine::init(ctx, desc)) {
            // Bounded (verification) runs distinguish "no GPU device" from a real
            // failure so a separate-process test can be SKIPPED rather than
            // failed on a machine without a device.
            return run_options.max_frames > 0 ? kRuntimeNoDeviceExitCode : 1;
        }
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
                    if (control->take_save_request()) {
                        // Fire-and-forget across the ABI today: the result cannot
                        // ride back to the editor yet (that uniform channel is
                        // #300), so log a failed save here rather than discard it.
                        if (const wz::fs::FileError err = app.save_scene();
                            err != wz::fs::FileError::None) {
                            ctx.logger.error(
                                std::string("save_scene (editor request) "
                                            "failed: ")
                                + wz::fs::to_string(err));
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
                const bool drive_camera = camera_enabled && input.window.focused;
                wz::input::InputState frame_input{};
                if (input.window.focused) {
                    frame_input = input;
                }
                else {
                    frame_input.window = input.window;
                }

                // Skip the simulation while paused (editor "pause simulation"): the render
                // path below keeps running and re-presents the last simulated frame, so the
                // viewport stays live but agents/behaviors/time stop. dt is recomputed every
                // frame regardless, so resuming does not produce a catch-up spike.
                if (!control || !control->paused()) {
                    app.simulation_tick(frame_input, dt, drive_camera);
                }
                else {
                    // Paused freezes the SCENE, not the VIEWPORT (#313, B4-C8).
                    // The gate used to skip all of simulation_tick, which took
                    // the projection aspect and the fly-cam with it: resizing
                    // while paused stretched the image because the swapchain
                    // resized and the aspect did not, and you could not look
                    // around the scene you had paused to inspect. Both live in
                    // update_view now; materialize_active_view is what carries
                    // the result into the render path, and with behaviors not
                    // running there is nothing to order it after.
                    app.paused_frame_tick(frame_input, dt, drive_camera);
                }

                if (!wz::gpu::begin_frame(ctx.device)) {
                    ctx.logger.error("begin_frame failed");
                    frame_error = true;
                    break;
                }
                wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
                if (!app.render_scene()) {
                    frame_error = true;
                    break;
                }
                // S6 3D-mesh consumer: puppet(s) rendered to an offscreen texture
                // and shown on a spinning card over the scene (no-op when the
                // showcase is off or the scene has no puppet).
                if (!app.render_puppet_showcase()) {
                    ctx.logger.error("render_puppet_showcase failed");
                    frame_error = true;
                    break;
                }
                if (!wz::gpu::end_frame(ctx.device)) {
                    ctx.logger.error("end_frame failed");
                    frame_error = true;
                    break;
                }
                if (!wz::gpu::present(ctx.device)) {
                    ctx.logger.error("present failed");
                    frame_error = true;
                    break;
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

        wz::engine::shutdown(ctx);
        return exit_code;
    }
}
