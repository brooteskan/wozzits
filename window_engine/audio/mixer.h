#pragma once

// audio/mixer.h
//
// The runtime audio mixer: a fixed-capacity pool of voices summed into an
// interleaved output buffer, with a master gain and a safety limiter.
//
// ── Scope of this slice ───────────────────────────────────────────────────────
//
// This is the DSP spine. render() is the audio-thread side and is the only part
// that touches sample data; it allocates nothing and takes no locks. play()/
// stop() are the control side. In THIS slice they mutate the pool directly, so
// callers must drive control and render from the same thread (which the offline
// render harness and tests do). The realtime split — a lock-free command queue
// + timestamped event scheduling so the sim thread can drive a free-running
// device callback safely — is audio-track item 3a and replaces only the control
// plumbing; the Voice math and render() stay put.
//
// Deferred from item 3 (follow-up slices): category/sub-mix buses, a soft-knee
// limiter (V1 hard-clips), and device ownership (binding render() to the
// wz::audio pull callback).

#include <audio/audio_buffer.h>
#include <audio/voice.h>

#include <cstdint>
#include <vector>

namespace wz::audio {

    // Stable reference to a playing voice. generation == 0 is the invalid handle;
    // a handle goes stale once its slot is stopped or reused by a later play().
    struct VoiceHandle
    {
        uint32_t index = 0;
        uint32_t generation = 0;

        bool valid() const noexcept { return generation != 0; }
    };

    class Mixer
    {
    public:
        explicit Mixer(uint32_t max_voices = 64);

        // Start a voice on `src`. Returns a handle, or an invalid handle if the
        // source is invalid. When the pool is full the oldest voice is stolen.
        // client_id is an optional caller tag (0 = untagged) used by stop_client()
        // to address a voice without a handle round-trip — needed when control is
        // posted across a thread boundary (item 3a).
        VoiceHandle play(const AudioBufferView& src,
                         float gain = 1.0f,
                         float pitch = 1.0f,
                         bool looping = false,
                         uint32_t client_id = 0) noexcept;

        // Stop a specific voice (no-op if the handle is stale).
        void stop(VoiceHandle handle) noexcept;

        // Stop every active voice tagged with client_id (no-op if client_id == 0
        // or none match).
        void stop_client(uint32_t client_id) noexcept;

        // Stop every voice.
        void stop_all() noexcept;

        void set_master_gain(float gain) noexcept { master_gain_ = gain; }
        float master_gain() const noexcept { return master_gain_; }

        uint32_t capacity() const noexcept
        {
            return static_cast<uint32_t>(slots_.size());
        }
        uint32_t active_voice_count() const noexcept;

        // Render `frames` of interleaved output at `channels` x `sample_rate`.
        // Clears `out`, sums every active voice, applies master gain, then the
        // safety limiter. out must hold frames * channels floats.
        void render(float* out,
                    uint32_t frames,
                    uint32_t channels,
                    uint32_t sample_rate) noexcept;

    private:
        struct Slot
        {
            Voice    voice{};
            uint32_t generation = 0;  // bumped on each (re)allocation; 0 = never used
            uint64_t start_seq = 0;   // monotonically increasing; oldest = smallest
            uint32_t client_id = 0;   // caller tag for stop_client (0 = untagged)
        };

        // Pick a slot for a new voice: a free one if available, otherwise steal
        // the oldest active voice. Returns the slot index.
        uint32_t acquire_slot() noexcept;

        std::vector<Slot> slots_;
        float master_gain_ = 1.0f;
        uint64_t seq_ = 0;
    };

} // namespace wz::audio
