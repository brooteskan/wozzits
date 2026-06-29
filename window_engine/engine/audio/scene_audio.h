#pragma once

// engine/audio/scene_audio.h
//
// Runtime bridge from authored scene audio to the mixer (audio-track item 7).
// Resolves each scene AudioSource's audio-renderable terminal through the asset
// library (renderable -> clip -> interleaved PCM view) and posts Play commands
// to the realtime scheduler. This is the sim-side step that makes a scene audible
// — the "hello sound" path.

#include <scene/scene_ecs.h>  // RuntimeEntityId

#include <cstdint>

namespace wz::engine::assets {
    class EngineAssetLibrary;
    struct SceneInstance;
}

namespace wz::audio {
    class AudioScheduler;
}

namespace wz::engine::audio {

    struct ScenePlaybackReport
    {
        uint32_t played = 0;             // Play commands posted
        uint32_t skipped_disabled = 0;   // AudioSource.enabled == false
        uint32_t skipped_unresolved = 0; // empty/unresolvable renderable or clip
    };

    // Post a Play for every enabled, auto_play AudioSource in `instance` whose
    // audio-renderable resolves to a valid clip. Sources that are disabled,
    // not auto_play, or unresolved are not played (the first two are honored, the
    // last is reported). Playback uses the renderable's baked gain/pitch/looping;
    // each voice is tagged with a per-source client id so it can be stopped later.
    //
    // Producer-side (sim thread): only posts commands, never touches the audio
    // thread. Safe to call once when a scene starts.
    ScenePlaybackReport play_scene_audio_sources(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler);

    // Behavior-triggered audio (audio-track item 9). The host translates a
    // PLAY/STOP/SET_SOUND_GAIN behavior command into one of these and applies it
    // to `entity`'s AudioSource: Play resolves the renderable→clip and posts a
    // Play tagged with the source's stable client_id; Stop/SetGain address that
    // tag. The behavior names the entity; this maps it to the AudioSource.
    enum class AudioBehaviorVerb { Play, Stop, SetGain };

    // Apply one behavior audio verb to `entity`'s AudioSource via the scheduler.
    // Returns true iff a command was posted; a no-op (false) when the entity has
    // no AudioSource, (Play) its renderable doesn't resolve to a clip, or the
    // queue is full. `enabled`/`auto_play` are NOT consulted — a behavior trigger
    // is an explicit play regardless of the auto-play policy.
    //   Stop:    v0 = fade-out frames (0 = hard cut); v1 unused.
    //   SetGain: v0 = target gain, v1 = ramp frames (0 = jump).
    //   Play:    clip selection for a bank-backed renderable, in v0/v1 —
    //              v0 >= 0  selects that clip index (rounded);
    //              v0 == -1 uses the renderable's default_index;
    //              v0 <= -2 selects by name, v1 carrying the 32-bit name hash as a
    //                       bit pattern (an unknown name falls back to default).
    //            Uses the renderable's baked gain/pitch/looping.
    bool apply_audio_behavior_command(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler,
        AudioBehaviorVerb verb,
        wz::scene::RuntimeEntityId entity,
        float v0,
        float v1);

} // namespace wz::engine::audio
