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

    AssetGraphCompileResult EditorRuntimeControl::bind_asset_graph(
        wz::asset::AssetGraphDraft& draft)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock,
            [this] { return !has_asset_graph_request_ || finished_; });
        if (finished_) {
            return bind_failed("engine runtime is not running");
        }

        pending_asset_graph_draft_ = std::move(draft);
        has_asset_graph_request_ = true;
        has_asset_graph_result_ = false;
        cv_.notify_all();

        cv_.wait(lock,
            [this] { return has_asset_graph_result_ || finished_; });
        if (!has_asset_graph_result_) {
            // The engine stopped. If it had not yet taken the request, hand the
            // draft back intact so the caller's authoring state survives.
            if (has_asset_graph_request_) {
                draft = std::move(pending_asset_graph_draft_);
                has_asset_graph_request_ = false;
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
        }

        // Bind outside the lock - it can take seconds (GPU resolve). binder
        // mutates `draft` in place (resolved keys + validation messages).
        AssetGraphCompileResult bound = binder(draft);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            asset_graph_result_ = std::move(bound);
            result_asset_graph_draft_ = std::move(draft);
            has_asset_graph_result_ = true;
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
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !has_add_request_ || finished_; });
        if (finished_) {
            return wz::engine::assets::SceneAddChildResult{
                .ok = false,
                .new_id = {},
                .error = "engine runtime is not running",
            };
        }

        pending_add_parent_ = parent_id;
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
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !has_export_request_ || finished_; });
        if (finished_) {
            return false;  // runtime is not running — nothing to export
        }

        pending_export_root_ = root_node_id;
        pending_export_path_ = out_path;
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
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !has_open_scene_request_ || finished_; });
        if (finished_) {
            return false;  // runtime is not running
        }

        pending_open_scene_path_ = scene_path;
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
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(
            lock, [this] { return !has_add_behavior_request_ || finished_; });
        if (finished_) {
            return wz::engine::assets::SceneAddBehaviorResult{
                .ok = false,
                .binding_id = {},
                .error = "engine runtime is not running",
            };
        }

        pending_add_behavior_node_ = node_id;
        pending_add_behavior_module_ = module;
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

    std::vector<wz::engine::assets::SceneNodeAsset>
    EditorRuntimeControl::request_grafted_scene_nodes()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !has_grafted_request_ || finished_; });
        if (finished_) {
            return {};  // no runtime — caller falls back to its JSON tree
        }

        has_grafted_request_ = true;
        has_grafted_result_ = false;
        cv_.notify_all();

        cv_.wait(lock, [this] { return has_grafted_result_ || finished_; });
        if (!has_grafted_result_) {
            has_grafted_request_ = false;
            return {};  // engine stopped before servicing
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

    std::vector<wz::engine::assets::SceneNodeAsset>
    EditorRuntimeControl::request_scene_nodes()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !has_scene_nodes_request_ || finished_; });
        if (finished_) {
            return {};
        }

        has_scene_nodes_request_ = true;
        has_scene_nodes_result_ = false;
        cv_.notify_all();

        cv_.wait(lock, [this] { return has_scene_nodes_result_ || finished_; });
        if (!has_scene_nodes_result_) {
            has_scene_nodes_request_ = false;
            return {};
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
                        [&app](const SceneNodeTransformEdit& edit) {
                            app.set_node_transform(edit.id, edit.transform);
                        });
                    control->service_pending_scene_node_properties(
                        [&app](const SceneNodePropertiesEdit& edit) {
                            app.set_node_properties(
                                edit.id, edit.name, edit.visible);
                        });
                    control->service_pending_scene_node_reparents(
                        [&app](const SceneNodeReparentEdit& edit) {
                            app.reparent_node(edit.id, edit.new_parent_id);
                        });
                    control->service_pending_scene_node_removes(
                        [&app](const wz::scene::AuthoredEntityId& id) {
                            app.remove_node(id);
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
                        [&app](const SceneNodeRenderableEdit& edit) {
                            // Author the preferred asset-graph renderable (or
                            // clear it when asset_graph_node_id == 0).
                            app.set_node_renderable_asset(
                                edit.node_id, edit.asset_graph_node_id);
                        });
                    control->service_pending_scene_node_audio_renderables(
                        [&app](const SceneNodeAudioRenderableEdit& edit) {
                            // Author the AudioSource's renderable reference (or
                            // clear it when asset_graph_node_id == 0).
                            app.set_node_audio_renderable(
                                edit.node_id, edit.asset_graph_node_id);
                        });
                    control->service_pending_scene_node_audio_source_plays(
                        [&app](const SceneNodeAudioSourcePlayEdit& edit) {
                            app.set_node_audio_source_play(
                                edit.node_id, edit.auto_play, edit.enabled);
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
                        app.save_scene();
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

                const wz::time::Tick now_ticks =
                    wz::time::TimeSource::now_ticks();
                const float dt = static_cast<float>(
                    static_cast<double>(now_ticks - last_ticks)
                    / static_cast<double>(
                        wz::time::TimeSource::ticks_per_second()));
                last_ticks = now_ticks;

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

                app.simulation_tick(frame_input, dt, drive_camera);

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
            app.save_scene();

            wz::platform::win32::controller_shutdown();
            wz::input::shutdown_raw_input();
        }

        wz::engine::shutdown(ctx);
        return exit_code;
    }
}
