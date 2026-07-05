#pragma once

// engine/audio/scene_audio.h
//
// Runtime bridge from authored scene audio to the mixer (audio-track item 7).
// Resolves each scene AudioSource's audio-renderable terminal through the asset
// library (renderable -> clip -> interleaved PCM view) and posts Play commands
// to the realtime scheduler. This is the sim-side step that makes a scene audible
// — the "hello sound" path.

#include <audio/grain_cloud.h>  // GrainCloudDesc

#include <math/math_types.h>  // Mat4, Vec3
#include <scene/scene_ecs.h>  // RuntimeEntityId

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>

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
    //
    // `played_clients` (optional, in/out): when non-null, a source whose client_id
    // is already in the set is skipped (not re-counted), and every source this call
    // DOES start has its client_id inserted. This makes the pass idempotent across
    // calls that share one set — run it at scene load, then again after each prefab
    // spawn, and the ambient bed starts once while a freshly spawned node's
    // auto_play source (e.g. a spawned tank's engine grain cloud) starts on the
    // spawn pass. Pass nullptr for the classic play-everything behavior.
    ScenePlaybackReport play_scene_audio_sources(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler,
        GrainCloudDescStore& grain_store,
        std::unordered_set<uint32_t>* played_clients = nullptr);

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

    // Ramp a live grain-cloud parameter on `entity`'s AudioSource (audio-track
    // grain synth). `param_id` is a wz::audio::GrainParam / WZ_GRAIN_PARAM_*
    // ordinal; value is the target, ramp_frames the de-zipper length (0 = jump).
    // Resolves entity → AudioSource → stable client_id and posts a SetGrainParam.
    // Returns true iff a command was posted; a no-op (false) when the entity has
    // no AudioSource or the queue is full. Harmless if the source isn't a grain
    // cloud — the mixer only finds matching grain slots, so it's ignored there.
    // Producer-side; the param smooths on the audio thread, so per-frame is fine.
    bool apply_grain_param_command(
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler,
        wz::scene::RuntimeEntityId entity,
        uint8_t param_id,
        float value,
        uint32_t ramp_frames,
        uint8_t source_index = 0);

    // ─── Spatialization (audio-track seams 1–3) ────────────────────────────────

    // Persistent per-scene spatialization state, owned by the app and passed by
    // ref into update_scene_audio_spatialization each tick. It carries the
    // previous-tick world positions (keyed by AudioSource client_id, plus the
    // listener) needed to derive velocity for Doppler. Cleared on scene load (the
    // app does this next to grain_desc_store_.clear()) so a new scene starts with
    // no stale velocity. No prev entry on the first tick => Doppler is skipped
    // that tick (pitch = 1).
    struct AudioSpatializationState
    {
        std::unordered_map<uint32_t, wz::math::Vec3> prev_source_pos;
        wz::math::Vec3 prev_listener_pos{};
        bool has_prev_listener = false;

        void clear() noexcept
        {
            prev_source_pos.clear();
            prev_listener_pos = wz::math::Vec3{};
            has_prev_listener = false;
        }
    };

    // Per-tick producer-side spatialization pass (sim thread). Resolves the active
    // listener and, for every enabled Clip-kind AudioSource, computes a pan
    // (equal-power) + ITD from the source's azimuth relative to the listener, a
    // distance attenuation, and a Doppler pitch from radial velocity, then posts a
    // SetSpatial command tagged with the source's client_id (ramped over the
    // tick's frame count so motion is smooth). GrainCloud sources stay ambient
    // (2D) and the listener's own node is skipped. NEVER starts a voice — it only
    // retunes ones already playing; posting to a finished one-shot's client_id is
    // a harmless no-op. With no active listener it does nothing (sources stay 2D).
    //
    // #221: the source/listener world poses are read straight from the instance's
    // simulation polytree (instance.storage.polytree, indexed by the record's
    // runtime node) — the SAME single source of truth scene_world_transforms()
    // draws + saves from — so this pass no longer takes (nor scans) the authored
    // scene_nodes_ span. A record whose node is out of the polytree's range is
    // skipped (defensive; the tables are built from the same instance).
    //
    //   instance     the runtime scene: audio_sources / audio_listeners (each
    //                record's `node` is the polytree handle) + storage.polytree
    //                (the world matrices the sim advanced this tick).
    //   dt           seconds since the last tick (for velocity/Doppler).
    //   sample_rate  the device sample rate (for ITD + ramp-frame math).
    //
    // Returns the number of SetSpatial commands posted (diagnostics/tests).
    uint32_t update_scene_audio_spatialization(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        float dt,
        uint32_t sample_rate,
        wz::audio::AudioScheduler& scheduler,
        AudioSpatializationState& state);

} // namespace wz::engine::audio
