#pragma once

// audio/audio_device.h

#include <cstdint>

namespace wz::audio
{
    struct AudioFormat
    {
        uint32_t sample_rate = 48000;
        uint32_t channels = 2;
        uint32_t frames_per_buffer = 256;
    };

    struct AudioCallbackContext
    {
        float* output = nullptr;      // interleaved float32 output
        uint32_t frames = 0;
        uint32_t channels = 0;
        double stream_time = 0.0;
    };

    using AudioCallback = void(*)(AudioCallbackContext& context, void* user);

    struct Device;

    struct DeviceDesc
    {
        AudioFormat format{};
        AudioCallback callback = nullptr;
        void* user = nullptr;
    };

    Device* create_device(const DeviceDesc& desc);
    void destroy_device(Device* device);

    bool start(Device* device);
    void stop(Device* device);

    bool is_running(const Device* device);
}