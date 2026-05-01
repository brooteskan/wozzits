#pragma once

// #include <cstdint>
#include <time/w_time.h>
#include <input/input.h>
// #include <render/render.h>

namespace wz::engine
{
    /**
     * @brief Global engine execution context (lifecycle state only).
     *
     * This object represents the long-lived engine state shared across frames.
     * It does NOT contain per-frame simulation data.
     *
     * The Context is passed into the engine update loop to allow control
     * of execution (e.g. shutdown requests).
     */
    struct Context
    {
        /**
         * @brief Controls whether the engine main loop continues running.
         *
         * Setting this to false requests a graceful shutdown at the end of the current frame.
         */
        bool running = true;
    };

    /**
     * @brief Per-frame execution data.
     *
     * This structure represents the temporal and per-frame simulation boundary.
     * It is recreated (or updated) once per engine tick and passed into the
     * user update function.
     *
     * All frame-synchronous systems (input, simulation, rendering submission)
     * should derive their behavior from this object.
     */
    struct FrameContext
    {
        /**
         * @brief Temporal frame descriptor (index + time interval).
         */
        wz::time::Frame frame{};
        

        /**
         * @brief Deterministic frame identity.
         */
        uint64_t seed{};

        wz::input::InputState input{};

        // wz::core::render::RenderIR render_ir{};
    };

    /**
     * @brief Per-frame update function signature.
     *
     * The engine calls this function once per frame.
     *
     * @param ctx      Engine lifecycle context (global state, shutdown control).
     * @param fctx     Per-frame simulation context (time + frame data).
     * @param user_data Opaque pointer provided by the application.
     *
     * @warning This is a low-level interface. The engine does not impose
     * any stage system (pre-update / update / post-update). All sequencing
     * must be handled externally if needed.
     */
    using UpdateFn = void (*)(Context &ctx, FrameContext &fctx, void *user_data);

    /**
     * @brief Runs the engine main loop.
     *
     * Continuously invokes the user-provided update function once per frame
     * until Context::running is set to false or shutdown() is called.
     *
     * @param update     User-defined per-frame update function.
     * @param user_data  Opaque pointer forwarded to the update function.
     */
    void run(UpdateFn update, void *user_data);

    /**
     * @brief Requests engine shutdown.
     *
     * Safe to call from any thread depending on implementation guarantees.
     */
    void shutdown();

    /**
     * @brief Retrieves the global engine context.
     *
     * @return Reference to the engine Context singleton.
     */
    Context &context();
}