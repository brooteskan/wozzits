#include "rtaudio_device.h"

#include <RtAudio.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <exception>
#include <memory>

namespace wz::audio
{
    struct Device
    {
        RtAudio audio;
        DeviceDesc desc{};
        bool running = false;
        unsigned int buffer_frames = 0;
    };
}

namespace wz::audio::rtaudio
{
    namespace
    {
        int audio_callback(
            void* output_buffer,
            void* input_buffer,
            unsigned int n_buffer_frames,
            double stream_time,
            RtAudioStreamStatus status,
            void* user_data)
        {
            (void)input_buffer;
            (void)status;

            auto* device = static_cast<Device*>(user_data);
            if (!device || !device->desc.callback || !output_buffer) {
                return 0;
            }

            auto* out = static_cast<float*>(output_buffer);

            AudioCallbackContext context{};
            context.output = out;
            context.frames = n_buffer_frames;
            context.channels = device->desc.format.channels;
            context.stream_time = stream_time;

            device->desc.callback(context, device->desc.user);

            return 0;
        }
    }

    Device* create_device(const DeviceDesc& desc)
    {
        if (!desc.callback) {
            return nullptr;
        }

        if (desc.format.channels == 0 ||
            desc.format.sample_rate == 0 ||
            desc.format.frames_per_buffer == 0) {
            return nullptr;
        }

        auto device = std::make_unique<Device>();
        device->desc = desc;
        device->buffer_frames = desc.format.frames_per_buffer;

        try {
            if (device->audio.getDeviceCount() < 1) {
                return nullptr;
            }

            RtAudio::StreamParameters output_params{};
            output_params.deviceId = device->audio.getDefaultOutputDevice();
            output_params.nChannels = desc.format.channels;
            output_params.firstChannel = 0;

            RtAudio::StreamOptions options{};
            options.flags = 0;
            options.numberOfBuffers = 0;
            options.priority = 0;

            device->audio.openStream(
                &output_params,
                nullptr,
                RTAUDIO_FLOAT32,
                desc.format.sample_rate,
                &device->buffer_frames,
                &audio_callback,
                device.get(),
                &options);

            return device.release();
        }
        catch (const RtAudioErrorType&) {
            return nullptr;
        }
        catch (const std::exception&) {
            return nullptr;
        }
    }

    void destroy_device(Device* device)
    {
        if (!device) {
            return;
        }

        wz::audio::rtaudio::stop(device);


        try {
            if (device->audio.isStreamOpen()) {
                device->audio.closeStream();
            }
        }
        catch (...) {
        }

        delete device;
    }

    bool start(Device* device)
    {
        if (!device) {
            return false;
        }

        try {
            if (!device->audio.isStreamRunning()) {
                device->audio.startStream();
            }

            device->running = true;
            return true;
        }
        catch (...) {
            device->running = false;
            return false;
        }
    }

    void stop(Device* device)
    {
        if (!device) {
            return;
        }

        try {
            if (device->audio.isStreamRunning()) {
                device->audio.stopStream();
            }
        }
        catch (...) {
        }

        device->running = false;
    }

    bool is_running(const Device* device)
    {
        return device && device->running;
    }
}