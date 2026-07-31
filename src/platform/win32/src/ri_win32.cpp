#include <windows.h>
#include <event/event.h>
#include <platform/win32/ri_win32.h>
#include <malloc.h>

#include <atomic>
#include <cstdint>

namespace
{
    HWND g_input_hwnd = nullptr;

    // Raw-input transitions lost because the event queue was full. A dropped
    // button edge is not cosmetic -- input.cpp latches mouse.down and clears it
    // only on a received UP, so a lost UP sticks that button down until another
    // one arrives. The drop-telemetry hook here used to be commented out
    // (#313, B4-S4); it is cheap and this is the one place that knows.
    std::atomic<uint64_t> g_dropped_input_events{ 0 };
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

        // The class lives for the process lifetime (ri_shutdown destroys the
        // window, not the class). A viewport restart re-enters ri_init, so a
        // re-register fails with ERROR_CLASS_ALREADY_EXISTS - that's fine, reuse
        // it. Bailing here is why a restarted viewport received no raw input.
        if (!RegisterClassW(&wc)
            && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
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
            // Unregister the raw-input devices before tearing down the window so
            // a later ri_init (viewport restart) re-registers cleanly. For
            // RIDEV_REMOVE, hwndTarget must be NULL (left zero here). The window
            // class is intentionally kept registered and reused across restarts.
            RAWINPUTDEVICE rid[2]{};
            rid[0].usUsagePage = 0x01;
            rid[0].usUsage = 0x02;
            rid[0].dwFlags = RIDEV_REMOVE;
            rid[1].usUsagePage = 0x01;
            rid[1].usUsage = 0x06;
            rid[1].dwFlags = RIDEV_REMOVE;
            RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

            DestroyWindow(g_input_hwnd);
            g_input_hwnd = nullptr;
        }
    }
}

namespace wz::platform::win32
{
    uint64_t ri_dropped_input_events()
    {
        return g_dropped_input_events.load(std::memory_order_relaxed);
    }

    std::size_t ri_translate_mouse(
        const RAWMOUSE& m,
        wz::event::Event* out,
        std::size_t capacity)
    {
        if (!out || capacity == 0) {
            return 0;
        }

        std::size_t count = 0;
        const auto emit = [&](wz::event::Event::Type type) -> wz::event::Event*
        {
            if (count >= capacity) {
                return nullptr;
            }
            wz::event::Event& e = out[count++];
            e = wz::event::Event{};
            e.source = wz::event::Event::Source::Platform;
            e.category = wz::event::Event::Category::Input;
            e.timestamp = wz::time::TimeSource::now_ticks();
            e.type = type;
            return &e;
        };

        // Motion. Independent of everything else in the packet -- this is the
        // one that used to shadow the wheel and button branches.
        if (m.lLastX != 0 || m.lLastY != 0) {
            if (wz::event::Event* e = emit(wz::event::Event::Type::MouseMove)) {
                e->mouse_move.dx = m.lLastX;
                e->mouse_move.dy = m.lLastY;
            }
        }

        // Wheel.
        if (m.usButtonFlags & RI_MOUSE_WHEEL) {
            if (wz::event::Event* e = emit(wz::event::Event::Type::MouseWheel)) {
                e->mouse_wheel.delta = static_cast<int16_t>(m.usButtonData);
            }
        }

        // Buttons. EVERY edge set in this packet gets its own event: the flags
        // genuinely OR together (a click faster than one report interval sets
        // DOWN and UP at once, routine on a high-polling-rate mouse), and the
        // old chained else-if kept only whichever was tested first.
        struct ButtonEdge
        {
            USHORT flag;
            uint8_t button;
            bool pressed;
        };
        static constexpr ButtonEdge kEdges[] = {
            { RI_MOUSE_LEFT_BUTTON_DOWN, 0, true },
            { RI_MOUSE_LEFT_BUTTON_UP, 0, false },
            { RI_MOUSE_RIGHT_BUTTON_DOWN, 1, true },
            { RI_MOUSE_RIGHT_BUTTON_UP, 1, false },
        };
        for (const ButtonEdge& edge : kEdges) {
            if ((m.usButtonFlags & edge.flag) == 0) {
                continue;
            }
            if (wz::event::Event* e =
                    emit(wz::event::Event::Type::MouseButton))
            {
                e->mouse_button.button = edge.button;
                e->mouse_button.pressed = edge.pressed;
            }
        }

        return count;
    }

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
            // One packet can carry motion + wheel + several button edges. Emit
            // ALL of them (#313, B4-C6/B4-C15) -- the old if/else-if chain kept
            // at most one, and a dropped button UP latches that button down
            // forever in input.cpp.
            wz::event::Event events[kRawMouseMaxEvents];
            const std::size_t count =
                ri_translate_mouse(raw->data.mouse, events, kRawMouseMaxEvents);

            for (std::size_t i = 0; i < count; ++i) {
                if (!wz::event::event_queue.try_push(events[i])) {
                    // The queue is full, so this transition is LOST. For a
                    // button edge that means a stuck latch, so it is worth more
                    // than the commented-out counter that used to live here
                    // (#313, B4-S4).
                    ++g_dropped_input_events;
                }
            }
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
