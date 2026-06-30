// src/audio/grain_cloud.cpp

#include <audio/grain_cloud.h>

#include <algorithm>
#include <cmath>

namespace wz::audio {

    namespace {
        constexpr double kPi = 3.14159265358979323846;

        // Interpolated mono read of a source at fractional frame `pos`. Multi-
        // channel sources are downmixed (averaged). Taps clamp to the buffer so a
        // grain that overruns fades to the last sample rather than reading OOB.
        float read_source_mono(const AudioBufferView& src, double pos) noexcept
        {
            const uint64_t fc = src.frame_count;
            if (fc == 0) {
                return 0.0f;
            }
            double base = std::floor(pos);
            const double t = pos - base;
            uint64_t i0 = (base < 0.0) ? 0u : static_cast<uint64_t>(base);
            uint64_t i1 = i0 + 1;
            if (i0 >= fc) i0 = fc - 1;
            if (i1 >= fc) i1 = fc - 1;

            const float inv_ch = 1.0f / static_cast<float>(src.channels);
            float m0 = 0.0f;
            float m1 = 0.0f;
            for (uint32_t c = 0; c < src.channels; ++c) {
                m0 += src.at(i0, c);
                m1 += src.at(i1, c);
            }
            m0 *= inv_ch;
            m1 *= inv_ch;
            return m0 + static_cast<float>(t) * (m1 - m0);
        }
    }

    // ─── Ramp ─────────────────────────────────────────────────────────────────────

    void GrainCloud::Ramp::set(float t, uint32_t ramp_frames) noexcept
    {
        target = t;
        if (ramp_frames == 0) {
            current = t;
            step = 0.0f;
            frames = 0;
        }
        else {
            step = (t - current) / static_cast<float>(ramp_frames);
            frames = ramp_frames;
        }
    }

    void GrainCloud::Ramp::advance() noexcept
    {
        if (frames > 0) {
            current += step;
            if (--frames == 0) {
                current = target;
                step = 0.0f;
            }
        }
    }

    // ─── Lifecycle / config ─────────────────────────────────────────────────────

    void GrainCloud::configure(const Config& config) noexcept
    {
        const uint32_t max_grains = (config.max_grains == 0) ? 1u : config.max_grains;
        grains_.assign(max_grains, Grain{});
        pool_limit_ = max_grains;

        rng_state_ = (config.seed != 0u) ? config.seed : 1u;
        spawn_phase_ = 0.0;
        blend_rate_ = Ramp{};
        blend_depth_ = Ramp{};
        blend_phase_ = 0.0;
        normalize_ = 0.0f;
        emitting_ = false;

        // Usable defaults: unity gain/pitch, no grains until density is set.
        gain_ = Ramp{};      gain_.current = gain_.target = 1.0f;
        pitch_ = Ramp{};     pitch_.current = pitch_.target = 1.0f;
        density_ = Ramp{};   // 0 grains/sec
        position_ = Ramp{};  // playhead at start
    }

    void GrainCloud::reset(const GrainCloudDesc& desc) noexcept
    {
        // No allocation: reuse the existing pool. Deactivate every grain, clamp the
        // usable count to the pool capacity, and re-arm from the descriptor.
        for (Grain& g : grains_) {
            g.active = false;
        }
        const uint32_t cap = static_cast<uint32_t>(grains_.size());
        pool_limit_ = (desc.max_grains < cap) ? desc.max_grains : cap;

        rng_state_ = (desc.seed != 0u) ? desc.seed : 1u;
        spawn_phase_ = 0.0;
        blend_phase_ = 0.0;
        emitting_ = false;

        gain_ = Ramp{};      gain_.current = gain_.target = 1.0f;
        pitch_ = Ramp{};     pitch_.current = pitch_.target = 1.0f;
        density_ = Ramp{};
        position_ = Ramp{};

        set_sources(desc.sources, desc.weights, desc.source_count);
        set_gain(desc.gain);
        set_density(desc.density);
        set_position(desc.position);
        set_pitch(desc.pitch);
        set_position_jitter(desc.position_jitter);
        set_pitch_jitter(desc.pitch_jitter_semitones);
        set_pan(desc.pan_center, desc.pan_spread);
        set_grain_size(desc.grain_ms);
        set_window(desc.window, desc.window_param);
        set_blend(desc.blend_rate, desc.blend_depth);
        set_normalize(desc.normalize);
    }

    void GrainCloud::set_sources(const AudioBufferView* views,
                                 const float* weights,
                                 uint32_t count) noexcept
    {
        source_count_ = std::min(count, kMaxSources);
        weight_total_ = 0.0f;
        for (uint32_t i = 0; i < source_count_; ++i) {
            sources_[i] = views ? views[i] : AudioBufferView{};
            const float w = weights ? weights[i] : 1.0f;
            weights_[i] = (w > 0.0f) ? w : 0.0f;
            weight_total_ += weights_[i];
        }
    }

    void GrainCloud::set_gain(float target, uint32_t ramp_frames) noexcept
    {
        gain_.set(target, ramp_frames);
    }

    void GrainCloud::set_density(float grains_per_second, uint32_t ramp_frames) noexcept
    {
        density_.set((grains_per_second > 0.0f) ? grains_per_second : 0.0f,
                     ramp_frames);
    }

    void GrainCloud::set_position(float normalized, uint32_t ramp_frames) noexcept
    {
        position_.set(std::clamp(normalized, 0.0f, 1.0f), ramp_frames);
    }

    void GrainCloud::set_pitch(float multiplier, uint32_t ramp_frames) noexcept
    {
        pitch_.set((multiplier > 0.0f) ? multiplier : 0.0f, ramp_frames);
    }

    void GrainCloud::set_position_jitter(float normalized) noexcept
    {
        position_jitter_ = std::clamp(normalized, 0.0f, 1.0f);
    }

    void GrainCloud::set_pitch_jitter(float semitones) noexcept
    {
        pitch_jitter_semitones_ = semitones;
    }

    void GrainCloud::set_pan(float center, float spread) noexcept
    {
        pan_center_ = std::clamp(center, -1.0f, 1.0f);
        pan_spread_ = std::clamp(spread, 0.0f, 1.0f);
    }

    void GrainCloud::set_grain_size(float milliseconds) noexcept
    {
        grain_ms_ = (milliseconds > 0.0f) ? milliseconds : 1.0f;
    }

    void GrainCloud::set_window(GrainWindow window, float param) noexcept
    {
        window_ = window;
        window_param_ = param;
    }

    void GrainCloud::set_blend(float rate_hz, float depth) noexcept
    {
        // Immediate (config-time) set of both.
        blend_rate_.set((rate_hz > 0.0f) ? rate_hz : 0.0f, 0u);
        blend_depth_.set(std::clamp(depth, 0.0f, 1.0f), 0u);
    }

    void GrainCloud::set_blend_rate(float rate_hz, uint32_t ramp_frames) noexcept
    {
        blend_rate_.set((rate_hz > 0.0f) ? rate_hz : 0.0f, ramp_frames);
    }

    void GrainCloud::set_blend_depth(float depth, uint32_t ramp_frames) noexcept
    {
        blend_depth_.set(std::clamp(depth, 0.0f, 1.0f), ramp_frames);
    }

    void GrainCloud::set_normalize(float reference_overlap) noexcept
    {
        normalize_ = (reference_overlap > 0.0f) ? reference_overlap : 0.0f;
    }

    void GrainCloud::start() noexcept { emitting_ = true; }
    void GrainCloud::stop() noexcept { emitting_ = false; }

    bool GrainCloud::active() const noexcept
    {
        if (emitting_) {
            return true;
        }
        for (const Grain& g : grains_) {
            if (g.active) {
                return true;
            }
        }
        return false;
    }

    uint32_t GrainCloud::active_grain_count() const noexcept
    {
        uint32_t n = 0;
        for (const Grain& g : grains_) {
            if (g.active) ++n;
        }
        return n;
    }

    // ─── PRNG (deterministic xorshift32) ────────────────────────────────────────

    uint32_t GrainCloud::rng_next() noexcept
    {
        uint32_t x = rng_state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng_state_ = x;
        return x;
    }

    float GrainCloud::rng_unit() noexcept
    {
        // Top 24 bits → [0, 1).
        return static_cast<float>(rng_next() >> 8) * (1.0f / 16777216.0f);
    }

    float GrainCloud::rng_bipolar() noexcept
    {
        return rng_unit() * 2.0f - 1.0f;
    }

    // ─── Spawning ───────────────────────────────────────────────────────────────

    float GrainCloud::effective_weight(uint32_t i) const noexcept
    {
        float w = weights_[i];
        const float depth = blend_depth_.current;
        if (depth > 0.0f && source_count_ > 1) {
            // Sine LFO on the selection weight; sources are evenly staggered in
            // phase so they crossfade (2 clips) or rotate (N). depth 1 => a source
            // fully drops out at its trough.
            const double ph = blend_phase_
                + static_cast<double>(i) / static_cast<double>(source_count_);
            const float lfo =
                0.5f + 0.5f * static_cast<float>(std::sin(2.0 * kPi * ph));
            w *= (1.0f - depth) + depth * lfo;
        }
        return w;
    }

    uint32_t GrainCloud::pick_source() noexcept
    {
        if (source_count_ <= 1) {
            return 0u;
        }
        float w[kMaxSources];
        float total = 0.0f;
        for (uint32_t i = 0; i < source_count_; ++i) {
            w[i] = effective_weight(i);
            total += w[i];
        }
        if (total <= 0.0f) {
            return 0u;
        }
        float r = rng_unit() * total;
        for (uint32_t i = 0; i < source_count_; ++i) {
            r -= w[i];
            if (r <= 0.0f) {
                return i;
            }
        }
        return source_count_ - 1;
    }

    void GrainCloud::spawn_grain(uint32_t out_rate) noexcept
    {
        if (source_count_ == 0) {
            return;
        }

        // Find a free grain slot within the usable pool; drop the spawn if full.
        Grain* slot = nullptr;
        for (uint32_t i = 0; i < pool_limit_; ++i) {
            if (!grains_[i].active) {
                slot = &grains_[i];
                break;
            }
        }
        if (slot == nullptr) {
            return;
        }

        const uint32_t src_idx = pick_source();
        const AudioBufferView& src = sources_[src_idx];
        if (!src.valid()) {
            return;
        }

        // Start position: the (smoothed) playhead plus normalized jitter.
        const float start_norm = std::clamp(
            position_.current + position_jitter_ * rng_bipolar(), 0.0f, 1.0f);
        const double start_frame =
            static_cast<double>(start_norm) *
            static_cast<double>(src.frame_count - 1);

        // Pitch: the (smoothed) multiplier with a semitone-spread jitter.
        const float semis = pitch_jitter_semitones_ * rng_bipolar();
        const float pitch = pitch_.current * std::pow(2.0f, semis / 12.0f);
        const double step =
            static_cast<double>(src.sample_rate) /
            static_cast<double>(out_rate) * static_cast<double>(pitch);

        const double size_d =
            static_cast<double>(grain_ms_) * static_cast<double>(out_rate) / 1000.0;
        const uint32_t size =
            (size_d < 1.0) ? 1u : static_cast<uint32_t>(size_d + 0.5);

        const float pan = std::clamp(
            pan_center_ + pan_spread_ * rng_bipolar(), -1.0f, 1.0f);

        slot->active = true;
        slot->source = src_idx;
        slot->position = start_frame;
        slot->step = step;
        slot->age = 0;
        slot->size = size;
        slot->pan = pan;
        slot->window = window_;
        slot->window_param = window_param_;
    }

    // ─── Render ─────────────────────────────────────────────────────────────────

    void GrainCloud::render_add(float* out,
                                uint32_t frames,
                                uint32_t out_channels,
                                uint32_t out_rate) noexcept
    {
        if (out == nullptr || out_rate == 0 || out_channels == 0) {
            return;
        }

        const uint32_t cap = pool_limit_;

        for (uint32_t f = 0; f < frames; ++f) {
            // Advance the per-frame smoothing of every cloud param.
            gain_.advance();
            density_.advance();
            position_.advance();
            pitch_.advance();
            blend_rate_.advance();
            blend_depth_.advance();

            // Advance the autonomous source-blend LFO phase (wraps in [0,1)).
            if (blend_rate_.current > 0.0f) {
                blend_phase_ +=
                    static_cast<double>(blend_rate_.current) /
                    static_cast<double>(out_rate);
                if (blend_phase_ >= 1.0) {
                    blend_phase_ -= std::floor(blend_phase_);
                }
            }

            // Spawn grains for this frame at the current density (grains/sec),
            // bounded so a runaway rate can't loop unbounded.
            if (emitting_ && density_.current > 0.0f && weight_total_ >= 0.0f) {
                spawn_phase_ +=
                    static_cast<double>(density_.current) /
                    static_cast<double>(out_rate);
                uint32_t spawned = 0;
                while (spawn_phase_ >= 1.0 && spawned < cap) {
                    spawn_grain(out_rate);
                    spawn_phase_ -= 1.0;
                    ++spawned;
                }
                if (spawn_phase_ >= 1.0) {
                    // Pool saturated this frame; drop the backlog, keep the phase.
                    spawn_phase_ -= std::floor(spawn_phase_);
                }
            }

            // Sum every sounding grain into the two pan legs. While we're here,
            // accumulate the sum of SQUARED window envelopes (win_energy) so the
            // optional power-normalize step below can read this frame's overlap.
            float left = 0.0f;
            float right = 0.0f;
            float win_energy = 0.0f;
            for (Grain& g : grains_) {
                if (!g.active) {
                    continue;
                }

                const float phase =
                    static_cast<float>(g.age) / static_cast<float>(g.size);
                const float env =
                    grain_window_gain(g.window, phase, g.window_param);
                win_energy += env * env;
                const float s = env * read_source_mono(sources_[g.source], g.position);

                // Constant-power pan: pan -1 → left, +1 → right.
                const double theta = (static_cast<double>(g.pan) + 1.0) * (kPi * 0.25);
                left += s * static_cast<float>(std::cos(theta));
                right += s * static_cast<float>(std::sin(theta));

                g.position += g.step;
                if (++g.age >= g.size) {
                    g.active = false;
                }
            }

            // Running power normalization (authored; 0 = off → exact legacy path).
            // Grains are random and overlap-counts fluctuate, so loudness pumps as
            // sources fade in/out. Random grains add in POWER, so a frame's RMS is
            // ∝ sqrt(Σ env²) = sqrt(win_energy). Scaling the grain sum by
            //
            //     norm = sqrt(N_ref) / sqrt(win_energy + N_ref)
            //
            // pins the output RMS to a reference overlap N_ref (normalize_):
            //   • win_energy → 0  (silence / a lone grain's quiet edges):
            //       norm → sqrt(N_ref)/sqrt(N_ref) = 1 → grains pass UNCHANGED, so
            //       each window stays smooth and low-density edges don't get
            //       boosted into clicks. The +N_ref term is that soft floor.
            //   • win_energy >> N_ref (dense overlap):
            //       norm ≈ sqrt(N_ref/win_energy) → RMS flattens toward a constant
            //       ~sqrt(N_ref)·(grain RMS) no matter how many grains overlap.
            // Applied to the grain sum only, NOT the master gain below.
            if (normalize_ > 0.0f) {
                const float norm = std::sqrt(normalize_) /
                                   std::sqrt(win_energy + normalize_);
                left *= norm;
                right *= norm;
            }

            const float gain = gain_.current;
            left *= gain;
            right *= gain;

            float* dst = out + static_cast<size_t>(f) * out_channels;
            if (out_channels == 1) {
                dst[0] += left + right;
            }
            else {
                dst[0] += left;
                dst[1] += right;
            }
        }
    }

} // namespace wz::audio
