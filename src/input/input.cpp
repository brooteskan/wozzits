#include <input/input.h>

#include <algorithm>

namespace wz::input
{
    namespace
    {
        constexpr float kControllerAxisChangeEpsilon = 0.0001f;

        bool axis_changed(float current, float previous) noexcept
        {
            const float delta = current - previous;
            return delta > kControllerAxisChangeEpsilon
                || delta < -kControllerAxisChangeEpsilon;
        }
    }

    void build_input(InputState &input,
                     const InputState& prev,
                     const wz::event::Event *events,
                     size_t count,
                     wz::time::Frame frame,
                     const ControllerInputSample& controller_sample)
    {

        // Phase 1: carry held state forward and clear per-frame edges.
        input = prev;

        memset(input.keyboard.pressed, 0, sizeof(input.keyboard.pressed));
        memset(input.keyboard.released, 0, sizeof(input.keyboard.released));

        memset(input.mouse.pressed, 0, sizeof(input.mouse.pressed));
        memset(input.mouse.released, 0, sizeof(input.mouse.released));

        input.mouse.dx = 0;
        input.mouse.dy = 0;

        // Phase 2: integrate sampled controller signal state.
        input.controllers = {};
        input.controllers.count =
            static_cast<uint8_t>(
                std::min<uint32_t>(
                    controller_sample.count,
                    kMaxControllers));
        for (uint32_t controller_index = 0;
             controller_index < input.controllers.count;
             ++controller_index)
        {
            const ControllerSample& sample =
                controller_sample.controllers[controller_index];
            const ControllerState& previous =
                prev.controllers.controllers[controller_index];
            ControllerState& controller =
                input.controllers.controllers[controller_index];

            controller.connected = sample.connected;
            controller.connected_pressed =
                sample.connected && !previous.connected;
            controller.connected_released =
                !sample.connected && previous.connected;

            for (uint32_t axis = 0; axis < kControllerAxisCount; ++axis) {
                controller.axes[axis] = sample.axes[axis];
                controller.axes_changed[axis] =
                    sample.connected
                    && axis_changed(sample.axes[axis], previous.axes[axis]);
            }

            for (uint32_t button = 0;
                 button < kControllerButtonCount;
                 ++button)
            {
                controller.buttons[button] = sample.buttons[button];
                controller.buttons_pressed[button] =
                    sample.buttons[button] && !previous.buttons[button];
                controller.buttons_released[button] =
                    !sample.buttons[button] && previous.buttons[button];
            }
        }

        // Phase 3: apply discrete input/window events.
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
