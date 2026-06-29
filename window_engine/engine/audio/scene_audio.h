#pragma once

// engine/audio/scene_audio.h
//
// Runtime bridge from authored scene audio to the mixer (audio-track item 7).
// Resolves each scene AudioSource's audio-renderable terminal through the asset
// library (renderable -> clip -> interleaved PCM view) and posts Play commands
// to the realtime scheduler. This is the sim-side step that makes a scene audible
// — the "hello sound" path.

#include <audio/grain_cloud.h>  // GrainCloudDesc

#include <scene/scene_ecs.h>  // RuntimeEntityId

#include <cstdint>
#include <deque>

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
        uint32_t played = 0;             // Play / PlayGrainCloud commands posted
        uint32_t skipped_disabled = 0;   // AudioSource.enabled == false
        uint32_t skipped_unresolved = 0; // empty/unresolvable renderable or clip
    };

    // Stable owner of the GrainCloudDesc objects a grain-cloud AudioSource posts to
    // the audio thread. A PlayGrainCloud command carries a POINTER to a desc, so the
    // desc must outlive the command's consumption — this store (a std::deque, so
    // addresses never move) holds them for the scene's playback lifetime. The owner
    // (the app) clears it when reloading a scene, AFTER the audio runtime is stopped.
    class GrainCloudDescStore
    {
    public:
        wz::audio::GrainCloudDesc& allocate()
        {
            descs_.emplace_back();
            return descs_.back();
        }
        void clear() noexcept { descs_.clear(); }
        std::size_t size() const noexcept { return descs_.size(); }

    private:
        std::deque<wz::audio::GrainCloudDesc> descs_;
    };

    // Post a start command for every enabled, auto_play AudioSource in `instance`
    // whose audio-renderable resolves: a clip renderable posts a voice Play; a
    // grain-cloud renderable builds a GrainCloudDesc (in `grain_store`) and posts a
    // PlayGrainCloud. Sources that are disabled, not auto_play, or unresolved are
    // not played (the first two honored, the last reported). Each is tagged with a
    // per-source client id so it can be stopped/steered later.
    //
    // Producer-side (sim thread): only posts commands, never touches the audio
    // thread. Safe to call once when a scene starts.
    ScenePlaybackReport play_scene_audio_sources(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler,
        GrainCloudDescStore& grain_store);

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
