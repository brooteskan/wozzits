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

#include <audio/audio_buffer.h>

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
    };

} // namespace wz::audio
