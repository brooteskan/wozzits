#pragma once

// audio/voice.h
//
// The mixer's single voice primitive: a generic interpolating buffer-reader.
//
// A voice reads an AudioBufferView at a fractional rate, linearly interpolates,
// applies gain, maps channels, and ACCUMULATES into an output buffer. Clip
// playback and (later) wavetable oscillation are the same operation: the only
// difference is the source buffer and how the read rate is derived. The read
// rate per output frame is:
//
//     step = source.sample_rate / output_sample_rate * pitch
//
// so a clip's pitch and a wavetable's frequency both live in `pitch`, giving
// equal CPU cost by construction (the audio track's explicit requirement).
//
// A voice does not own its source buffer — the owner (mixer / asset table) must
// keep the backing samples alive for the voice's lifetime.
//
// ── Spatial mode (audio-track spatialization, seam 1) ─────────────────────────
//
// A voice can be put into an opt-in SPATIAL mode (set_spatial(true)) that turns
// it into a positionable point source for stereo out. The producer (the sim-side
// spatialization pass) feeds it pre-computed cues each tick; the voice does no
// geometry. Relative to the default path a spatial voice adds three things, all
// de-zippered with the SAME ramp machinery as the scalar gain so motion is
// click-free:
//   1. Sum-to-mono: every source channel is averaged to ONE mono sample per
//      output frame, so a stereo clip becomes a single positionable point.
//   2. Per-channel L/R gain (equal-power pan), each independently ramped.
//   3. A fractional inter-aural time difference (ITD): the post-resample mono
//      signal feeds a tiny ring buffer; the NEAR ear reads at offset 0 and the
//      FAR ear reads at a (ramped, linear-interpolated) fractional delay. Sign
//      convention: itd_frames > 0 means the source is on the RIGHT, so the LEFT
//      (far) channel is delayed; itd_frames < 0 delays the RIGHT channel.
// Doppler rides the existing pitch field (the producer posts a pitch multiplier;
// set_pitch is reused). For mono out a spatial voice sums the two legs (the
// ITD then combs — expected; the producer dials ITD toward 0 when appropriate).
//
// A voice that is NEVER put in spatial mode renders byte-for-byte as before:
// the spatial branch is fully bypassed and none of its state is touched.

#include <audio/audio_buffer.h>

#include <array>
#include <cstdint>

namespace wz::audio {

    class Voice
    {
    public:
        // Begin playing `src` at the given linear gain and pitch multiplier.
        // looping wraps at the buffer end; otherwise the voice deactivates when it
        // runs past the last frame. A voice with an invalid source stays inactive.
        void start(const AudioBufferView& src,
                   float gain,
                   float pitch,
                   bool looping) noexcept;

        // Silence and free this voice immediately (hard cut — may click; prefer
        // fade_out for audible stops).
        void stop() noexcept;

        // Ramp gain toward `target` over `ramp_frames` output frames (0 = jump).
        // De-zippers volume changes on a live voice.
        void set_gain(float target, uint32_t ramp_frames = 0) noexcept;

        // Ramp gain to zero over `frames`, then deactivate. The de-click path for
        // stop and steal (frames == 0 falls back to an immediate stop).
        void fade_out(uint32_t frames) noexcept;

        bool active() const noexcept { return active_; }

        void set_pitch(float pitch) noexcept { pitch_ = pitch; }

        // Turn this voice into a positionable point source (or back). Entering
        // spatial mode initializes the L/R gains from the current scalar gain and
        // zeroes the ITD/delay line so the first spatial block doesn't click;
        // leaving it restores the default (non-spatial) render path. Idempotent.
        void set_spatial(bool spatial) noexcept;
        bool spatial() const noexcept { return spatial_; }

        // Steer a spatial voice: ramp the per-channel L/R gains and the far-leg
        // ITD offset (in output frames; see the sign convention above) toward the
        // targets over `ramp_frames` output frames (0 = jump). De-zippers the pan
        // and the ITD so a moving source is click-free (ramping the ITD as the
        // source pans laterally also yields the micro-pitch cue for free). No-op
        // on a non-spatial voice. Doppler is posted separately via set_pitch.
        void set_spatial_params(float gain_l,
                                float gain_r,
                                float itd_frames,
                                uint32_t ramp_frames = 0) noexcept;

        // Accumulate this voice's contribution into the interleaved output buffer
        // out[frames * out_channels], resampling from the source rate to out_rate.
        // Does nothing if the voice is inactive. May deactivate the voice when a
        // non-looping source is exhausted.
        void render_add(float* out,
                        uint32_t frames,
                        uint32_t out_channels,
                        uint32_t out_rate) noexcept;

    private:
        // Linearly-interpolated read of one SOURCE channel given the two integer
        // taps and fraction already resolved for the current output frame. The
        // tap/fraction split is hoisted out of the per-channel loop by the caller
        // so a stereo+ source resolves it once per frame, not once per channel.
        float lerp_at(uint64_t i0,
                      uint64_t i1,
                      double t,
                      uint32_t source_channel) const noexcept;

        AudioBufferView src_{};
        double position_ = 0.0;   // current read position, in source frames
        double pitch_ = 1.0;

        // Gain is applied per-sample from gain_current_, which ramps toward
        // gain_target_ by gain_step_ for gain_ramp_frames_ frames (then snaps to
        // target). When fading_out_ and the ramp completes the voice deactivates.
        float gain_current_ = 1.0f;
        float gain_target_ = 1.0f;
        float gain_step_ = 0.0f;
        uint32_t gain_ramp_frames_ = 0;
        bool fading_out_ = false;

        bool looping_ = false;
        bool active_ = false;

        // ── Spatial mode state ────────────────────────────────────────────────
        // All zero/default until set_spatial(true); the non-spatial path never
        // reads or writes any of it.
        bool spatial_ = false;

        // Per-channel L/R gains, each ramped exactly like the scalar gain_*.
        float gain_l_current_ = 1.0f;
        float gain_l_target_ = 1.0f;
        float gain_l_step_ = 0.0f;
        float gain_r_current_ = 1.0f;
        float gain_r_target_ = 1.0f;
        float gain_r_step_ = 0.0f;
        uint32_t pan_ramp_frames_ = 0;

        // Fractional far-leg delay (in output frames), itself ramped so a moving
        // source's ITD slews smoothly (which also produces the micro-pitch cue).
        float itd_current_ = 0.0f;
        float itd_target_ = 0.0f;
        float itd_step_ = 0.0f;
        uint32_t itd_ramp_frames_ = 0;

        // Mono delay ring holding the post-resample signal. The near ear reads
        // tap 0; the far ear reads a fractional delay. kDelayCapacity comfortably
        // exceeds the max human ITD (~0.66 ms ≈ 32 frames @ 48 kHz). write_pos_
        // is the index the NEXT sample is written to; the most-recent sample is
        // at (write_pos_ - 1).
        static constexpr uint32_t kDelayCapacity = 64;
        std::array<float, kDelayCapacity> delay_{};
        uint32_t delay_write_ = 0;

        // Read the mono delay line `frames_back` output frames in the past
        // (fractional, linear-interpolated). frames_back == 0 returns the sample
        // just written (the near-ear tap). Clamps to the capacity.
        float delay_read(float frames_back) const noexcept;
    };

} // namespace wz::audio
