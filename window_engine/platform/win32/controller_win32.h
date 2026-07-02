#pragma once

#include <input/input.h>

#include <cstdint>

namespace wz::platform::win32
{
    enum class ControllerBackendKind : uint32_t
    {
        XInput = 0,
        WindowsGamingInput = 1,
    };

    struct ControllerSampleDiagnostics
    {
        ControllerBackendKind backend = ControllerBackendKind::XInput;
        bool used_fallback = false;
        bool timestamp_valid[wz::input::kMaxControllers]{};
        uint64_t timestamp_100ns[wz::input::kMaxControllers]{};
    };

    bool controller_init();
    void controller_shutdown();

    void controller_sample(wz::input::ControllerInputSample& out);
    const ControllerSampleDiagnostics& controller_last_sample_diagnostics();
}
