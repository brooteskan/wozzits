// src/engine/app/editor_runtime.cpp

#include <engine/app/editor_runtime.h>

#include <engine/app_context.h>

#include <gpu/gpu.h>
#include <window/window2.h>

#include <utility>

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

        // Bind outside the lock — it can take seconds (GPU resolve). binder
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

    int run_project_runtime(
        const std::string& window_title,
        const wz::fs::Path& asset_graph,
        const wz::fs::Path& scene,
        const wz::fs::Path& resource_root,
        EditorRuntimeControl* control)
    {
        wz::engine::AppContext ctx;

        wz::engine::AppDesc desc;
        desc.window = { window_title.c_str(), 1280, 720, true, false };
        desc.resource_root = resource_root;

        if (!wz::engine::init(ctx, desc)) {
            return 1;  // init logged the failure
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

                app.simulation_tick();

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
        }

        wz::engine::shutdown(ctx);
        return 0;
    }
}
