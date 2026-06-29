// src/audio/mixer.cpp

#include <audio/mixer.h>

#include <algorithm>
#include <cstring>

namespace wz::audio {

    Mixer::Mixer(uint32_t max_voices)
    {
        if (max_voices == 0)
            max_voices = 1;
        slots_.resize(max_voices);
    }

    uint32_t Mixer::acquire_slot() noexcept
    {
        // Prefer a free slot.
        for (uint32_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].voice.active())
                return i;
        }

        // Pool full: steal the oldest active voice (smallest start_seq).
        uint32_t oldest = 0;
        uint64_t oldest_seq = slots_[0].start_seq;
        for (uint32_t i = 1; i < slots_.size(); ++i) {
            if (slots_[i].start_seq < oldest_seq) {
                oldest_seq = slots_[i].start_seq;
                oldest = i;
            }
        }
        return oldest;
    }

    VoiceHandle Mixer::play(const AudioBufferView& src,
                            float gain,
                            float pitch,
                            bool looping,
                            uint32_t client_id) noexcept
    {
        if (!src.valid())
            return VoiceHandle{};

        const uint32_t index = acquire_slot();
        Slot& slot = slots_[index];

        // Bump generation so any prior handle to this slot goes stale. Skip 0,
        // which is reserved for the invalid handle.
        slot.generation += 1;
        if (slot.generation == 0)
            slot.generation = 1;

        slot.start_seq = ++seq_;
        slot.client_id = client_id;
        slot.voice.start(src, gain, pitch, looping);

        return VoiceHandle{ .index = index, .generation = slot.generation };
    }

    void Mixer::stop(VoiceHandle handle) noexcept
    {
        if (!handle.valid() || handle.index >= slots_.size())
            return;

        Slot& slot = slots_[handle.index];
        if (slot.generation == handle.generation)
            slot.voice.stop();
    }

    void Mixer::stop_client(uint32_t client_id) noexcept
    {
        if (client_id == 0)
            return;
        for (Slot& slot : slots_) {
            if (slot.voice.active() && slot.client_id == client_id)
                slot.voice.stop();
        }
    }

    void Mixer::stop_all() noexcept
    {
        for (Slot& slot : slots_)
            slot.voice.stop();
    }

    uint32_t Mixer::active_voice_count() const noexcept
    {
        uint32_t count = 0;
        for (const Slot& slot : slots_) {
            if (slot.voice.active())
                ++count;
        }
        return count;
    }

    void Mixer::render(float* out,
                       uint32_t frames,
                       uint32_t channels,
                       uint32_t sample_rate) noexcept
    {
        if (out == nullptr || channels == 0)
            return;

        const size_t total = static_cast<size_t>(frames) * channels;
        std::memset(out, 0, total * sizeof(float));

        for (Slot& slot : slots_) {
            if (slot.voice.active())
                slot.voice.render_add(out, frames, channels, sample_rate);
        }

        // Master gain + V1 safety limiter (hard clip). A soft-knee limiter is a
        // follow-up; hard clipping guarantees the device never receives values
        // outside [-1, 1].
        for (size_t i = 0; i < total; ++i) {
            float v = out[i] * master_gain_;
            v = std::clamp(v, -1.0f, 1.0f);
            out[i] = v;
        }
    }

} // namespace wz::audio
