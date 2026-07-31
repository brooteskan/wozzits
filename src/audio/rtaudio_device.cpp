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

            // RtAudio 6 REPORTS BY RETURN VALUE, NOT BY THROWING (RtAudio.h
            // pins 6.0.1 here, and openStream/startStream/stopStream are all
            // declared returning RtAudioErrorType). The catch blocks below are
            // inherited from the RtAudio 4/5 contract and can no longer fire,
            // so discarding this return meant a failed open still handed back a
            // live Device and the whole audio stack reported success against a
            // stream that was never opened. #313, B4-C4.
            const RtAudioErrorType opened = device->audio.openStream(
                &output_params,
                nullptr,
                RTAUDIO_FLOAT32,
                desc.format.sample_rate,
                &device->buffer_frames,
                &audio_callback,
                device.get(),
                &options);

            if (opened != RTAUDIO_NO_ERROR || !device->audio.isStreamOpen()) {
                return nullptr;
            }

            return device.release();
        }
        catch (const std::exception&) {
            // Kept for the allocation/parameter paths that can still throw;
            // RtAudio's own failures arrive as the return value above.
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
                // Same as the open: the return value IS the error report in
                // RtAudio 6, so discarding it made every start succeed (#313,
                // B4-C4). AudioOutput::start already tears the device down on
                // a false return, so reporting the truth is all that is needed
                // for a failed start to stop pretending.
                const RtAudioErrorType started = device->audio.startStream();
                if (started != RTAUDIO_NO_ERROR) {
                    device->running = false;
                    return false;
                }
            }

            device->running = device->audio.isStreamRunning();
            return device->running;
        }
        catch (const std::exception&) {
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
                // Return value checked for symmetry with start(); a failed stop
                // still leaves us wanting `running` false, and destroy_device
                // closes the stream regardless.
                (void)device->audio.stopStream();
            }
        }
        catch (const std::exception&) {
        }

        device->running = false;
    }

    bool is_running(const Device* device)
    {
        if (!device) {
            return false;
        }

        // Ask the STREAM, not our cached flag (#313, B4-C5). `running` only
        // ever changes when we call start/stop, so a stream that stopped on its
        // own -- the device was unplugged, the driver dropped it, the endpoint
        // was reconfigured -- left this reporting true forever. Callers use it
        // to decide whether audio is live (AudioOutput::running, and through
        // that the per-tick spatialization gate), so a lie here is silent
        // silence.
        //
        // const_cast because RtAudio::isStreamRunning is not const; it is a
        // plain read of the stream state and mutates nothing.
        auto& audio = const_cast<Device*>(device)->audio;
        try {
            return device->running && audio.isStreamRunning();
        }
        catch (const std::exception&) {
            return false;
        }
    }
}