// src/audio/audio_scheduler.cpp

#include <audio/audio_scheduler.h>

namespace wz::audio {

    AudioScheduler::AudioScheduler(uint32_t max_voices,
                                   size_t queue_capacity_pow2)
        : mixer_(max_voices)
        , queue_(queue_capacity_pow2)
    {
    }

    bool AudioScheduler::post(const AudioCommand& cmd) noexcept
    {
        return queue_.push(cmd);
    }

    void AudioScheduler::apply(const AudioCommand& cmd) noexcept
    {
        switch (cmd.type) {
        case AudioCommandType::Play:
            mixer_.play(cmd.source, cmd.gain, cmd.pitch, cmd.looping,
                        cmd.client_id);
            break;
        case AudioCommandType::Stop:
            if (cmd.ramp_frames == 0)
                mixer_.stop_client(cmd.client_id);
            else
                mixer_.fade_out_client(cmd.client_id, cmd.ramp_frames);
            break;
        case AudioCommandType::SetGain:
            mixer_.set_gain_client(cmd.client_id, cmd.value, cmd.ramp_frames);
            break;
        case AudioCommandType::SetMasterGain:
            mixer_.set_master_gain(cmd.value);
            break;
        case AudioCommandType::PlayGrainCloud:
            if (cmd.grain != nullptr)
                mixer_.play_grain_cloud(*cmd.grain, cmd.client_id);
            break;
        case AudioCommandType::SetGrainParam:
            mixer_.set_grain_param_client(
                cmd.client_id,
                static_cast<GrainParam>(cmd.grain_param),
                cmd.value,
                cmd.ramp_frames);
            break;
        }
    }

    void AudioScheduler::render_span(float* out,
                                     uint32_t from,
                                     uint32_t to,
                                     uint32_t channels,
                                     uint32_t sample_rate) noexcept
    {
        if (to <= from)
            return;
        mixer_.render(out + static_cast<size_t>(from) * channels,
                      to - from, channels, sample_rate);
    }

    void AudioScheduler::process(float* out,
                                 uint32_t frames,
                                 uint32_t channels,
                                 uint32_t sample_rate) noexcept
    {
        if (out == nullptr || channels == 0 || frames == 0)
            return;

        const uint64_t block_end = clock_ + frames;
        uint32_t cursor = 0;

        for (;;) {
            if (!has_pending_) {
                if (!queue_.try_pop(pending_))
                    break;
                has_pending_ = true;
            }

            // Future block: leave it pending (later commands are later still).
            if (pending_.sample_time >= block_end)
                break;

            // Resolve the in-block offset; a late command applies at the cursor
            // (we cannot rewind already-rendered frames).
            uint32_t offset = (pending_.sample_time <= clock_)
                ? 0u
                : static_cast<uint32_t>(pending_.sample_time - clock_);
            if (offset < cursor)
                offset = cursor;

            render_span(out, cursor, offset, channels, sample_rate);
            cursor = offset;

            apply(pending_);
            has_pending_ = false;
        }

        // Render the tail of the block after the last command.
        render_span(out, cursor, frames, channels, sample_rate);

        clock_ = block_end;
    }

} // namespace wz::audio
