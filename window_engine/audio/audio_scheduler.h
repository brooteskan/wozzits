#pragma once

// audio/audio_scheduler.h
//
// The realtime control split for the mixer (audio-track item 3a).
//
// The sim thread posts POD AudioCommands; the audio thread drains them inside
// process() and applies them sample-accurately, splitting each render block at
// command offsets so a sound starts/stops on the exact frame it was scheduled
// for. The handoff is a lock-free SPSC queue: post() is the single producer
// (sim), process() is the single consumer (audio callback). Neither allocates.
//
// ── Threading contract ────────────────────────────────────────────────────────
//   post()    — call from ONE producer thread only (the sim).
//   process() — call from ONE consumer thread only (the audio device callback).
//   Construct the scheduler off both threads (the queue allocates in its ctor).
//
// ── Scope of this slice ───────────────────────────────────────────────────────
// Sample-accurate Play / Stop / SetMasterGain scheduling. Per-sample parameter
// ramps and stop/steal de-click fades are the next 3a sub-slice (they need voice
// ramp state); SetMasterGain here is a sample-accurate STEP, not yet a ramp.
// Binding process() to the wz::audio device callback is a separate thin wrapper.

#include <audio/audio_buffer.h>
#include <audio/audio_command.h>
#include <audio/mixer.h>

#include <containers/spsc_queue.h>

#include <cstddef>
#include <cstdint>

namespace wz::audio {

    class AudioScheduler
    {
    public:
        AudioScheduler(uint32_t max_voices = 64,
                       size_t queue_capacity_pow2 = 1024);

        // Producer side (sim thread). Returns false if the queue is full; the
        // command is dropped and the caller may retry next tick.
        bool post(const AudioCommand& cmd) noexcept;

        // Consumer side (audio thread). Applies all commands due within this block
        // at their exact frame offsets, renders into out (frames * channels), and
        // advances the sample clock.
        void process(float* out,
                     uint32_t frames,
                     uint32_t channels,
                     uint32_t sample_rate) noexcept;

        // Current absolute frame on the scheduler clock (consumer side).
        uint64_t sample_clock() const noexcept { return clock_; }

        // Underlying mixer, for inspection/tests. Do not drive play/stop directly
        // when using the scheduler — post commands instead.
        Mixer& mixer() noexcept { return mixer_; }
        const Mixer& mixer() const noexcept { return mixer_; }

    private:
        void apply(const AudioCommand& cmd) noexcept;
        void render_span(float* out,
                         uint32_t from,
                         uint32_t to,
                         uint32_t channels,
                         uint32_t sample_rate) noexcept;

        Mixer mixer_;
        wz::core::containers::SPSCQueue<AudioCommand> queue_;

        uint64_t clock_ = 0;

        // One-command lookahead: the queue has no peek, so a command popped but
        // not yet due is held here until its block arrives. Because commands are
        // posted in non-decreasing time order, seeing one future command means
        // the rest are future too.
        bool has_pending_ = false;
        AudioCommand pending_{};
    };

} // namespace wz::audio
