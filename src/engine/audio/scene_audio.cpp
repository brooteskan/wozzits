// src/engine/audio/scene_audio.cpp

#include <engine/audio/scene_audio.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_instance.h>

#include <audio/audio_command.h>
#include <audio/audio_scheduler.h>

#include <cstring>

namespace wz::engine::audio {

    // The behavior ABI passes grain params as WZ_GRAIN_PARAM_* ordinals (0..3);
    // pin GrainParam to the same values so the cast in apply_grain_param_command /
    // the scheduler is correct.
    static_assert(
        static_cast<uint8_t>(wz::audio::GrainParam::Gain) == 0
        && static_cast<uint8_t>(wz::audio::GrainParam::Density) == 1
        && static_cast<uint8_t>(wz::audio::GrainParam::Position) == 2
        && static_cast<uint8_t>(wz::audio::GrainParam::Pitch) == 3
        && static_cast<uint8_t>(wz::audio::GrainParam::BlendRate) == 4
        && static_cast<uint8_t>(wz::audio::GrainParam::BlendDepth) == 5,
        "GrainParam ordinals must match WZ_GRAIN_PARAM_* in the behavior ABI");

    namespace {
        // Which clip of a (possibly bank-backed) renderable to play. Default uses
        // the renderable's default_index (auto-play); Index selects by ordinal;
        // Name selects by FNV-1a/32 name hash. Name/out-of-range that don't resolve
        // fall back to default_index. A single-clip renderable has one clip, so
        // any selector resolves to it.
        struct ClipSelector {
            enum class Mode { Default, Index, Name } mode = Mode::Default;
            uint32_t index = 0;
            uint32_t name_hash = 0;
        };

        // Resolve an AudioSource's renderable terminal → clip → playable PCM view
        // + the renderable's baked params. False if the reference is empty or the
        // renderable/clip can't be resolved. Shared by the auto-play pass and the
        // behavior Play verb so both interpret the descriptor identically. All clip
        // selection (index/name → ordinal) happens here, against the resolved
        // renderable.
        bool resolve_source_voice(
            const wz::engine::assets::EngineAssetLibrary& assets,
            const wz::engine::assets::AudioSourceComponent& source,
            const ClipSelector& selector,
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
            if (renderable == nullptr || !renderable->valid()) {
                return false;
            }
            uint32_t index = renderable->default_index;
            switch (selector.mode) {
            case ClipSelector::Mode::Default:
                break;
            case ClipSelector::Mode::Index:
                index = selector.index;
                break;
            case ClipSelector::Mode::Name: {
                const int found =
                    renderable->index_for_name_hash(selector.name_hash);
                if (found >= 0) {
                    index = static_cast<uint32_t>(found);
                }
                // Unknown name → keep default_index.
                break;
            }
            }
            const wz::asset::ResourceHandle clip_handle =
                renderable->clip_at(index);
            const AudioClipData* clip =
                assets.audio_clips().get_audio_clip_data(
                    AudioClipHandle{ clip_handle });
            if (clip == nullptr || !clip->valid()) {
                return false;
            }
            view = clip->view();
            gain = renderable->gain;
            pitch = renderable->pitch;
            looping = renderable->looping;
            return true;
        }

        // Resolve an AudioSource's referenced renderable terminal (any kind), or
        // nullptr if the reference is empty/unresolvable.
        const wz::engine::assets::AudioRenderableData* fetch_renderable(
            const wz::engine::assets::EngineAssetLibrary& assets,
            const wz::engine::assets::AudioSourceComponent& source)
        {
            using namespace wz::engine::assets;
            if (source.audio_renderable == wz::asset::AssetKey{}) {
                return nullptr;
            }
            return assets.audio_renderables().get_audio_renderable_data(
                assets.audio_renderables().get_audio_renderable(
                    AudioRenderableAsset{ .output = source.audio_renderable }));
        }

        // Fill a GrainCloudDesc from a GrainCloud renderable: resolve its clip
        // handles into PCM views (the generator's sources) and copy the authored
        // granular params. False if no source clip resolves.
        bool build_grain_cloud_desc(
            const wz::engine::assets::EngineAssetLibrary& assets,
            const wz::engine::assets::AudioRenderableData& renderable,
            wz::audio::GrainCloudDesc& out)
        {
            using namespace wz::engine::assets;

            out = wz::audio::GrainCloudDesc{};
            const GrainCloudParams& g = renderable.grain;
            out.max_grains = g.max_grains;
            out.seed = g.seed;
            out.gain = g.gain;
            out.density = g.density;
            out.position = g.position;
            out.pitch = g.pitch;
            out.position_jitter = g.position_jitter;
            out.pitch_jitter_semitones = g.pitch_jitter_semitones;
            out.pan_center = g.pan_center;
            out.pan_spread = g.pan_spread;
            out.grain_ms = g.grain_ms;
            out.window = g.window;
            out.window_param = g.window_param;
            out.blend_rate = g.blend_rate;
            out.blend_depth = g.blend_depth;

            uint32_t n = 0;
            for (size_t i = 0;
                 i < renderable.clips.size() && n < wz::audio::kMaxGrainSources;
                 ++i)
            {
                const AudioClipData* clip =
                    assets.audio_clips().get_audio_clip_data(
                        AudioClipHandle{ renderable.clips[i] });
                if (clip == nullptr || !clip->valid()) {
                    continue;
                }
                out.sources[n] = clip->view();
                out.weights[n] = 1.0f;
                ++n;
            }
            out.source_count = n;
            return n > 0;
        }
    }

    ScenePlaybackReport play_scene_audio_sources(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler,
        GrainCloudDescStore& grain_store)
    {
        using namespace wz::engine::assets;

        ScenePlaybackReport report{};

        for (const auto& record : instance.audio_sources) {
            const AudioSourceComponent& source = record.component;
            // Stable per-entity tag (derived at instantiate from the node id) so a
            // behavior addressing this entity can stop/retune the same voice/cloud.
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

            const AudioRenderableData* renderable =
                fetch_renderable(assets, source);
            if (renderable == nullptr || !renderable->valid()) {
                ++report.skipped_unresolved;
                continue;
            }

            if (renderable->kind == AudioRenderableKind::GrainCloud) {
                // Build a stable desc (the command carries a pointer) and start the
                // cloud as a continuous bed tagged with the source's client id.
                wz::audio::GrainCloudDesc& desc = grain_store.allocate();
                if (!build_grain_cloud_desc(assets, *renderable, desc)) {
                    ++report.skipped_unresolved;
                    continue;
                }
                wz::audio::AudioCommand cmd{};
                cmd.type = wz::audio::AudioCommandType::PlayGrainCloud;
                cmd.sample_time = 0;
                cmd.client_id = client_id;
                cmd.grain = &desc;
                if (scheduler.post(cmd)) {
                    ++report.played;
                }
                else {
                    ++report.skipped_unresolved;
                }
                continue;
            }

            // Clip renderable: post a voice Play using the default clip.
            wz::audio::AudioCommand cmd{};
            cmd.type = wz::audio::AudioCommandType::Play;
            cmd.sample_time = 0;            // play as soon as the next block runs
            cmd.client_id = client_id;
            if (!resolve_source_voice(assets, source, ClipSelector{},
                                      cmd.source, cmd.gain, cmd.pitch,
                                      cmd.looping)) {
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
        case AudioBehaviorVerb::Play: {
            cmd.type = wz::audio::AudioCommandType::Play;
            // Clip selection for a bank-backed renderable, encoded in v0/v1:
            //   v0 <= -2  → select by name; v1 carries the 32-bit name hash as a
            //               bit pattern (reinterpreted, not a numeric value).
            //   v0 == -1  → use the renderable's default_index.
            //   v0 >= 0   → select that clip index (rounded).
            ClipSelector selector{};
            if (v0 <= -1.5f) {
                selector.mode = ClipSelector::Mode::Name;
                std::memcpy(&selector.name_hash, &v1, sizeof(uint32_t));
            }
            else if (v0 < 0.0f) {
                selector.mode = ClipSelector::Mode::Default;
            }
            else {
                selector.mode = ClipSelector::Mode::Index;
                selector.index = static_cast<uint32_t>(v0 + 0.5f);
            }
            if (!resolve_source_voice(assets, *source, selector,
                                      cmd.source, cmd.gain, cmd.pitch,
                                      cmd.looping)) {
                return false;  // renderable doesn't resolve to a clip
            }
            break;
        }
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

    bool apply_grain_param_command(
        const wz::engine::assets::SceneInstance& instance,
        wz::audio::AudioScheduler& scheduler,
        wz::scene::RuntimeEntityId entity,
        uint8_t param_id,
        float value,
        uint32_t ramp_frames)
    {
        using namespace wz::engine::assets;

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
        cmd.type = wz::audio::AudioCommandType::SetGrainParam;
        cmd.sample_time = 0;  // apply on the next block
        cmd.client_id = source->client_id;
        cmd.grain_param = param_id;
        cmd.value = value;
        cmd.ramp_frames = ramp_frames;
        return scheduler.post(cmd);
    }

} // namespace wz::engine::audio
