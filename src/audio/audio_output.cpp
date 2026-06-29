// src/audio/audio_output.cpp

#include <audio/audio_output.h>

namespace wz::audio {

    AudioOutput::AudioOutput(AudioScheduler& scheduler,
                             const AudioFormat& format) noexcept
        : scheduler_(scheduler)
        , format_(format)
    {
    }

    AudioOutput::~AudioOutput()
    {
        stop();
    }

    void AudioOutput::render_callback(AudioCallbackContext& ctx,
                                      void* user) noexcept
    {
        auto* self = static_cast<AudioOutput*>(user);
        if (self == nullptr)
            return;

        // The device owns the buffer layout (frames/channels); the sample rate is
        // the format the scheduler clock was configured against.
        self->scheduler_.process(
            ctx.output, ctx.frames, ctx.channels, self->format_.sample_rate);
    }

    bool AudioOutput::start()
    {
        if (device_ != nullptr)
            return true;

        DeviceDesc desc{};
        desc.format = format_;
        desc.callback = &AudioOutput::render_callback;
        desc.user = this;

        device_ = create_device(desc);
        if (device_ == nullptr)
            return false;

        if (!wz::audio::start(device_)) {
            destroy_device(device_);
            device_ = nullptr;
            return false;
        }
        return true;
    }

    void AudioOutput::stop()
    {
        if (device_ == nullptr)
            return;
        wz::audio::stop(device_);
        destroy_device(device_);
        device_ = nullptr;
    }

    bool AudioOutput::running() const noexcept
    {
        return device_ != nullptr && wz::audio::is_running(device_);
    }

} // namespace wz::audio
