// src/engine/audio/scene_audio.cpp

#include <engine/audio/scene_audio.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_instance.h>

#include <audio/audio_command.h>
#include <audio/audio_scheduler.h>

namespace wz::engine::audio {

    namespace {
        // Resolve an AudioSource's renderable terminal → clip → playable PCM view
        // + the renderable's baked params. False if the reference is empty or the
        // renderable/clip can't be resolved. Shared by the auto-play pass and the
        // behavior Play verb so both interpret the descriptor identically.
        bool resolve_source_voice(
            const wz::engine::assets::EngineAssetLibrary& assets,
            const wz::engine::assets::AudioSourceComponent& source,
            wz::audio::AudioBufferView& view,
            float& gain,
            float& pitch,
            bool& looping)
        {
            using namespace wz::engine::assets;

            if (source.audio_renderable == wz::asset::AssetKey{}) {
                return false;
            }
            const AudioRenderableData* renderable =
                assets.audio_renderables().get_audio_renderable_data(
                    assets.audio_renderables().get_audio_renderable(
                        AudioRenderableAsset{ .output = source.audio_renderable }));
            if (renderable == nullptr) {
                return false;
            }
            const AudioClipData* clip =
                assets.audio_clips().get_audio_clip_data(
                    AudioClipHandle{ renderable->clip });
            if (clip == nullptr || !clip->valid()) {
                return false;
            }
            view = clip->view();
            gain = renderable->gain;
            pitch = renderable->pitch;
            looping = renderable->looping;
            return true;
        }
    }

    ScenePlaybackReport play_scene_audio_sources(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler)
    {
        using namespace wz::engine::assets;

        ScenePlaybackReport report{};

        for (const auto& record : instance.audio_sources) {
            const AudioSourceComponent& source = record.component;
            // Stable per-entity tag (derived at instantiate from the node id) so a
            // behavior addressing this entity can stop/retune the same voice.
            const uint32_t client_id = source.client_id;

            if (!source.enabled) {
                ++report.skipped_disabled;
                continue;
            }
            if (!source.auto_play) {
                // Valid but manually triggered (e.g. by a behavior) — not played
                // here and not counted as skipped.
                continue;
            }

            // Resolve renderable→clip (empty/unresolvable key => node-id-only
            // authoring not yet materialized, or a missing renderable/clip).
            wz::audio::AudioCommand cmd{};
            cmd.type = wz::audio::AudioCommandType::Play;
            cmd.sample_time = 0;            // play as soon as the next block runs
            cmd.client_id = client_id;
            if (!resolve_source_voice(assets, source, cmd.source, cmd.gain,
                                      cmd.pitch, cmd.looping)) {
                ++report.skipped_unresolved;
                continue;
            }

            if (scheduler.post(cmd)) {
                ++report.played;
            }
            else {
                // Queue full this tick; treat as not played.
                ++report.skipped_unresolved;
            }
        }

        return report;
    }

    bool apply_audio_behavior_command(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler,
        AudioBehaviorVerb verb,
        wz::scene::RuntimeEntityId entity,
        float v0,
        float v1)
    {
        using namespace wz::engine::assets;

        // Resolve the addressed entity to its AudioSource (one per node, so the
        // entity is a complete address). No source → no-op.
        const AudioSourceComponent* source = nullptr;
        for (const auto& record : instance.audio_sources) {
            if (record.node == entity) {
                source = &record.component;
                break;
            }
        }
        if (source == nullptr) {
            return false;
        }

        wz::audio::AudioCommand cmd{};
        cmd.sample_time = 0;  // apply on the next block (no tick→sample map yet)
        cmd.client_id = source->client_id;

        switch (verb) {
        case AudioBehaviorVerb::Play:
            cmd.type = wz::audio::AudioCommandType::Play;
            if (!resolve_source_voice(assets, *source, cmd.source, cmd.gain,
                                      cmd.pitch, cmd.looping)) {
                return false;  // renderable doesn't resolve to a clip
            }
            break;
        case AudioBehaviorVerb::Stop:
            cmd.type = wz::audio::AudioCommandType::Stop;
            cmd.ramp_frames = (v0 > 0.0f) ? static_cast<uint32_t>(v0) : 0u;
            break;
        case AudioBehaviorVerb::SetGain:
            cmd.type = wz::audio::AudioCommandType::SetGain;
            cmd.value = v0;
            cmd.ramp_frames = (v1 > 0.0f) ? static_cast<uint32_t>(v1) : 0u;
            break;
        }

        return scheduler.post(cmd);
    }

} // namespace wz::engine::audio
