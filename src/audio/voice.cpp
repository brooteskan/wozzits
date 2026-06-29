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
        gain_ = gain;
        pitch_ = pitch;
        looping_ = looping;
        active_ = src.valid();
    }

    void Voice::stop() noexcept
    {
        active_ = false;
        src_ = AudioBufferView{};
        position_ = 0.0;
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

            switch (mode) {
            case Mode::Broadcast: {
                // Mono source: broadcast to every output channel.
                const float m = lerp_at(i0, i1, t, 0);
                for (uint32_t k = 0; k < out_channels; ++k)
                    dst[k] += gain_ * m;
                break;
            }
            case Mode::Downmix: {
                // Downmix all source channels to mono by averaging.
                float sum = 0.0f;
                for (uint32_t c = 0; c < src_channels; ++c)
                    sum += lerp_at(i0, i1, t, c);
                dst[0] += gain_ * sum * downmix_scale;
                break;
            }
            case Mode::PerChannel: {
                // Surplus output channels clamp to the last source channel.
                for (uint32_t k = 0; k < out_channels; ++k) {
                    const uint32_t sc = (k < src_channels) ? k : src_channels - 1;
                    dst[k] += gain_ * lerp_at(i0, i1, t, sc);
                }
                break;
            }
            }

            position_ += step;

            if (looping_ && position_ >= static_cast<double>(fc))
                position_ = std::fmod(position_, static_cast<double>(fc));
        }
    }

} // namespace wz::audio
