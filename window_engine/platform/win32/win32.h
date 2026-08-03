#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <gpu/gpu.h>
#include <window/window_types.h>
#include <event/platform_event.h>

namespace wz::platform::win32
{
    wz::gpu::Device create_dx12_device(const wz::window::WindowHandle& window);


    /**
     * @brief Opaque handle to a native Win32 window.
     *
     * This is intentionally incomplete in the header.
     * The implementation owns the actual structure.
     */
    struct NativeWindow;

    /**
     * @brief Creates a native window.
     */
    wz::window::WindowHandle w32_create_window(int width,
                                               int height,
                                               const char *title);

    /**
     * @brief Destroys a native window.
     */
    void w32_destroy_window(wz::window::WindowHandle window);

    /**
     * @brief Returns whether the window has requested close.
     */
    bool w32_window_should_close(wz::window::WindowHandle window);

    void* w32_native_window_handle(wz::window::WindowHandle window);

    bool w32_poll_event(wz::window::WindowHandle window, PlatformEvent &out_event);

    /**
     * @brief Pumps the Win32 message queue.
     */
    void w32_pump_messages();
}
