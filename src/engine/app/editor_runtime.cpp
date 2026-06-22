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
        EditorRuntimeLogSink log_sink)
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

                // Service a pending draft bind from the editor (compile/swap),
                // before sim + render so this frame reflects it.
                if (control) {
                    control->service_pending_bind(binder);
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

            wz::input::shutdown_raw_input();
        }

        wz::engine::shutdown(ctx);
        return 0;
    }
}
