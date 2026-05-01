/**
 * @file input.h
 * @brief Frame-sampled input state reconstruction system.
 *
 * The Wozzits input system converts:
 * - Discrete events (from event_queue)
 * - Platform device state (implicit OS signals)
 *
 * into a **single immutable InputState per frame**.
 *
 * ## Design Model
 *
 * Input is NOT a live stream.
 * Input is a **frame snapshot reconstruction**:
 *
 * - Events represent discrete transitions (edges)
 * - InputState represents the full reconstructed state at frame end
 * - Analog inputs are stored as smoothed state, not events
 *
 * ## Ownership Rules
 *
 * - Event system owns: discrete transitions
 * - Input system owns: frame-level state reconstruction
 * - OS layer owns: raw device state
 * - build_input owns: temporal consistency per frame
 */

#pragma once

#include <cstdint>
#include <event/event.h>

namespace wz::input
{
    void init_raw_input();   // calls into os platform
    void shutdown_raw_input();

    /**
     * @brief Keyboard state reconstructed per frame.
     *
     * Represents the full keyboard state at a given frame boundary.
     *
     * @note This is derived from discrete KeyDown / KeyUp events.
     */
    struct KeyboardState
    {
        bool down[256]{};     ///< Key is currently held
        bool pressed[256]{};  ///< Key transitioned down this frame
        bool released[256]{}; ///< Key transitioned up this frame
    };

    /**
     * @brief Mouse state reconstructed per frame.
     *
     * Contains both:
     * - discrete button state (from events)
     * - positional deltas (from platform signal sampling)
     *
     * @note Mouse movement is treated as a signal, not an event.
     */
    struct MouseState
    {
        int x = 0;  ///< Absolute or accumulated X position
        int y = 0;  ///< Absolute or accumulated Y position
        int dx = 0; ///< Delta X this frame
        int dy = 0; ///< Delta Y this frame

        bool down[3]{};     ///< Button held state
        bool pressed[3]{};  ///< Button down this frame
        bool released[3]{}; ///< Button up this frame
    };

    /**
     * @brief Window system state reconstructed per frame.
     */
    struct WindowState
    {
        bool focused = true; ///< Window focus state
        int width = 0;       ///< Current framebuffer width
        int height = 0;      ///< Current framebuffer height
    };

    /**
     * @brief Controller state reconstructed per frame.
     *
     * Contains both:
     * - discrete button events (buttons[])
     * - continuous analog signals (axes[])
     *
     * @note Axes values are expected to be pre-conditioned
     * (EMA / deadzone / slew filtering applied upstream).
     */
    struct ControllerState
    {
        bool connected = false; ///< Controller connection state

        float axes[8]{};    ///< Analog axis values (smoothed signal)
        bool buttons[16]{}; ///< Digital button state
    };

    /**
     * @brief Complete input snapshot for a single frame.
     *
     * This is the **only structure consumed by simulation**.
     *
     * It represents a fully resolved view of:
     * - discrete input events
     * - continuous input state
     * - platform-derived window state
     */
    struct InputState
    {
        KeyboardState keyboard;
        MouseState mouse;
        WindowState window;
        ControllerState controller;
    };

    /**
     * @brief Builds a frame-synchronized InputState.
     *
     * This function consumes:
     * - event stream (discrete transitions)
     * - implicit platform signal state (mouse/controller/window)
     *
     * and produces a deterministic snapshot valid for [frame_start, frame_end).
     *
     * ## Responsibilities
     * - drain event queue for frame window
     * - apply events to discrete state
     * - integrate platform signal state
     * - produce final InputState snapshot
     *
     * ## Guarantees
     * - deterministic per frame
     * - does not mutate event_queue
     * - does not allocate memory
     * - does not persist state across frames
     *
     * @param[out] input Output frame snapshot
     * @param events Pointer to event buffer
     * @param count Number of events
     * @param frame_start Start of frame time window
     * @param frame_end End of frame time window
     */
    void build_input(InputState &input,
                     const InputState& prev,
                     const wz::event::Event *events,
                     size_t count,
                     wz::time::Frame frame_startframe);
}