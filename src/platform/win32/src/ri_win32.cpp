#include <windows.h>
#include <event/event.h>
#include <platform/win32/ri_win32.h>
#include <malloc.h>

namespace
{
    HWND g_input_hwnd = nullptr;
}

namespace
{
    LRESULT CALLBACK RIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_INPUT:
            wz::platform::win32::ri_process_input(lParam);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }
}

namespace wz::platform::win32
{
    bool ri_init(HINSTANCE hInstance)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = RIWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"WozzitsRawInput";

        if (!RegisterClassW(&wc))
            return false;

        g_input_hwnd = CreateWindowExW(
            0,
            wc.lpszClassName,
            L"",
            0,
            0, 0, 0, 0,
            HWND_MESSAGE,
            nullptr,
            hInstance,
            nullptr);

        if (!g_input_hwnd)
            return false;

        RAWINPUTDEVICE rid[2]{};

        // Mouse
        rid[0].usUsagePage = 0x01;
        rid[0].usUsage = 0x02;
        rid[0].dwFlags = RIDEV_INPUTSINK;
        rid[0].hwndTarget = g_input_hwnd;

        // Keyboard
        rid[1].usUsagePage = 0x01;
        rid[1].usUsage = 0x06;
        rid[1].dwFlags = RIDEV_INPUTSINK;
        rid[1].hwndTarget = g_input_hwnd;

        if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
            return false;

        return true;
    }
}

namespace wz::platform::win32
{
    void ri_shutdown()
    {
        if (g_input_hwnd)
        {
            DestroyWindow(g_input_hwnd);
            g_input_hwnd = nullptr;
        }
    }
}

namespace wz::platform::win32
{
    void ri_process_input(LPARAM lParam)
    {

        UINT size = 0;
        GetRawInputData((HRAWINPUT)lParam,
                        RID_INPUT,
                        nullptr,
                        &size,
                        sizeof(RAWINPUTHEADER));

        if (size == 0)
            return;

        BYTE *buffer = (BYTE *)alloca(size);

        if (GetRawInputData((HRAWINPUT)lParam,
                            RID_INPUT,
                            buffer,
                            &size,
                            sizeof(RAWINPUTHEADER)) != size)
            return;

        RAWINPUT *raw = (RAWINPUT *)buffer;

        wz::event::Event e{};
        e.source = wz::event::Event::Source::Platform;
        e.timestamp = wz::time::TimeSource::now_ticks();

        if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            const RAWMOUSE& m = raw->data.mouse;

            wz::event::Event e{};
            e.source = wz::event::Event::Source::Platform;
            e.category = wz::event::Event::Category::Input;
            e.timestamp = wz::time::TimeSource::now_ticks();

            bool pushed = false;

            // -------------------------
            // 1. Mouse movement (highest priority)
            // -------------------------
            if (m.lLastX != 0 || m.lLastY != 0)
            {
                e.type = wz::event::Event::Type::MouseMove;
                e.mouse_move.dx = m.lLastX;
                e.mouse_move.dy = m.lLastY;

                pushed = wz::event::event_queue.try_push(e);
            }

            // -------------------------
            // 2. Wheel (mutually exclusive with movement per packet in practice)
            // -------------------------
            else if (m.usButtonFlags & RI_MOUSE_WHEEL)
            {
                e.type = wz::event::Event::Type::MouseWheel;
                e.mouse_wheel.delta = (int16_t)m.usButtonData;

                pushed = wz::event::event_queue.try_push(e);
            }

            // -------------------------
            // 3. Buttons (may OR together in same packet, but still ONE event)
            // -------------------------
            else if (m.usButtonFlags & (
                RI_MOUSE_LEFT_BUTTON_DOWN |
                RI_MOUSE_LEFT_BUTTON_UP |
                RI_MOUSE_RIGHT_BUTTON_DOWN |
                RI_MOUSE_RIGHT_BUTTON_UP))
            {
                e.type = wz::event::Event::Type::MouseButton;

                if (m.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)
                {
                    e.mouse_button.button = 0;
                    e.mouse_button.pressed = true;
                }
                else if (m.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)
                {
                    e.mouse_button.button = 0;
                    e.mouse_button.pressed = false;
                }
                else if (m.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)
                {
                    e.mouse_button.button = 1;
                    e.mouse_button.pressed = true;
                }
                else if (m.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)
                {
                    e.mouse_button.button = 1;
                    e.mouse_button.pressed = false;
                }

                pushed = wz::event::event_queue.try_push(e);
            }
            // optional debug hook (later useful)
            // if (!pushed) { drop counter / telemetry 
            (void)pushed;
        }

        else if (raw->header.dwType == RIM_TYPEKEYBOARD)
        {
            const RAWKEYBOARD& k = raw->data.keyboard;

            e.category = wz::event::Event::Category::Input;

            // -----------------------------------
            // 1. Key state (down / up)
            // -----------------------------------
            bool is_break = (k.Flags & RI_KEY_BREAK);

            e.type = is_break
                ? wz::event::Event::Type::KeyPressUp
                : wz::event::Event::Type::KeyPressDown;

            // -----------------------------------
            // 2. Payload (this is the important part)
            // -----------------------------------
            e.key.vkey = k.VKey;
            e.key.scancode = k.MakeCode;
            e.key.flags = k.Flags;

            // -----------------------------------
            // 3. Push event
            // -----------------------------------
            bool pushed = wz::event::event_queue.try_push(e);
            // optional debug hook (later useful)
            // if (!pushed) { drop counter / telemetry }

        }

        // wz::event::event_queue.try_push(e);
    }
}
