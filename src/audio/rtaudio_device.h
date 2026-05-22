#pragma once
// src/audio/rtaudio_device.h 

#include <audio/audio_device.h>

namespace wz::audio::rtaudio
{
    Device* create_device(const DeviceDesc& desc);
    void destroy_device(Device* device);

    bool start(Device* device);
    void stop(Device* device);

    bool is_running(const Device* device);
}