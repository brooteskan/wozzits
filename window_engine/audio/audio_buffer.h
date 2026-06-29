#pragma once

// audio/audio_buffer.h
//
// Non-owning view of interleaved float32 PCM, the single input currency of the
// mixer voice. Both decoded clips (AudioClipData) and single-cycle wavetables
// (the future wavetable asset) present as an AudioBufferView, so one
// interpolating buffer-reader serves both at equal per-sample cost.
//
// This type deliberately has no dependency on the engine asset layer: it is pure
// DSP input. Callers adapt their owning storage (e.g. AudioClipData) into a view.

#include <cstdint>

namespace wz::audio {

    struct AudioBufferView
    {
        const float* samples = nullptr;  // interleaved, frame-major
        uint64_t frame_count = 0;        // samples per channel
        uint32_t sample_rate = 0;        // frames per second of this buffer
        uint32_t channels = 0;           // interleaved channel count

        bool valid() const noexcept
        {
            return samples != nullptr
                && frame_count > 0
                && sample_rate > 0
                && channels > 0;
        }

        // Raw (non-interpolated) interleaved sample read. No bounds checking.
        float at(uint64_t frame, uint32_t ch) const noexcept
        {
            return samples[frame * channels + ch];
        }
    };

} // namespace wz::audio
