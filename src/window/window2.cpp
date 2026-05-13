#include <window/window2.h>
#include <platform/win32/win32.h>
#include <event/platform_event.h>

namespace wz::window
{
    WindowHandle create_window(const WindowDesc &desc)
    {
        return wz::platform::win32::w32_create_window(
            desc.width,
            desc.height,
            desc.title);
    }

    void destroy_window(WindowHandle window)
    {
        wz::platform::win32::w32_destroy_window(window);
    }

    bool window_should_close(WindowHandle window)
    {
        return wz::platform::win32::w32_window_should_close(window);
    }

    bool poll_event(WindowHandle window, PlatformEvent &out_event)
    {
        return wz::platform::win32::w32_poll_event(window, out_event);
    }

    void pump_messages()
    {
        wz::platform::win32::w32_pump_messages();
    }

    void set_native_message_hook(
        WindowHandle window,
        NativeMessageHook hook,
        void* user)
    {
        wz::platform::win32::w32_set_native_message_hook(
            window,
            hook,
            user);
    }

}