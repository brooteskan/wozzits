#pragma once

// audio/grain_cloud.h
//
// A granular generator: a continuous texture built by spawning many short,
// windowed "grains" read from one or more source buffers. It is the audio analog
// of a particle system — its purpose right now is blending long-running
// environmental clips (room tone, wind, rain) into an evolving bed, but the same
// engine drives pitched textures (e.g. engine revs) by modulating its params.
//
// ── Threading model (mirrors Voice/Mixer) ─────────────────────────────────────
//
// render_add() is the audio-thread side: it allocates nothing and takes no locks
// (the grain pool is sized once in configure()). The set_*() control methods are
// the producer-side intent. Crucially, all PER-FRAME smoothing happens HERE on the
// audio thread: cloud params (gain/density/position/pitch) are {current,target,
// step} triples that ramp toward their targets per output frame, so control-rate
// updates (a behavior posting a new target each sim frame) come out audio-rate
// smooth — never zippered — exactly like Voice's gain de-zipper.
//
// ── Grain lifecycle ───────────────────────────────────────────────────────────
//
// A grain captures its parameters AT SPAWN (source, start position, pitch, pan,
// size, window) and runs to completion with them — so changing a cloud param only
// affects grains spawned afterwards, which keeps each grain glitch-free. Spawning
// is driven by `density` (grains/sec) via a fractional phase accumulator, so the
// rate is sample-accurate and smoothly modulable. When the pool is full a spawn is
// dropped (graceful CPU bound) rather than stealing a sounding grain.
//
// Grains read their source as mono (multi-channel sources are downmixed) and are
// panned into the output — the standard "grain = panned point source" model. Per-
// channel source reading can be added later without changing this interface.

#include <audio/audio_buffer.h>
#include <audio/grain_window.h>

#include <cstdint>
#include <vector>

namespace wz::audio {

    class GrainCloud
    {
    public:
        // The maximum number of source buffers a cloud blends between.
        static constexpr uint32_t kMaxSources = 8;

        struct Config
        {
            uint32_t max_grains = 32;  // hard ceiling on simultaneous grains (CPU bound)
            uint32_t seed = 1;         // PRNG seed for jitter; forced non-zero
        };

        // Allocate the grain pool and seed the PRNG. Call once off the audio thread
        // before rendering. Re-configuring resets all grains.
        void configure(const Config& config) noexcept;

        // Set the source buffers the cloud draws grains from, with per-source
        // weights (relative spawn probability; non-positive total => first source).
        // Views are non-owning and must outlive playback. `count` is clamped to
        // kMaxSources. Pass count == 0 to clear (the cloud then emits silence).
        void set_sources(const AudioBufferView* views,
                         const float* weights,
                         uint32_t count) noexcept;

        // ── Smoothed cloud params (ramp toward target over ramp_frames; 0 = jump).
        // Sampled by each grain at spawn (except gain, applied at the mix). ───────
        void set_gain(float target, uint32_t ramp_frames = 0) noexcept;
        void set_density(float grains_per_second, uint32_t ramp_frames = 0) noexcept;
        void set_position(float normalized, uint32_t ramp_frames = 0) noexcept;  // 0..1 playhead
        void set_pitch(float multiplier, uint32_t ramp_frames = 0) noexcept;

        // ── Direct params: apply to grains spawned after the call. ───────────────
        void set_position_jitter(float normalized) noexcept;   // 0..1 spread around position
        void set_pitch_jitter(float semitones) noexcept;       // +/- spread in semitones
        void set_pan(float center, float spread) noexcept;     // center/spread in [-1, 1]
        void set_grain_size(float milliseconds) noexcept;      // grain duration
        void set_window(GrainWindow window, float param) noexcept;

        // Begin / end emitting. stop() stops SPAWNING; in-flight grains finish so
        // the texture tails out click-free (active() stays true until they do).
        void start() noexcept;
        void stop() noexcept;
        [[nodiscard]] bool active() const noexcept;

        // Number of grains currently sounding (for tests / diagnostics).
        [[nodiscard]] uint32_t active_grain_count() const noexcept;

        // Accumulate this cloud's contribution into the interleaved output buffer
        // out[frames * out_channels], resampling source rates to out_rate. Mono out
        // sums both pan legs; stereo out applies constant-power panning; >2 channels
        // fill the first two and leave the rest untouched.
        void render_add(float* out,
                       uint32_t frames,
                       uint32_t out_channels,
                       uint32_t out_rate) noexcept;

    private:
        // One smoothed control value (mirrors Voice's gain ramp).
        struct Ramp
        {
            float current = 0.0f;
            float target = 0.0f;
            float step = 0.0f;
            uint32_t frames = 0;

            void set(float t, uint32_t ramp_frames) noexcept;
            void advance() noexcept;  // one output frame
        };

        struct Grain
        {
            bool        active = false;
            uint32_t    source = 0;
            double      position = 0.0;  // read head, in source frames
            double      step = 1.0;      // source frames per output frame
            uint32_t    age = 0;         // output frames elapsed
            uint32_t    size = 1;        // total output frames
            float       pan = 0.0f;      // [-1, 1]
            GrainWindow window = GrainWindow::Gaussian;  // captured at spawn
            float       window_param = 0.4f;
        };

        // xorshift32 PRNG state + helpers (deterministic given the seed).
        uint32_t rng_next() noexcept;
        float    rng_unit() noexcept;      // [0, 1)
        float    rng_bipolar() noexcept;   // [-1, 1)

        uint32_t pick_source() noexcept;   // weighted by source weights
        void spawn_grain(uint32_t out_rate) noexcept;

        AudioBufferView sources_[kMaxSources]{};
        float           weights_[kMaxSources]{};
        uint32_t        source_count_ = 0;
        float           weight_total_ = 0.0f;

        std::vector<Grain> grains_;

        Ramp gain_{};
        Ramp density_{};   // grains/sec
        Ramp position_{};  // 0..1
        Ramp pitch_{};     // multiplier

        float       position_jitter_ = 0.0f;
        float       pitch_jitter_semitones_ = 0.0f;
        float       pan_center_ = 0.0f;
        float       pan_spread_ = 0.0f;
        float       grain_ms_ = 100.0f;
        GrainWindow window_ = GrainWindow::Gaussian;
        float       window_param_ = 0.4f;

        double   spawn_phase_ = 0.0;  // fractional grain-spawn accumulator
        uint32_t rng_state_ = 1;
        bool     emitting_ = false;
    };

} // namespace wz::audio
