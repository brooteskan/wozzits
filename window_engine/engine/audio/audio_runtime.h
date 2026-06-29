#pragma once

// engine/audio/audio_runtime.h
//
// App-side audio runtime: owns the realtime scheduler and (when started) the
// output device that drives it. Header-only lifecycle wrapper so an app can hold
// one member, start it when entering play, and stop it when leaving. The
// scheduler exists for the runtime's whole life; the device is created lazily by
// start() and released by stop()/destruction.
//
// Non-movable: the device callback captures &scheduler_, so the scheduler must
// keep a stable address.

#include <audio/audio_output.h>
#include <audio/audio_scheduler.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace wz::engine::audio {

    class AudioRuntime
    {
    public:
        explicit AudioRuntime(uint32_t max_voices = 64,
                              std::size_t queue_capacity_pow2 = 1024)
            : scheduler_(max_voices, queue_capacity_pow2)
        {
        }

        AudioRuntime(const AudioRuntime&) = delete;
        AudioRuntime& operator=(const AudioRuntime&) = delete;

        // Open the output device (idempotent). Returns false if no device is
        // available — callers should treat audio as optional, not fatal.
        bool start(const wz::audio::AudioFormat& format = {})
        {
            if (output_ && output_->running())
                return true;
            output_.emplace(scheduler_, format);
            if (!output_->start()) {
                output_.reset();
                return false;
            }
            return true;
        }

        void stop()
        {
            if (output_) {
                output_->stop();
                output_.reset();
            }
        }

        bool running() const
        {
            return output_.has_value() && output_->running();
        }

        // The control surface; post commands here from the sim thread.
        wz::audio::AudioScheduler& scheduler() noexcept { return scheduler_; }

    private:
        wz::audio::AudioScheduler scheduler_;
        std::optional<wz::audio::AudioOutput> output_;
    };

} // namespace wz::engine::audio
