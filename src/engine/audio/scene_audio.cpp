// src/engine/audio/scene_audio.cpp

#include <engine/audio/scene_audio.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_instance.h>

#include <audio/audio_command.h>
#include <audio/audio_scheduler.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wz::engine::audio {

    // The behavior ABI passes grain params as WZ_GRAIN_PARAM_* ordinals; pin
    // GrainParam to the same values so the cast in apply_grain_param_command / the
    // scheduler is correct.
    static_assert(
        static_cast<uint8_t>(wz::audio::GrainParam::Gain) == 0
        && static_cast<uint8_t>(wz::audio::GrainParam::Density) == 1
        && static_cast<uint8_t>(wz::audio::GrainParam::Position) == 2
        && static_cast<uint8_t>(wz::audio::GrainParam::Pitch) == 3
        && static_cast<uint8_t>(wz::audio::GrainParam::BlendRate) == 4
        && static_cast<uint8_t>(wz::audio::GrainParam::BlendDepth) == 5
        && static_cast<uint8_t>(wz::audio::GrainParam::SourceWeight) == 6
        && static_cast<uint8_t>(wz::audio::GrainParam::PositionJitter) == 7
        && static_cast<uint8_t>(wz::audio::GrainParam::PitchJitter) == 8
        && static_cast<uint8_t>(wz::audio::GrainParam::PanCenter) == 9
        && static_cast<uint8_t>(wz::audio::GrainParam::PanSpread) == 10
        && static_cast<uint8_t>(wz::audio::GrainParam::GrainMs) == 11
        && static_cast<uint8_t>(wz::audio::GrainParam::WindowParam) == 12
        && static_cast<uint8_t>(wz::audio::GrainParam::Window) == 13,
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
            out.normalize = g.normalize;

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
        GrainCloudDescStore& grain_store,
        std::unordered_set<uint32_t>* played_clients)
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
            if (played_clients != nullptr
                && played_clients->count(client_id) != 0) {
                // Already started on an earlier pass (scene load, or a prior
                // spawn) — don't re-post. It's playing fine, so not "skipped".
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
                    if (played_clients != nullptr) {
                        played_clients->insert(client_id);
                    }
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
                if (played_clients != nullptr) {
                    played_clients->insert(client_id);
                }
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
        uint32_t ramp_frames,
        uint8_t source_index)
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
        cmd.grain_source_index = source_index;
        cmd.value = value;
        cmd.ramp_frames = ramp_frames;
        return scheduler.post(cmd);
    }

    // ─── Spatialization producer pass ──────────────────────────────────────────

    namespace {
        // The world is scene-scale (the test scene spans thousands of units), so
        // these distance-model defaults are picked to be audibly reasonable there
        // and trivially tunable later (no per-source knobs yet). Inside kRefDist a
        // source plays at full level; beyond it the level rolls off as
        //     atten = ref / (ref + rolloff * (dist - ref)).
        // kRolloff = 1 gives a gentle ~half-level at ref + ref/rolloff ≈ 2*ref.
        constexpr float kRefDistance = 50.0f;   // world units of "close"
        constexpr float kRolloff = 1.0f;        // unitless rolloff steepness
        constexpr float kSpeedOfSound = 343.0f; // m/s, for Doppler
        // Max ITD ≈ 0.66 ms (head width / speed of sound). At 48 kHz that's ~32
        // output frames — the far ear's delay at full lateral offset.
        constexpr float kMaxItdSeconds = 0.00066f;

        constexpr float kPi = 3.14159265358979323846f;

        using Vec3 = wz::math::Vec3;

        Vec3 mat_translation(const wz::math::Mat4& m) noexcept
        {
            return { m.m[12], m.m[13], m.m[14] };  // column-major
        }
        // Column 0 = local +X (right); column 2 = local +Z (forward). Normalized
        // so a scaled node (e.g. the test tank's 0.5) doesn't skew the basis.
        Vec3 mat_axis(const wz::math::Mat4& m, int col) noexcept
        {
            const int b = col * 4;
            Vec3 v{ m.m[b + 0], m.m[b + 1], m.m[b + 2] };
            const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len > 1.0e-6f) {
                v.x /= len; v.y /= len; v.z /= len;
            }
            return v;
        }
        float dot(const Vec3& a, const Vec3& b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        // The world matrix of a record's runtime node, straight from the sim
        // polytree (the #221 single source of truth) — nullptr if the handle is
        // out of range. The record's `node` IS the polytree handle, so this is the
        // same read scene_world_transforms() does, without an id→index scan.
        const wz::math::Mat4* node_world(
            const wz::engine::assets::SceneInstance& instance,
            wz::scene::RuntimeEntityId entity) noexcept
        {
            if (entity >= wz::core::graph::node_count(instance.storage.polytree)) {
                return nullptr;
            }
            return &wz::core::graph::node_data(
                       instance.storage.polytree, entity)
                        .world;
        }
    }

    uint32_t update_scene_audio_spatialization(
        const wz::engine::assets::EngineAssetLibrary& assets,
        const wz::engine::assets::SceneInstance& instance,
        float dt,
        uint32_t sample_rate,
        wz::audio::AudioScheduler& scheduler,
        AudioSpatializationState& state)
    {
        using namespace wz::engine::assets;

        if (sample_rate == 0) {
            return 0;
        }

        // 1. Resolve the active listener: the first listener record marked active.
        //    No active listener => leave every source 2D (do nothing).
        const wz::math::Mat4* listener_world = nullptr;
        wz::scene::RuntimeEntityId listener_entity =
            wz::scene::INVALID_RUNTIME_ENTITY;
        for (const auto& record : instance.audio_listeners) {
            if (!record.component.active) {
                continue;
            }
            const wz::math::Mat4* world = node_world(instance, record.node);
            if (world == nullptr) {
                continue;
            }
            listener_world = world;
            listener_entity = record.node;
            break;
        }
        if (listener_world == nullptr) {
            // Drop any stale listener velocity so re-acquiring a listener later
            // doesn't compute a bogus jump on the first reacquired tick.
            state.has_prev_listener = false;
            return 0;
        }

        const Vec3 listener_pos = mat_translation(*listener_world);
        const Vec3 listener_right = mat_axis(*listener_world, 0);
        const Vec3 listener_forward = mat_axis(*listener_world, 2);

        // Listener velocity (0 on the first tick after acquiring it).
        Vec3 listener_vel{};
        const bool have_dt = dt > 1.0e-6f;
        if (state.has_prev_listener && have_dt) {
            listener_vel = {
                (listener_pos.x - state.prev_listener_pos.x) / dt,
                (listener_pos.y - state.prev_listener_pos.y) / dt,
                (listener_pos.z - state.prev_listener_pos.z) / dt,
            };
        }

        const float max_itd_frames =
            std::round(kMaxItdSeconds * static_cast<float>(sample_rate));
        // Ramp each per-tick steer over the tick's worth of output frames so the
        // pan/ITD/Doppler slew smoothly between updates (de-zipper).
        const uint32_t ramp_frames = have_dt
            ? static_cast<uint32_t>(
                  std::round(dt * static_cast<float>(sample_rate)))
            : 0u;

        uint32_t posted = 0;
        for (const auto& record : instance.audio_sources) {
            const AudioSourceComponent& source = record.component;
            if (!source.enabled || source.client_id == 0) {
                continue;
            }
            if (record.node == listener_entity) {
                continue;  // never spatialize the listener's own node
            }

            // GrainCloud sources stay ambient (2D); only Clip sources position.
            const AudioRenderableData* renderable =
                fetch_renderable(assets, source);
            if (renderable == nullptr
                || renderable->kind != AudioRenderableKind::Clip) {
                continue;
            }

            const wz::math::Mat4* source_world =
                node_world(instance, record.node);
            if (source_world == nullptr) {
                continue;
            }
            const Vec3 source_pos = mat_translation(*source_world);

            // Relative vector listener→source, and distance.
            const Vec3 rel = {
                source_pos.x - listener_pos.x,
                source_pos.y - listener_pos.y,
                source_pos.z - listener_pos.z,
            };
            const float dist =
                std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);

            // 2. Azimuth → pan + ITD. Project rel onto the listener's right/
            //    forward (horizontal plane; elevation ignored for v1). pan in
            //    [-1,1] from the lateral fraction; equal-power gains from
            //    theta = (pan+1)*pi/4 (left = cos, right = sin).
            float pan = 0.0f;
            float sin_azimuth = 0.0f;
            if (dist > 1.0e-4f) {
                const float lateral = dot(rel, listener_right);
                const float forward = dot(rel, listener_forward);
                // azimuth measured from forward; sin(az) = lateral / horiz_len.
                const float horiz =
                    std::sqrt(lateral * lateral + forward * forward);
                if (horiz > 1.0e-4f) {
                    sin_azimuth = lateral / horiz;
                }
                pan = std::clamp(lateral / dist, -1.0f, 1.0f);
            }
            const float theta = (pan + 1.0f) * (kPi * 0.25f);
            float gain_l = std::cos(theta);
            float gain_r = std::sin(theta);
            // itd_frames > 0 => source on the right => LEFT (far) leg delayed,
            // matching Voice's convention (sin_azimuth > 0 is the right side).
            const float itd_frames = sin_azimuth * max_itd_frames;

            // 3. Distance attenuation (inverse model), folded into both gains.
            float atten = 1.0f;
            if (dist > kRefDistance) {
                atten = kRefDistance
                    / (kRefDistance + kRolloff * (dist - kRefDistance));
            }
            gain_l *= atten;
            gain_r *= atten;

            // 4. Doppler from radial velocity along the unit rel vector
            //    (positive = receding). Skipped until we have a prev position.
            float pitch = 1.0f;
            const auto prev_it = state.prev_source_pos.find(source.client_id);
            if (prev_it != state.prev_source_pos.end() && have_dt
                && dist > 1.0e-4f) {
                const Vec3 prev = prev_it->second;
                const Vec3 source_vel = {
                    (source_pos.x - prev.x) / dt,
                    (source_pos.y - prev.y) / dt,
                    (source_pos.z - prev.z) / dt,
                };
                const Vec3 rel_vel = {
                    source_vel.x - listener_vel.x,
                    source_vel.y - listener_vel.y,
                    source_vel.z - listener_vel.z,
                };
                const Vec3 unit = { rel.x / dist, rel.y / dist, rel.z / dist };
                const float v_r = dot(rel_vel, unit);  // + = receding
                pitch = std::clamp(
                    kSpeedOfSound / (kSpeedOfSound + v_r), 0.5f, 2.0f);
            }
            state.prev_source_pos[source.client_id] = source_pos;

            wz::audio::AudioCommand cmd{};
            cmd.type = wz::audio::AudioCommandType::SetSpatial;
            cmd.sample_time = 0;  // apply on the next block
            cmd.client_id = source.client_id;
            cmd.gain_l = gain_l;
            cmd.gain_r = gain_r;
            cmd.itd_frames = itd_frames;
            // COMPOSE Doppler with the AUTHORED playback rate, do not replace
            // it (#313, B4-C3). Voice has one pitch field and set_spatial_client
            // ASSIGNS into it, so sending a Doppler-only ratio here wiped the
            // renderable's authored pitch on the first spatialization tick after
            // a voice started -- the Pitch dial was dead for every spatialized
            // clip in play mode, and Doppler was being applied around the wrong
            // base. Reusing the pitch field as the Doppler transport is
            // deliberate (see voice.h and Mixer::set_spatial_client); computing
            // the ratio from a hardcoded 1.0 was not.
            //
            // The clamp above bounds the DOPPLER RATIO; the authored factor
            // multiplies on top of it, so an authored 2.0 stays 2.0 at rest
            // rather than being squeezed into the Doppler range.
            cmd.pitch = pitch * renderable->pitch;
            cmd.ramp_frames = ramp_frames;
            if (scheduler.post(cmd)) {
                ++posted;
            }
        }

        // Remember the listener position for next tick's velocity.
        state.prev_listener_pos = listener_pos;
        state.has_prev_listener = true;

        return posted;
    }

} // namespace wz::engine::audio
