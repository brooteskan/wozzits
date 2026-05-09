#include <input/input.h>

namespace wz::input
{
    void build_input(InputState &input,
                     const InputState& prev,
                     const wz::event::Event *events,
                     size_t count,
                     wz::time::Frame frame)
    {

        //  phase 1 Reset input state
        input = prev;

        memset(input.keyboard.pressed, 0, sizeof(input.keyboard.pressed));
        memset(input.keyboard.released, 0, sizeof(input.keyboard.released));

        memset(input.mouse.pressed, 0, sizeof(input.mouse.pressed));
        memset(input.mouse.released, 0, sizeof(input.mouse.released));

        input.mouse.dx = 0;
        input.mouse.dy = 0;

        // phase 2 Iterate events
        for (size_t i = 0; i < count; ++i)
        {
            const auto &e = events[i];

            if (e.category != wz::event::Event::Category::Input &&
                e.category != wz::event::Event::Category::Window)
                continue;

            switch (e.type)
            {
            case wz::event::Event::Type::MouseMove:
                input.mouse.dx += e.mouse_move.dx;
                input.mouse.dy += e.mouse_move.dy;
                break;

            case wz::event::Event::Type::MouseButton:
                input.mouse.down[e.mouse_button.button] = e.mouse_button.pressed;

                if (e.mouse_button.pressed)
                    input.mouse.pressed[e.mouse_button.button] = true;
                else
                    input.mouse.released[e.mouse_button.button] = true;
                break;

            case wz::event::Event::Type::KeyPressDown:
                input.keyboard.down[e.key.vkey] = true;
                input.keyboard.pressed[e.key.vkey] = true;
                break;

            case wz::event::Event::Type::KeyPressUp:
                input.keyboard.down[e.key.vkey] = false;
                input.keyboard.released[e.key.vkey] = true;
                break;

            case wz::event::Event::Type::MouseWheel:
                input.mouse.dx += e.mouse_wheel.delta; // temporary placeholder
                break;

            case wz::event::Event::Type::Resized:
                input.window.width = e.resize.width;
                input.window.height = e.resize.height;
                break;

            case wz::event::Event::Type::FocusGained:
                input.window.focused = true;
                break;

            case wz::event::Event::Type::FocusLost:
                input.window.focused = false;
                break;

            default:
                break;
            }

            // leave dx/dy as-is for simulation, or explicitly define policy
            // input.mouse.dx = 0; // optional depending on design
        }
    }
}
