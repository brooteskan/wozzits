#pragma once

// audio/audio_command.h
//
// The POD command that crosses the sim -> audio thread boundary (audio-track
// item 3a). Commands ride a lock-free SPSC queue, so the element type MUST be
// trivially copyable: no std::vector or other owning member, or push/try_pop
// would heap-allocate on the audio thread. Carry scalars, fixed data, and
// non-owning views/handles only.

#include <audio/audio_buffer.h>

#include <cstdint>
#include <type_traits>

namespace wz::audio {

    struct GrainCloudDesc;  // audio/grain_cloud.h — carried by pointer (stable mem)

    enum class AudioCommandType : uint8_t
    {
        Play,            // start `source` (gain/pitch/looping/client_id)
        Stop,            // stop the voice(s)/cloud(s) tagged client_id (ramp = fade)
        SetGain,         // ramp tagged voice(s)/cloud(s) gain -> value over ramp_frames
        SetMasterGain,   // set master gain to `value`
        PlayGrainCloud,  // start the grain cloud `grain` points at, tagged client_id
        SetGrainParam,   // ramp tagged cloud(s) param `grain_param` -> value
        SetSpatial,      // put tagged voice(s) in spatial mode + steer pan/ITD/Doppler
    };

    struct AudioCommand
    {
        AudioCommandType type = AudioCommandType::Play;

        // Absolute frame on the scheduler's sample clock at which to apply this
        // command. Commands MUST be posted in non-decreasing sample_time order
        // (the SPSC queue preserves order; the scheduler relies on it).
        uint64_t sample_time = 0;

        // Play payload. source borrows external PCM that must outlive playback.
        AudioBufferView source{};
        float gain = 1.0f;
        float pitch = 1.0f;
        bool looping = false;

        // Play: optional tag stored on the voice (0 = untagged).
        // Stop / SetGain: which tag to target (0 = no-op).
        uint32_t client_id = 0;

        // Ramp length in output frames for Stop (fade-out; 0 = hard stop) and
        // SetGain / SetGrainParam (0 = jump). Ignored by other types.
        uint32_t ramp_frames = 0;

        // SetGain / SetMasterGain / SetGrainParam target.
        float value = 1.0f;

        // PlayGrainCloud payload: a stable descriptor (its source PCM must outlive
        // playback). nullptr for non-grain commands.
        const GrainCloudDesc* grain = nullptr;

        // SetGrainParam: which GrainParam to ramp (ordinal). Ignored otherwise.
        uint8_t grain_param = 0;

        // SetGrainParam + GrainParam::SourceWeight: which source (clip) the weight
        // targets. Ignored by every other grain param and command type.
        uint8_t grain_source_index = 0;

        // SetSpatial payload: per-channel equal-power pan gains and the far-leg
        // ITD offset in output frames (sign convention in voice.h). Doppler reuses
        // `pitch`; the ramp length reuses `ramp_frames`; the target reuses
        // `client_id`. Ignored by other types. A SetSpatial puts every tagged
        // voice into spatial mode (the producer's per-tick steer).
        float gain_l = 1.0f;
        float gain_r = 1.0f;
        float itd_frames = 0.0f;
    };

    static_assert(std::is_trivially_copyable_v<AudioCommand>,
                  "AudioCommand crosses a lock-free queue and must be POD");

} // namespace wz::audio
