// src/audio/voice.cpp

#include <audio/voice.h>

#include <cmath>

namespace wz::audio {

    void Voice::start(const AudioBufferView& src,
                      float gain,
                      float pitch,
                      bool looping) noexcept
    {
        src_ = src;
        position_ = 0.0;
        gain_current_ = gain;   // start at target: no attack ramp, exact output
        gain_target_ = gain;
        gain_step_ = 0.0f;
        gain_ramp_frames_ = 0;
        fading_out_ = false;
        pitch_ = pitch;
        looping_ = looping;
        active_ = src.valid();

        // A re-armed voice starts non-spatial; the producer opts it into spatial
        // mode (set_spatial) after start if this is a positionable source. Clear
        // the spatial state so a slot reused for a 2D voice can't inherit a prior
        // voice's pan/ITD.
        spatial_ = false;
        gain_l_current_ = gain_l_target_ = 1.0f;
        gain_r_current_ = gain_r_target_ = 1.0f;
        gain_l_step_ = gain_r_step_ = 0.0f;
        pan_ramp_frames_ = 0;
        itd_current_ = itd_target_ = 0.0f;
        itd_step_ = 0.0f;
        itd_ramp_frames_ = 0;
        delay_.fill(0.0f);
        delay_write_ = 0;
    }

    void Voice::stop() noexcept
    {
        active_ = false;
        src_ = AudioBufferView{};
        position_ = 0.0;
        gain_step_ = 0.0f;
        gain_ramp_frames_ = 0;
        fading_out_ = false;
        spatial_ = false;
    }

    void Voice::set_spatial(bool spatial) noexcept
    {
        if (spatial == spatial_) {
            return;
        }
        spatial_ = spatial;
        if (spatial) {
            // Seed both legs from the current scalar gain and clear the delay
            // line so the first spatial frame starts from a known, click-free
            // state. set_spatial_params then ramps toward the real pan/ITD.
            gain_l_current_ = gain_l_target_ = gain_current_;
            gain_r_current_ = gain_r_target_ = gain_current_;
            gain_l_step_ = gain_r_step_ = 0.0f;
            pan_ramp_frames_ = 0;
            itd_current_ = itd_target_ = 0.0f;
            itd_step_ = 0.0f;
            itd_ramp_frames_ = 0;
            delay_.fill(0.0f);
            delay_write_ = 0;
        }
    }

    void Voice::set_spatial_params(float gain_l,
                                   float gain_r,
                                   float itd_frames,
                                   uint32_t ramp_frames) noexcept
    {
        if (!spatial_) {
            return;
        }
        gain_l_target_ = gain_l;
        gain_r_target_ = gain_r;
        itd_target_ = itd_frames;
        if (ramp_frames == 0) {
            gain_l_current_ = gain_l;
            gain_r_current_ = gain_r;
            gain_l_step_ = gain_r_step_ = 0.0f;
            pan_ramp_frames_ = 0;
            itd_current_ = itd_frames;
            itd_step_ = 0.0f;
            itd_ramp_frames_ = 0;
        }
        else {
            const float inv = 1.0f / static_cast<float>(ramp_frames);
            gain_l_step_ = (gain_l - gain_l_current_) * inv;
            gain_r_step_ = (gain_r - gain_r_current_) * inv;
            pan_ramp_frames_ = ramp_frames;
            itd_step_ = (itd_frames - itd_current_) * inv;
            itd_ramp_frames_ = ramp_frames;
        }
    }

    float Voice::delay_read(float frames_back) const noexcept
    {
        // Clamp the lookback into the line; 0 reads the most-recent sample.
        if (frames_back < 0.0f) {
            frames_back = 0.0f;
        }
        const float max_back = static_cast<float>(kDelayCapacity - 1);
        if (frames_back > max_back) {
            frames_back = max_back;
        }

        // The most-recent written sample sits at (delay_write_ - 1). Read d
        // frames before that, linearly interpolating between the two taps that
        // straddle the fractional position.
        const uint32_t whole = static_cast<uint32_t>(frames_back);
        const float frac = frames_back - static_cast<float>(whole);

        const uint32_t newest = (delay_write_ + kDelayCapacity - 1) % kDelayCapacity;
        const uint32_t i0 =
            (newest + kDelayCapacity - whole) % kDelayCapacity;
        const uint32_t i1 =
            (i0 + kDelayCapacity - 1) % kDelayCapacity;  // one frame older
        return delay_[i0] + frac * (delay_[i1] - delay_[i0]);
    }

    void Voice::set_gain(float target, uint32_t ramp_frames) noexcept
    {
        gain_target_ = target;
        if (ramp_frames == 0) {
            gain_current_ = target;
            gain_step_ = 0.0f;
            gain_ramp_frames_ = 0;
        }
        else {
            gain_step_ =
                (target - gain_current_) / static_cast<float>(ramp_frames);
            gain_ramp_frames_ = ramp_frames;
        }
    }

    void Voice::fade_out(uint32_t frames) noexcept
    {
        if (frames == 0) {
            stop();
            return;
        }
        set_gain(0.0f, frames);
        fading_out_ = true;
    }

    float Voice::lerp_at(uint64_t i0,
                         uint64_t i1,
                         double t,
                         uint32_t source_channel) const noexcept
    {
        const float s0 = src_.at(i0, source_channel);
        const float s1 = src_.at(i1, source_channel);
        return s0 + static_cast<float>(t) * (s1 - s0);
    }

    void Voice::render_add(float* out,
                           uint32_t frames,
                           uint32_t out_channels,
                           uint32_t out_rate) noexcept
    {
        if (!active_ || out == nullptr || out_rate == 0 || out_channels == 0)
            return;

        const double step =
            static_cast<double>(src_.sample_rate) /
            static_cast<double>(out_rate) * pitch_;

        const uint64_t fc = src_.frame_count;
        const uint32_t src_channels = src_.channels;

        // Channel mode is loop-invariant across this call; resolve it once.
        enum class Mode { Broadcast, Downmix, PerChannel };
        const Mode mode =
            (src_channels == 1) ? Mode::Broadcast :
            (out_channels == 1) ? Mode::Downmix : Mode::PerChannel;
        const float downmix_scale = 1.0f / static_cast<float>(src_channels);

        for (uint32_t f = 0; f < frames; ++f) {
            if (!looping_ && position_ >= static_cast<double>(fc)) {
                active_ = false;
                break;
            }

            // Resolve the interpolation taps once per frame, not once per channel.
            uint64_t i0 = static_cast<uint64_t>(position_);
            const double t = position_ - static_cast<double>(i0);
            uint64_t i1 = i0 + 1;
            if (looping_) {
                i0 %= fc;
                i1 %= fc;
            }
            else {
                // Clamp the upper tap so the final fractional sample fades to the
                // last value rather than reading out of bounds.
                if (i0 >= fc) i0 = fc - 1;
                if (i1 >= fc) i1 = fc - 1;
            }

            float* dst = out + static_cast<size_t>(f) * out_channels;
            const float g = gain_current_;

            if (spatial_) {
                // Sum every source channel to ONE mono sample (the existing
                // downmix averaging), then push it through the delay ring so the
                // far ear can read it late. The scalar gain still applies (master
                // level + fades ride it); the L/R gains add the pan on top.
                float mono = 0.0f;
                for (uint32_t c = 0; c < src_channels; ++c)
                    mono += lerp_at(i0, i1, t, c);
                mono *= downmix_scale;
                mono *= g;

                delay_[delay_write_] = mono;
                delay_write_ = (delay_write_ + 1) % kDelayCapacity;

                // itd_current_ > 0 => source on the right => LEFT (far) leg is
                // delayed; the RIGHT (near) leg reads the freshest sample. A
                // negative ITD swaps which leg is delayed.
                const float left_back =
                    itd_current_ > 0.0f ? itd_current_ : 0.0f;
                const float right_back =
                    itd_current_ < 0.0f ? -itd_current_ : 0.0f;
                const float left = delay_read(left_back);
                const float right = delay_read(right_back);

                if (out_channels == 1) {
                    // Mono out: sum both legs (the ITD then combs — expected; the
                    // producer reduces ITD when folding to mono).
                    dst[0] += gain_l_current_ * left + gain_r_current_ * right;
                }
                else {
                    dst[0] += gain_l_current_ * left;
                    dst[1] += gain_r_current_ * right;
                    // Surplus output channels (>2) get the centered mono leg so a
                    // spatial voice on a >2ch device is still audible there.
                    for (uint32_t k = 2; k < out_channels; ++k)
                        dst[k] += g * mono;
                }

                // Advance the pan (L/R gain) ramp.
                if (pan_ramp_frames_ > 0) {
                    gain_l_current_ += gain_l_step_;
                    gain_r_current_ += gain_r_step_;
                    if (--pan_ramp_frames_ == 0) {
                        gain_l_current_ = gain_l_target_;
                        gain_r_current_ = gain_r_target_;
                        gain_l_step_ = gain_r_step_ = 0.0f;
                    }
                }
                // Advance the ITD ramp (slewing it micro-pitches the far leg).
                if (itd_ramp_frames_ > 0) {
                    itd_current_ += itd_step_;
                    if (--itd_ramp_frames_ == 0) {
                        itd_current_ = itd_target_;
                        itd_step_ = 0.0f;
                    }
                }
            }
            else {
                switch (mode) {
                case Mode::Broadcast: {
                    // Mono source: broadcast to every output channel.
                    const float m = lerp_at(i0, i1, t, 0);
                    for (uint32_t k = 0; k < out_channels; ++k)
                        dst[k] += g * m;
                    break;
                }
                case Mode::Downmix: {
                    // Downmix all source channels to mono by averaging.
                    float sum = 0.0f;
                    for (uint32_t c = 0; c < src_channels; ++c)
                        sum += lerp_at(i0, i1, t, c);
                    dst[0] += g * sum * downmix_scale;
                    break;
                }
                case Mode::PerChannel: {
                    // Surplus output channels clamp to the last source channel.
                    for (uint32_t k = 0; k < out_channels; ++k) {
                        const uint32_t sc =
                            (k < src_channels) ? k : src_channels - 1;
                        dst[k] += g * lerp_at(i0, i1, t, sc);
                    }
                    break;
                }
                }
            }

            // Advance the per-sample gain ramp; snap to target on completion and
            // deactivate if this was a fade-out.
            if (gain_ramp_frames_ > 0) {
                gain_current_ += gain_step_;
                if (--gain_ramp_frames_ == 0) {
                    gain_current_ = gain_target_;
                    gain_step_ = 0.0f;
                    if (fading_out_) {
                        fading_out_ = false;
                        active_ = false;
                        break;
                    }
                }
            }

            position_ += step;

            if (looping_ && position_ >= static_cast<double>(fc))
                position_ = std::fmod(position_, static_cast<double>(fc));
        }
    }

} // namespace wz::audio
