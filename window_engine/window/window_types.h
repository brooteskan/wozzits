#pragma once

namespace wz::window
{

    struct WindowHandle
    {
        void *native = nullptr; // HWND on Windows
        bool valid() const { return native != nullptr; }
    };

    struct WindowDesc
    {
        const char *title = "Wozzits";
        int width = 1280;
        int height = 720;
        bool resizable = true;
        bool fullscreen = false;
    };

    enum class WindowEventType
    {
        Close,
        Resize,
        FocusGained,
        FocusLost,
        Key,
        Mouse
    };

    enum class KeyState
    {
        Down,
        Up
    };

    struct WindowEvent
    {
        WindowEventType type{};

        union
        {
            struct
            {
                int width;
                int height;
            } resize;

            struct
            {
                int key;
                KeyState state;
            } key;

            struct
            {
                int x;
                int y;
                int dx;
                int dy;
                int button;
                KeyState state;
            } mouse;
        };
    };

    struct KeyEvent
    {
        int key;
        KeyState state;
    };

    struct MouseEvent
    {
        int x;
        int y;
        int dx;
        int dy;
        int button;
        KeyState state;
    };

}
