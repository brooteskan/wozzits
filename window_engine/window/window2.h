#pragma once
#include <window/window_types.h>
#include <event/platform_event.h>

namespace wz::window
{
    /**
     * @brief Creates a native window using the active platform backend.
     *
     * This is an engine-facing abstraction.
     * The underlying implementation is platform-specific (Win32, etc).
     */
    WindowHandle create_window(const WindowDesc &desc);

    /**
     * @brief Destroys a previously created window.
     */
    void destroy_window(WindowHandle window);

    /**
     * @brief Queries whether the window has requested close.
     *
     * This reflects platform state (e.g. WM_CLOSE on Win32).
     */
    bool window_should_close(WindowHandle window);

    /**
     * @brief Polls a single window event (if available).
     *
     * @return true if an event was written to out_event.
     * @return false if no events are available.
     */
    bool poll_event(WindowHandle window, PlatformEvent &out_event);

    /**
     * @brief Pumps OS-level messages (platform loop integration).
     *
     * This processes native OS message queues (e.g. Win32 PeekMessage).
     */
    void pump_messages();

    static inline void* get_native_handle(const WindowHandle& window)
    {
        return window.native;
    }

    // for imgui tooling
    void set_native_message_hook(
        WindowHandle window,
        NativeMessageHook hook,
        void* user);
}