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

    // The maximum number of source buffers a cloud blends between.
    inline constexpr uint32_t kMaxGrainSources = 8;

    // The cloud parameters a behavior can drive PER FRAME. Every param is now a
    // live, ramped value so a "program" (a complete preset) can be crossfaded as a
    // whole — see set_source_weight and WzGrainProgram in the behavior API. Ordinal
    // values cross the audio command queue, so keep them stable. Window SHAPE is a
    // discrete enum and cannot interpolate, so it snaps (ramp ignored).
    enum class GrainParam : uint8_t
    {
        Gain = 0,
        Density = 1,
        Position = 2,
        Pitch = 3,
        BlendRate = 4,       // source-blend LFO cycles/sec
        BlendDepth = 5,      // source-blend amount 0..1
        SourceWeight = 6,    // per-clip spawn weight; carries a source index
        PositionJitter = 7,  // 0..1 spread around position
        PitchJitter = 8,     // +/- spread in semitones
        PanCenter = 9,       // -1..1
        PanSpread = 10,      // 0..1
        GrainMs = 11,        // grain duration (ms)
        WindowParam = 12,    // window shape parameter
        Window = 13,         // window SHAPE (GrainWindow ordinal) — snaps, no ramp
    };

    // A trivially-copyable, self-contained description of a grain cloud — the form
    // that crosses the sim->audio boundary (carried by pointer on a PlayGrainCloud
    // command, like a clip's AudioBufferView). `sources` borrow PCM that must
    // outlive playback; the desc itself need only outlive command consumption (the
    // mixer copies it into a generator slot when it starts the cloud).
    struct GrainCloudDesc
    {
        AudioBufferView sources[kMaxGrainSources]{};
        float           weights[kMaxGrainSources]{};
        uint32_t        source_count = 0;

        uint32_t max_grains = 32;
        uint32_t seed = 1;

        // Live params (drivable per frame via SetGrainParam) — initial values.
        float gain = 1.0f;
        float density = 0.0f;   // grains/sec
        float position = 0.0f;  // 0..1 playhead
        float pitch = 1.0f;     // multiplier

        // Authored / set-once params.
        float       position_jitter = 0.0f;
        float       pitch_jitter_semitones = 0.0f;
        float       pan_center = 0.0f;
        float       pan_spread = 0.0f;
        float       grain_ms = 100.0f;
        GrainWindow window = GrainWindow::Gaussian;
        float       window_param = 0.4f;

        // Autonomous source-blend LFO (no behavior / per-frame command needed): a
        // slow oscillator cycles the per-source selection weights so the cloud
        // crossfades between its sources on its own. Sources are evenly staggered
        // in phase, so two clips give an A<->B crossfade and N clips rotate
        // through them. blend_rate = cycles/sec (0 = off, static weights);
        // blend_depth = 0..1 (1 = a source fully drops out at its trough).
        float blend_rate = 0.0f;
        float blend_depth = 0.0f;

        // Running power normalization (0 = off, exact legacy output). When > 0,
        // each output frame's grain sum is scaled so loudness stays roughly
        // constant as overlap density and source fade-ins swing — random grains
        // add in POWER, so we pin RMS to a reference overlap count (this value,
        // typically 1..4) without touching any grain's window shape. See
        // render_add for the formula + soft-floor rationale.
        float normalize = 0.0f;
    };

    class GrainCloud
    {
    public:
        // The maximum number of source buffers a cloud blends between.
        static constexpr uint32_t kMaxSources = kMaxGrainSources;

        struct Config
        {
            uint32_t max_grains = 32;  // hard ceiling on simultaneous grains (CPU bound)
            uint32_t seed = 1;         // PRNG seed for jitter; forced non-zero
        };

        // Allocate the grain pool and seed the PRNG. Call once off the audio thread
        // before rendering. Re-configuring resets all grains.
        void configure(const Config& config) noexcept;

        // Re-arm an ALREADY-CONFIGURED cloud from a descriptor without allocating:
        // deactivates in-flight grains, re-seeds, and sets every parameter (sources
        // copied). The grain pool must already exist (a prior configure()); the
        // desc's max_grains is clamped to that capacity. Does NOT start emitting —
        // call start() after. This is the audio-thread-safe path the mixer uses to
        // spin up a generator slot from a PlayGrainCloud command (no realtime heap).
        void reset(const GrainCloudDesc& desc) noexcept;

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

        // ── Character params: sampled by each grain at spawn. Now ramped too, so a
        // program crossfade morphs them smoothly (new grains read the current
        // value). ramp_frames == 0 = instant (the config/reset path). ─────────────
        void set_position_jitter(float normalized, uint32_t ramp_frames = 0) noexcept;
        void set_pitch_jitter(float semitones, uint32_t ramp_frames = 0) noexcept;
        void set_pan_center(float center, uint32_t ramp_frames = 0) noexcept;   // -1..1
        void set_pan_spread(float spread, uint32_t ramp_frames = 0) noexcept;   // 0..1
        void set_pan(float center, float spread) noexcept;     // both, instant (reset)
        void set_grain_size(float milliseconds, uint32_t ramp_frames = 0) noexcept;
        void set_window_param(float param, uint32_t ramp_frames = 0) noexcept;
        void set_window_shape(GrainWindow window) noexcept;    // snaps (no ramp)
        void set_window(GrainWindow window, float param) noexcept;  // both, instant

        // Per-clip spawn weight (relative selection probability), ramped so program
        // switches crossfade which clips voice the texture. `index` beyond the
        // source count is ignored. A program whose weights all reach ~0 goes silent.
        void set_source_weight(uint32_t index, float weight,
                               uint32_t ramp_frames = 0) noexcept;

        // Autonomous source-blend LFO: cycles the per-source selection weights so
        // the cloud crossfades between its sources without any external driver.
        // rate_hz = cycles/sec (0 = off); depth = 0..1. See GrainCloudDesc. Sets
        // both as an immediate jump (used at configure/start).
        void set_blend(float rate_hz, float depth) noexcept;

        // Live (ramped) control of the blend LFO — drivable per frame by a behavior
        // (GrainParam::BlendRate / BlendDepth), e.g. to shift the texture when the
        // player enters a new region. ramp_frames de-zippers the change (0 = jump).
        void set_blend_rate(float rate_hz, uint32_t ramp_frames = 0) noexcept;
        void set_blend_depth(float depth, uint32_t ramp_frames = 0) noexcept;

        // Running power normalization (authored, set once at start; NOT a live
        // per-frame param). 0 = off (exact legacy output); > 0 is the reference
        // overlap count the cloud's loudness is pinned to. Clamped to >= 0. See
        // render_add for how it scales the per-frame grain sum.
        void set_normalize(float reference_overlap) noexcept;

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

        float    effective_weight(uint32_t i) const noexcept;  // base x blend LFO
        uint32_t pick_source() noexcept;   // weighted by effective source weights
        float    current_weight_total() const noexcept;  // sum of live base weights
        void spawn_grain(uint32_t out_rate) noexcept;

        AudioBufferView sources_[kMaxSources]{};
        Ramp            weights_[kMaxSources]{};  // per-clip weight, ramped
        uint32_t        source_count_ = 0;

        std::vector<Grain> grains_;
        uint32_t           pool_limit_ = 0;  // usable grains (<= grains_.size())

        Ramp gain_{};
        Ramp density_{};   // grains/sec
        Ramp position_{};  // 0..1
        Ramp pitch_{};     // multiplier

        Ramp        position_jitter_{};
        Ramp        pitch_jitter_semitones_{};
        Ramp        pan_center_{};
        Ramp        pan_spread_{};
        Ramp        grain_ms_{};
        GrainWindow window_ = GrainWindow::Gaussian;
        Ramp        window_param_{};

        Ramp     blend_rate_{};   // source-blend LFO cycles/sec (0 = off)
        Ramp     blend_depth_{};  // 0..1
        double   blend_phase_ = 0.0;   // [0,1) LFO phase

        float    normalize_ = 0.0f;  // running power-normalize reference (0 = off)

        double   spawn_phase_ = 0.0;  // fractional grain-spawn accumulator
        uint32_t rng_state_ = 1;
        bool     emitting_ = false;
    };

} // namespace wz::audio
