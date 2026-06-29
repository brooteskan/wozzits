#pragma once

// audio/audio_output.h
//
// Binds an AudioScheduler to a real wz::audio output device: the device's
// free-running pull callback drives scheduler.process() on the device thread.
// This is the only place the audio thread is actually created; the scheduler and
// mixer below it stay device-agnostic and headless-testable.
//
// Lifetime: the scheduler (and the PCM its voices reference) must outlive the
// AudioOutput. Destroying the AudioOutput stops and releases the device.

#include <audio/audio_device.h>
#include <audio/audio_scheduler.h>

namespace wz::audio {

    class AudioOutput
    {
    public:
        // Does not open the device; call start(). format.sample_rate/channels are
        // the contract passed to scheduler.process() in the callback.
        AudioOutput(AudioScheduler& scheduler, const AudioFormat& format) noexcept;
        ~AudioOutput();

        AudioOutput(const AudioOutput&) = delete;
        AudioOutput& operator=(const AudioOutput&) = delete;

        // Open and start the device. Returns false if device creation/start fails.
        bool start();

        // Stop and release the device (idempotent).
        void stop();

        bool running() const noexcept;

        const AudioFormat& format() const noexcept { return format_; }

        // The device pull callback. Exposed (and static) so it can be installed as
        // an AudioCallback function pointer and exercised directly in tests with a
        // hand-built context — no real device required.
        static void render_callback(AudioCallbackContext& ctx, void* user) noexcept;

    private:
        AudioScheduler& scheduler_;
        AudioFormat     format_;
        Device*         device_ = nullptr;
    };

} // namespace wz::audio
