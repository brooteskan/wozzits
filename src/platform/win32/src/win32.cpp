// file: platform/win32/src/win32.cpp

#include <platform/win32/win32.h>
#include <event/platform_event.h>
#include <event/event.h>
#include <gpu/dx12/dx12.h>
#include <gpu/gpu.h>
#include <dwmapi.h>
#include <Windowsx.h>
#include <cassert>

// This is safe because SPSCQueue is core infrastructure, not window system logic.
#include <containers/spsc_queue.h>
// Keeping the bridge to wozzits out of the win32 stuff

namespace
{
    // This is safe because SPSCQueue is core infrastructure, not window system logic.
    // This is the queue for "window events" only! 
    using PlatformEventQueue = wz::core::containers::SPSCQueue<PlatformEvent>;

    inline void push_window_event(PlatformEventQueue &q,
                                  PlatformEvent e)
    {
        q.push(std::move(e)); // drop-on-full policy
    }
}

namespace wz::platform::win32
{
    static bool g_registered = false;

    struct NativeWindow
    {
        HWND hwnd = nullptr;
        bool should_close = false;
        static constexpr size_t QUEUE_SIZE = 1024;
        PlatformEventQueue event_queue =
            PlatformEventQueue(QUEUE_SIZE);
    };

    static NativeWindow *unwrap(wz::window::WindowHandle h)
    {
        return static_cast<NativeWindow *>(h.native);
    }

    wz::gpu::Device create_dx12_device(const wz::window::WindowHandle& window)
    {
        auto* data = static_cast<NativeWindow*>(window.native);
        assert(data && data->hwnd);

        return wz::gpu::dx12::create_device((void*)data->hwnd);
    }

    struct WindowDesc
    {
        const char *title = "Wozzits";
        int width = 1280;
        int height = 720;
        bool resizable = true;
        bool fullscreen = false;
    };

    static NativeWindow *GetWindowData(HWND hwnd)
    {
        return reinterpret_cast<NativeWindow *>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    void w32_destroy_window(wz::window::WindowHandle window)
    {
        auto *data = unwrap(window);
        if (!data)
            return;

        DestroyWindow(data->hwnd);
    }

    bool w32_poll_event(wz::window::WindowHandle window, PlatformEvent &out_event)
    {
        if (!window.valid())
            return false;
        auto *data = unwrap(window);
        if (!data)
            return false;

        return data->event_queue.try_pop(out_event);
    }

    bool w32_window_should_close(wz::window::WindowHandle window)
    {
        if (!window.valid())
            return true;

        auto *data = reinterpret_cast<NativeWindow *>(window.native);
        if (!data)
            return true;

        return data->should_close;
    }

    void w32_pump_messages()
    {
        MSG msg;

        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                // You already use window_should_close instead of global quit state,
                // so nothing mandatory here unless you want global shutdown.
            }

            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_NCCREATE:
        {
            auto *cs = (CREATESTRUCT *)lParam;
            auto *data = (NativeWindow *)cs->lpCreateParams;

            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);
            return TRUE;
        }

        case WM_ERASEBKGND:
            return 1; // tell Windows "we handled it"

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH brush = CreateSolidBrush(RGB(20, 20, 20));
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCDESTROY:
        {
            auto *data = GetWindowData(hwnd);

            if (data)
            {
                delete data;
                SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }

        case WM_CLOSE:
        {
            auto *data = GetWindowData(hwnd);
            if (data)
            {
                data->should_close = true;
                PlatformEvent e{};
                e.type = PlatformEvent::Type::Close;

                push_window_event(data->event_queue, e);
            }
            return 0;
        }

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }

        case WM_SIZE:
        {
            auto *data = GetWindowData(hwnd);
            if (!data)
                return DefWindowProc(hwnd, msg, wParam, lParam);

            const int w = LOWORD(lParam);
            const int h = HIWORD(lParam);

            // Per-window queue: consumed by app to drive GPU swapchain resize
            PlatformEvent pe{};
            pe.type = PlatformEvent::Type::Resize;
            pe.resize.width  = w;
            pe.resize.height = h;
            push_window_event(data->event_queue, pe);

            // Global event queue: consumed by build_input to populate InputState::window
            wz::event::Event we{};
            we.category = wz::event::Event::Category::Window;
            we.source   = wz::event::Event::Source::Platform;
            we.type     = wz::event::Event::Type::Resized;
            we.resize.width  = static_cast<int16_t>(w);
            we.resize.height = static_cast<int16_t>(h);
            wz::event::event_queue.try_push(we);

            return 0;
        }

        case WM_SETFOCUS:
        {
            wz::event::Event e{};
            e.category = wz::event::Event::Category::Window;
            e.source   = wz::event::Event::Source::Platform;
            e.type     = wz::event::Event::Type::FocusGained;
            wz::event::event_queue.try_push(e);
            return 0;
        }

        case WM_KILLFOCUS:
        {
            wz::event::Event e{};
            e.category = wz::event::Event::Category::Window;
            e.source   = wz::event::Event::Source::Platform;
            e.type     = wz::event::Event::Type::FocusLost;
            wz::event::event_queue.try_push(e);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        {
            auto *data = GetWindowData(hwnd);
            if (!data)
                return DefWindowProc(hwnd, msg, wParam, lParam);

            PlatformEvent e{};
            e.type = PlatformEvent::Type::Key;
            e.key.key = (int)wParam;

            e.key.just_pressed =
                (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);

            push_window_event(data->event_queue, e);
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            auto *data = GetWindowData(hwnd);
            if (!data)
                return DefWindowProc(hwnd, msg, wParam, lParam);

            PlatformEvent e{};
            e.type = PlatformEvent::Type::MouseMove;
            e.mouse_move.x = GET_X_LPARAM(lParam);
            e.mouse_move.y = GET_Y_LPARAM(lParam);

            push_window_event(data->event_queue, e);
            return 0;
        }
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    wz::window::WindowHandle w32_create_window(int width,
                                               int height,
                                               const char *title)
    {
        HINSTANCE hInstance = GetModuleHandleA(nullptr);

        WNDCLASSA wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = "WozzitsWindowClass";

        RegisterClassA(&wc);

        auto *data = new NativeWindow();

        HWND hwnd = CreateWindowExA(
            0,
            wc.lpszClassName,
            title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            width, height,
            nullptr,
            nullptr,
            hInstance,
            data);

        if (!hwnd)
        {
            delete data;
            return {};
        }

        data->hwnd = hwnd;

        BOOL dark = TRUE;
        DwmSetWindowAttribute(
            hwnd,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &dark,
            sizeof(dark));

        ShowWindow(hwnd, SW_SHOW);

        // IMPORTANT: store NativeWindow pointer in WindowHandle
        return wz::window::WindowHandle{
            static_cast<void *>(data)};
    }

}