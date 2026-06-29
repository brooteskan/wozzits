// src/engine/audio/scene_audio.cpp

#include <engine/audio/scene_audio.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_instance.h>

#include <audio/audio_command.h>
#include <audio/audio_scheduler.h>

namespace wz::engine::audio {

    ScenePlaybackReport play_scene_audio_sources(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler)
    {
        using namespace wz::engine::assets;

        ScenePlaybackReport report{};

        uint32_t next_client_id = 1;

        for (const auto& record : instance.audio_sources) {
            const AudioSourceComponent& source = record.component;
            const uint32_t client_id = next_client_id++;

            if (!source.enabled) {
                ++report.skipped_disabled;
                continue;
            }
            if (!source.auto_play) {
                // Valid but manually triggered (e.g. by a behavior) — not played
                // here and not counted as skipped.
                continue;
            }

            // An empty key means the reference is unresolved (node-id-only
            // authoring must be materialized to a key before runtime).
            if (source.audio_renderable == wz::asset::AssetKey{}) {
                ++report.skipped_unresolved;
                continue;
            }

            // renderable terminal -> resolved descriptor
            const AudioRenderableHandle renderable_handle =
                assets.audio_renderables().get_audio_renderable(
                    AudioRenderableAsset{ .output = source.audio_renderable });
            const AudioRenderableData* renderable =
                assets.audio_renderables().get_audio_renderable_data(
                    renderable_handle);
            if (renderable == nullptr) {
                ++report.skipped_unresolved;
                continue;
            }

            // resolved clip handle -> CPU PCM
            const AudioClipData* clip =
                assets.audio_clips().get_audio_clip_data(
                    AudioClipHandle{ renderable->clip });
            if (clip == nullptr || !clip->valid()) {
                ++report.skipped_unresolved;
                continue;
            }

            wz::audio::AudioCommand cmd{};
            cmd.type = wz::audio::AudioCommandType::Play;
            cmd.sample_time = 0;            // play as soon as the next block runs
            cmd.source = clip->view();
            cmd.gain = renderable->gain;
            cmd.pitch = renderable->pitch;
            cmd.looping = renderable->looping;
            cmd.client_id = client_id;

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

} // namespace wz::engine::audio
