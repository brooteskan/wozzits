// src/audio/mixer.cpp

#include <audio/mixer.h>

#include <algorithm>
#include <cstring>

namespace wz::audio {

    Mixer::Mixer(uint32_t max_voices, uint32_t max_grain_clouds)
    {
        if (max_voices == 0)
            max_voices = 1;
        slots_.resize(max_voices);

        // Pre-allocate every cloud's grain pool here (off the audio thread) so that
        // starting a cloud later via play_grain_cloud() only re-arms it (no heap).
        grain_slots_.resize(max_grain_clouds);
        for (GrainSlot& gs : grain_slots_) {
            gs.cloud.configure(
                GrainCloud::Config{ .max_grains = kGrainsPerCloud, .seed = 1u });
        }
    }

    uint32_t Mixer::acquire_grain_slot() noexcept
    {
        for (uint32_t i = 0; i < grain_slots_.size(); ++i) {
            if (!grain_slots_[i].cloud.active())
                return i;
        }
        uint32_t oldest = 0;
        uint64_t oldest_seq = grain_slots_[0].start_seq;
        for (uint32_t i = 1; i < grain_slots_.size(); ++i) {
            if (grain_slots_[i].start_seq < oldest_seq) {
                oldest_seq = grain_slots_[i].start_seq;
                oldest = i;
            }
        }
        return oldest;
    }

    GrainCloudHandle Mixer::play_grain_cloud(const GrainCloudDesc& desc,
                                             uint32_t client_id) noexcept
    {
        if (grain_slots_.empty() || desc.source_count == 0)
            return GrainCloudHandle{};

        const uint32_t index = acquire_grain_slot();
        GrainSlot& gs = grain_slots_[index];

        gs.generation += 1;
        if (gs.generation == 0)
            gs.generation = 1;

        gs.start_seq = ++seq_;
        gs.client_id = client_id;
        gs.cloud.reset(desc);  // re-arm in place (no allocation)
        gs.cloud.start();

        return GrainCloudHandle{ .index = index, .generation = gs.generation };
    }

    void Mixer::set_grain_param_client(uint32_t client_id,
                                       GrainParam param,
                                       float value,
                                       uint32_t ramp_frames) noexcept
    {
        if (client_id == 0)
            return;
        for (GrainSlot& gs : grain_slots_) {
            if (!gs.cloud.active() || gs.client_id != client_id)
                continue;
            switch (param) {
            case GrainParam::Gain:     gs.cloud.set_gain(value, ramp_frames); break;
            case GrainParam::Density:  gs.cloud.set_density(value, ramp_frames); break;
            case GrainParam::Position: gs.cloud.set_position(value, ramp_frames); break;
            case GrainParam::Pitch:    gs.cloud.set_pitch(value, ramp_frames); break;
            case GrainParam::BlendRate:
                gs.cloud.set_blend_rate(value, ramp_frames); break;
            case GrainParam::BlendDepth:
                gs.cloud.set_blend_depth(value, ramp_frames); break;
            }
        }
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
        // A grain cloud stops SPAWNING; its in-flight grains window out so the
        // texture tails off click-free, then the slot frees itself.
        for (GrainSlot& gs : grain_slots_) {
            if (gs.cloud.active() && gs.client_id == client_id)
                gs.cloud.stop();
        }
    }

    void Mixer::fade_out_client(uint32_t client_id, uint32_t frames) noexcept
    {
        if (client_id == 0)
            return;
        for (Slot& slot : slots_) {
            if (slot.voice.active() && slot.client_id == client_id)
                slot.voice.fade_out(frames);
        }
        for (GrainSlot& gs : grain_slots_) {
            if (gs.cloud.active() && gs.client_id == client_id) {
                gs.cloud.set_gain(0.0f, frames);
                gs.cloud.stop();
            }
        }
    }

    void Mixer::set_gain_client(uint32_t client_id,
                                float gain,
                                uint32_t ramp_frames) noexcept
    {
        if (client_id == 0)
            return;
        for (Slot& slot : slots_) {
            if (slot.voice.active() && slot.client_id == client_id)
                slot.voice.set_gain(gain, ramp_frames);
        }
        for (GrainSlot& gs : grain_slots_) {
            if (gs.cloud.active() && gs.client_id == client_id)
                gs.cloud.set_gain(gain, ramp_frames);
        }
    }

    void Mixer::stop_all() noexcept
    {
        for (Slot& slot : slots_)
            slot.voice.stop();
        for (GrainSlot& gs : grain_slots_)
            gs.cloud.stop();
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

    uint32_t Mixer::active_grain_cloud_count() const noexcept
    {
        uint32_t count = 0;
        for (const GrainSlot& gs : grain_slots_) {
            if (gs.cloud.active())
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

        // Grain clouds get their own pool but sum into the same bus.
        for (GrainSlot& gs : grain_slots_) {
            if (gs.cloud.active())
                gs.cloud.render_add(out, frames, channels, sample_rate);
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
