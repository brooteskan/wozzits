#pragma once

struct PlatformEvent
{
    enum class Type
    {
        // Window
        Close,
        Resize,

        // Input
        Key,
        MouseMove,
        MouseButton,
        MouseWheel
    };

    Type type;

    union
    {
        struct
        {
            int key;
            bool is_down;
            bool just_pressed;
            bool just_released;
        } key;

        struct
        {
            int width;
            int height;
        } resize;

        struct
        {
            int x;
            int y;
            int dx;
            int dy;
        } mouse_move;

        struct
        {
            int button;   // 0=left, 1=right, etc.
            bool pressed;
        } mouse_button;

        struct
        {
            int delta; // wheel delta
        } mouse_wheel;
    };
};

