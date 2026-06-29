#pragma once

// engine/audio/scene_audio.h
//
// Runtime bridge from authored scene audio to the mixer (audio-track item 7).
// Resolves each scene AudioSource's audio-renderable terminal through the asset
// library (renderable -> clip -> interleaved PCM view) and posts Play commands
// to the realtime scheduler. This is the sim-side step that makes a scene audible
// — the "hello sound" path.

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

} // namespace wz::engine::audio
