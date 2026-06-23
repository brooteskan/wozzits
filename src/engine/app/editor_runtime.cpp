// src/engine/app/editor_runtime.cpp

#include <engine/app/editor_runtime.h>

#include <engine/app_context.h>

#include <event/event.h>
#include <gpu/gpu.h>
#include <input/input.h>
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

    AssetGraphCompileResult EditorRuntimeControl::bind(
        wz::asset::AssetGraphDraft& draft)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !has_request_ || finished_; });
        if (finished_) {
            return bind_failed("engine runtime is not running");
        }

        pending_draft_ = std::move(draft);
        has_request_ = true;
        has_result_ = false;
        cv_.notify_all();

        cv_.wait(lock, [this] { return has_result_ || finished_; });
        if (!has_result_) {
            // The engine stopped. If it had not yet taken the request, hand the
            // draft back intact so the caller's authoring state survives.
            if (has_request_) {
                draft = std::move(pending_draft_);
                has_request_ = false;
            }
            return bind_failed("engine runtime stopped before bind completed");
        }

        has_result_ = false;
        draft = std::move(result_draft_);
        return std::move(result_);
    }

    void EditorRuntimeControl::service_pending_bind(
        const std::function<
            AssetGraphCompileResult(wz::asset::AssetGraphDraft&)>& binder)
    {
        wz::asset::AssetGraphDraft draft;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!has_request_) {
                return;
            }
            draft = std::move(pending_draft_);
            has_request_ = false;
        }

        // Bind outside the lock - it can take seconds (GPU resolve). binder
        // mutates `draft` in place (resolved keys + validation messages).
        AssetGraphCompileResult bound = binder(draft);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(bound);
            result_draft_ = std::move(draft);
            has_result_ = true;
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
        const wz::fs::Path& behavior_module_folder)
    {
        wz::engine::AppContext ctx;

        wz::engine::AppDesc desc;
        desc.window = { window_title.c_str(), 1280, 720, true, false };
        desc.resource_root = resource_root;

        if (!wz::engine::init(ctx, desc)) {
            return 1;  // init logged the failure
        }
        if (log_sink.write) {
            wz::logging::set_log_sink(
                ctx.logger,
                forward_editor_runtime_log,
                &log_sink);
            ctx.logger.info("editor resident engine log sink attached");
        }

        {
            wz::app::WozzitsApp_v1 app(ctx);

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

            if (!app.load_scene(wz::app::WozzitsAppSceneLoadDesc{
                    .asset_graph = asset_graph,
                    .scene = scene,
                    .behavior_module_folder = behavior_module_folder,
                }))
            {
                ctx.logger.error("load scene failed");
            }

            // Free-fly camera input: raw keyboard/mouse feed the global input
            // event queue, drained per frame into an InputState that drives the
            // app's camera (and any future sim) via simulation_tick.
            wz::input::init_raw_input();
            wz::input::InputState input{};
            wz::input::InputState prev_input{};
            wz::time::Tick last_ticks = wz::time::TimeSource::now_ticks();

            // Fly-cam enable state, toggled by ESC (see the in-loop toggle below).
            // Starts disabled: the runtime window receives GLOBAL raw input
            // (RIDEV_INPUTSINK), so without an explicit arm the camera would react
            // to keystrokes meant for the editor window.
            bool camera_enabled = false;

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
                    control->service_pending_bind(binder);
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
                    if (control->take_save_request()) {
                        app.save_scene();
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
                wz::input::build_input(
                    input,
                    prev_input,
                    frame_events.data(),
                    frame_events.size(),
                    wz::time::Frame{});

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

                // Feed the camera real input only when active; otherwise a neutral
                // snapshot carrying just the window dimensions, so the aspect ratio
                // still tracks resizes while the camera holds its pose.
                wz::input::InputState camera_input{};
                if (camera_enabled && input.window.focused) {
                    camera_input = input;
                }
                else {
                    camera_input.window = input.window;
                }

                app.simulation_tick(camera_input, dt);

                if (!wz::gpu::begin_frame(ctx.device)) {
                    ctx.logger.error("begin_frame failed");
                    break;
                }
                wz::gpu::clear(ctx.device, 0.10f, 0.10f, 0.12f, 1.0f);
                if (!app.render_scene()) {
                    break;
                }
                if (!wz::gpu::end_frame(ctx.device)) {
                    ctx.logger.error("end_frame failed");
                    break;
                }
                if (!wz::gpu::present(ctx.device)) {
                    ctx.logger.error("present failed");
                    break;
                }
            }

            // Persist any unsaved edits before the runtime tears down (covers
            // the viewport window closing and the editor stopping the runtime).
            app.save_scene();

            wz::input::shutdown_raw_input();
        }

        wz::engine::shutdown(ctx);
        return 0;
    }
}
