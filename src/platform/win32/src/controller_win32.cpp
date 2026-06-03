#include <platform/win32/controller_win32.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <string_view>

#include <windows.h>
#include <Xinput.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/base.h>

namespace
{
    enum class ControllerBackend
    {
        XInput,
        WindowsGamingInput,
    };

    constexpr uint32_t kDisconnectedPollIntervalFrames = 60;

    bool g_slot_connected[wz::input::kMaxControllers]{};
    uint32_t g_sample_frame = 0;
    bool g_first_sample = true;
    ControllerBackend g_backend = ControllerBackend::XInput;
    bool g_winrt_initialized = false;
    wz::platform::win32::ControllerSampleDiagnostics g_diagnostics{};

    bool env_requests_wgi()
    {
        char value[32]{};
        size_t required = 0;
        if (getenv_s(&required, value, sizeof(value), "WZ_CONTROLLER_BACKEND")
            != 0
            || required == 0)
        {
            return false;
        }

        const std::string_view backend{ value };
        return backend == "wgi"
            || backend == "WGI"
            || backend == "WindowsGamingInput";
    }

    bool init_wgi_backend() noexcept
    {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            g_winrt_initialized = true;
            return true;
        }
        catch (const winrt::hresult_changed_state&) {
            // The thread already has an apartment. WGI static access can still
            // work through the existing COM apartment.
            return true;
        }
        catch (...) {
            return false;
        }
    }

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

    float apply_normalized_deadzone(
        double value,
        SHORT deadzone) noexcept
    {
        const double threshold =
            static_cast<double>(deadzone) / 32767.0;
        const double magnitude = std::abs(value);
        if (magnitude <= threshold) {
            return 0.0f;
        }

        const double sign = value < 0.0 ? -1.0 : 1.0;
        const double normalized =
            (magnitude - threshold) / (1.0 - threshold);
        return std::clamp(
            static_cast<float>(sign * normalized),
            -1.0f,
            1.0f);
    }

    float apply_normalized_trigger_threshold(double value) noexcept
    {
        const double threshold =
            static_cast<double>(XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / 255.0;
        if (value <= threshold) {
            return 0.0f;
        }

        const double normalized = (value - threshold) / (1.0 - threshold);
        return std::clamp(static_cast<float>(normalized), 0.0f, 1.0f);
    }

    void reset_diagnostics(
        wz::platform::win32::ControllerBackendKind backend,
        bool used_fallback = false) noexcept
    {
        g_diagnostics = {};
        g_diagnostics.backend = backend;
        g_diagnostics.used_fallback = used_fallback;
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

    bool wgi_button_down(
        winrt::Windows::Gaming::Input::GamepadButtons buttons,
        winrt::Windows::Gaming::Input::GamepadButtons mask) noexcept
    {
        return (static_cast<uint32_t>(buttons)
            & static_cast<uint32_t>(mask)) != 0u;
    }

    void set_wgi_button(
        wz::input::ControllerSample& controller,
        uint32_t button,
        winrt::Windows::Gaming::Input::GamepadButtons buttons,
        winrt::Windows::Gaming::Input::GamepadButtons mask) noexcept
    {
        if (button >= wz::input::kControllerButtonCount) {
            return;
        }
        controller.buttons[button] = wgi_button_down(buttons, mask);
    }

    void sample_xinput(wz::input::ControllerInputSample& out)
    {
        reset_diagnostics(
            wz::platform::win32::ControllerBackendKind::XInput);
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

    bool sample_wgi(wz::input::ControllerInputSample& out) noexcept
    {
        namespace wgi = winrt::Windows::Gaming::Input;

        try {
            reset_diagnostics(
                wz::platform::win32::ControllerBackendKind::WindowsGamingInput);
            out = {};
            out.count = static_cast<uint8_t>(wz::input::kMaxControllers);

            const auto gamepads = wgi::Gamepad::Gamepads();
            const uint32_t count = std::min<uint32_t>(
                gamepads.Size(),
                wz::input::kMaxControllers);

            for (uint32_t i = 0; i < count; ++i) {
                const wgi::GamepadReading reading =
                    gamepads.GetAt(i).GetCurrentReading();
                auto& controller = out.controllers[i];
                controller.connected = true;
                g_diagnostics.timestamp_valid[i] = true;
                g_diagnostics.timestamp_100ns[i] = reading.Timestamp;

                controller.axes[wz::input::kControllerAxisLeftX] =
                    apply_normalized_deadzone(
                        reading.LeftThumbstickX,
                        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                controller.axes[wz::input::kControllerAxisLeftY] =
                    apply_normalized_deadzone(
                        reading.LeftThumbstickY,
                        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                controller.axes[wz::input::kControllerAxisRightX] =
                    apply_normalized_deadzone(
                        reading.RightThumbstickX,
                        XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
                controller.axes[wz::input::kControllerAxisRightY] =
                    apply_normalized_deadzone(
                        reading.RightThumbstickY,
                        XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
                controller.axes[wz::input::kControllerAxisLeftTrigger] =
                    apply_normalized_trigger_threshold(reading.LeftTrigger);
                controller.axes[wz::input::kControllerAxisRightTrigger] =
                    apply_normalized_trigger_threshold(reading.RightTrigger);

                const wgi::GamepadButtons buttons = reading.Buttons;
                set_wgi_button(controller, wz::input::kControllerButtonDpadUp,
                    buttons, wgi::GamepadButtons::DPadUp);
                set_wgi_button(controller, wz::input::kControllerButtonDpadDown,
                    buttons, wgi::GamepadButtons::DPadDown);
                set_wgi_button(controller, wz::input::kControllerButtonDpadLeft,
                    buttons, wgi::GamepadButtons::DPadLeft);
                set_wgi_button(controller, wz::input::kControllerButtonDpadRight,
                    buttons, wgi::GamepadButtons::DPadRight);
                set_wgi_button(controller, wz::input::kControllerButtonStart,
                    buttons, wgi::GamepadButtons::Menu);
                set_wgi_button(controller, wz::input::kControllerButtonBack,
                    buttons, wgi::GamepadButtons::View);
                set_wgi_button(controller, wz::input::kControllerButtonLeftThumb,
                    buttons, wgi::GamepadButtons::LeftThumbstick);
                set_wgi_button(controller, wz::input::kControllerButtonRightThumb,
                    buttons, wgi::GamepadButtons::RightThumbstick);
                set_wgi_button(controller, wz::input::kControllerButtonLeftShoulder,
                    buttons, wgi::GamepadButtons::LeftShoulder);
                set_wgi_button(controller, wz::input::kControllerButtonRightShoulder,
                    buttons, wgi::GamepadButtons::RightShoulder);
                set_wgi_button(controller, wz::input::kControllerButtonA,
                    buttons, wgi::GamepadButtons::A);
                set_wgi_button(controller, wz::input::kControllerButtonB,
                    buttons, wgi::GamepadButtons::B);
                set_wgi_button(controller, wz::input::kControllerButtonX,
                    buttons, wgi::GamepadButtons::X);
                set_wgi_button(controller, wz::input::kControllerButtonY,
                    buttons, wgi::GamepadButtons::Y);
            }

            return true;
        }
        catch (...) {
            return false;
        }
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
        if (env_requests_wgi() && init_wgi_backend()) {
            g_backend = ControllerBackend::WindowsGamingInput;
        }
        else {
            g_backend = ControllerBackend::XInput;
        }
        reset_diagnostics(
            g_backend == ControllerBackend::WindowsGamingInput
                ? ControllerBackendKind::WindowsGamingInput
                : ControllerBackendKind::XInput);
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
        if (g_winrt_initialized) {
            winrt::uninit_apartment();
            g_winrt_initialized = false;
        }
        reset_diagnostics(ControllerBackendKind::XInput);
    }

    void controller_sample(wz::input::ControllerInputSample& out)
    {
        if (g_backend == ControllerBackend::WindowsGamingInput
            && sample_wgi(out))
        {
            return;
        }

        sample_xinput(out);
        if (g_backend == ControllerBackend::WindowsGamingInput) {
            g_diagnostics.used_fallback = true;
        }
    }

    const ControllerSampleDiagnostics& controller_last_sample_diagnostics()
    {
        return g_diagnostics;
    }
}
