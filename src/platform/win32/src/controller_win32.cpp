#include <platform/win32/controller_win32.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include <windows.h>
#include <Xinput.h>

namespace
{
    constexpr uint32_t kDisconnectedPollIntervalFrames = 60;

    bool g_slot_connected[wz::input::kMaxControllers]{};
    uint32_t g_sample_frame = 0;
    bool g_first_sample = true;

    float normalize_thumb_axis(SHORT value, SHORT deadzone) noexcept
    {
        const int magnitude = std::abs(static_cast<int>(value));
        if (magnitude <= deadzone) {
            return 0.0f;
        }

        const float sign = value < 0 ? -1.0f : 1.0f;
        const float normalized =
            static_cast<float>(magnitude - deadzone)
            / static_cast<float>(32767 - deadzone);
        return std::clamp(sign * normalized, -1.0f, 1.0f);
    }

    float normalize_trigger_axis(BYTE value) noexcept
    {
        if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
            return 0.0f;
        }

        const float normalized =
            static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
            / static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
        return std::clamp(normalized, 0.0f, 1.0f);
    }

    void set_button(
        wz::input::ControllerSample& controller,
        uint32_t button,
        WORD buttons,
        WORD mask) noexcept
    {
        if (button >= wz::input::kControllerButtonCount) {
            return;
        }
        controller.buttons[button] = (buttons & mask) != 0;
    }

    bool should_poll_slot(uint32_t slot) noexcept
    {
        if (slot >= wz::input::kMaxControllers) {
            return false;
        }
        if (g_first_sample || g_slot_connected[slot]) {
            return true;
        }

        return (g_sample_frame % kDisconnectedPollIntervalFrames)
            == (slot % kDisconnectedPollIntervalFrames);
    }
}

namespace wz::platform::win32
{
    bool controller_init()
    {
        std::fill(
            std::begin(g_slot_connected),
            std::end(g_slot_connected),
            false);
        g_sample_frame = 0;
        g_first_sample = true;
        return true;
    }

    void controller_shutdown()
    {
        std::fill(
            std::begin(g_slot_connected),
            std::end(g_slot_connected),
            false);
        g_sample_frame = 0;
        g_first_sample = true;
    }

    void controller_sample(wz::input::ControllerInputSample& out)
    {
        out = {};
        out.count = static_cast<uint8_t>(wz::input::kMaxControllers);

        for (DWORD i = 0; i < wz::input::kMaxControllers; ++i) {
            if (!should_poll_slot(i)) {
                continue;
            }

            XINPUT_STATE state{};
            if (XInputGetState(i, &state) != ERROR_SUCCESS) {
                g_slot_connected[i] = false;
                continue;
            }

            auto& controller = out.controllers[i];
            const XINPUT_GAMEPAD& gamepad = state.Gamepad;
            controller.connected = true;

            controller.axes[wz::input::kControllerAxisLeftX] = normalize_thumb_axis(
                gamepad.sThumbLX,
                XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            controller.axes[wz::input::kControllerAxisLeftY] = normalize_thumb_axis(
                gamepad.sThumbLY,
                XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            controller.axes[wz::input::kControllerAxisRightX] = normalize_thumb_axis(
                gamepad.sThumbRX,
                XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            controller.axes[wz::input::kControllerAxisRightY] = normalize_thumb_axis(
                gamepad.sThumbRY,
                XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            controller.axes[wz::input::kControllerAxisLeftTrigger] =
                normalize_trigger_axis(gamepad.bLeftTrigger);
            controller.axes[wz::input::kControllerAxisRightTrigger] =
                normalize_trigger_axis(gamepad.bRightTrigger);

            const WORD buttons = gamepad.wButtons;
            set_button(controller, wz::input::kControllerButtonDpadUp,
                buttons, XINPUT_GAMEPAD_DPAD_UP);
            set_button(controller, wz::input::kControllerButtonDpadDown,
                buttons, XINPUT_GAMEPAD_DPAD_DOWN);
            set_button(controller, wz::input::kControllerButtonDpadLeft,
                buttons, XINPUT_GAMEPAD_DPAD_LEFT);
            set_button(controller, wz::input::kControllerButtonDpadRight,
                buttons, XINPUT_GAMEPAD_DPAD_RIGHT);
            set_button(controller, wz::input::kControllerButtonStart,
                buttons, XINPUT_GAMEPAD_START);
            set_button(controller, wz::input::kControllerButtonBack,
                buttons, XINPUT_GAMEPAD_BACK);
            set_button(controller, wz::input::kControllerButtonLeftThumb,
                buttons, XINPUT_GAMEPAD_LEFT_THUMB);
            set_button(controller, wz::input::kControllerButtonRightThumb,
                buttons, XINPUT_GAMEPAD_RIGHT_THUMB);
            set_button(controller, wz::input::kControllerButtonLeftShoulder,
                buttons, XINPUT_GAMEPAD_LEFT_SHOULDER);
            set_button(controller, wz::input::kControllerButtonRightShoulder,
                buttons, XINPUT_GAMEPAD_RIGHT_SHOULDER);
            set_button(controller, wz::input::kControllerButtonA,
                buttons, XINPUT_GAMEPAD_A);
            set_button(controller, wz::input::kControllerButtonB,
                buttons, XINPUT_GAMEPAD_B);
            set_button(controller, wz::input::kControllerButtonX,
                buttons, XINPUT_GAMEPAD_X);
            set_button(controller, wz::input::kControllerButtonY,
                buttons, XINPUT_GAMEPAD_Y);

            g_slot_connected[i] = true;
        }

        g_first_sample = false;
        ++g_sample_frame;
    }
}
