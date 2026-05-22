// src/audio/audio_device.cpp

#include <audio/audio_device.h>

#include "rtaudio_device.h"

namespace wz::audio
{
    Device* create_device(const DeviceDesc& desc)
    {
        return rtaudio::create_device(desc);
    }

    void destroy_device(Device* device)
    {
        rtaudio::destroy_device(device);
    }

    bool start(Device* device)
    {
        return rtaudio::start(device);
    }

    void stop(Device* device)
    {
        rtaudio::stop(device);
    }

    bool is_running(const Device* device)
    {
        return rtaudio::is_running(device);
    }
}