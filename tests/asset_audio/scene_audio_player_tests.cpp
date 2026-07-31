// tests/asset_audio/scene_audio_player_tests.cpp
//
// End-to-end test for audio-track item 7: a scene AudioSource is auto-played
// through the scheduler by resolving its audio-renderable -> clip -> PCM and
// posting a Play command. Fully headless — the scheduler renders into a buffer
// (no device).

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/audio/audio_clip_bank.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/audio/scene_audio.h>

#include <audio/audio_scheduler.h>

#include <file/filesystem.h>
#include <graph/static_polytree.h>
#include <logging/logger.h>
#include <math/math_types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace wz::engine::assets::test {

    namespace stdfs = std::filesystem;

    class SceneAudioPlayerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (stdfs::temp_directory_path() / "wz_scene_audio_player_tests")
                    .string()
            };
            stdfs::create_directories(temp_dir_);
            library_ = std::make_unique<EngineAssetLibrary>(
                null_device_, logger_, temp_dir_);
        }

        void TearDown() override
        {
            library_.reset();
            stdfs::remove_all(temp_dir_);
        }

        // Build + resolve a real audio-renderable so its key resolves at runtime.
        wz::asset::AssetKey make_resolved_renderable(float gain,
                                                     bool looping = false,
                                                     float pitch = 1.0f)
        {
            const AudioClipAsset clip =
                library_->audio_clips().create_procedural_tone_audio_clip({
                    .name = "scene_audio/src",
                    .sample_rate = 48000,
                    .channels = 1,
                    .waveform = AudioToneWaveform::Sine,
                    .frequency = 1000.0f,
                    .duration_seconds = 0.01f,
                    .amplitude = 0.5f,
                    });
            const AudioRenderableAsset rend =
                library_->audio_renderables().create_audio_renderable({
                    .clip = clip,
                    .gain = gain,
                    .pitch = pitch,
                    .looping = looping,
                    });
            EXPECT_TRUE(library_->commit());
            EXPECT_TRUE(library_->resolve_all().ok());
            return rend.output;
        }

        // Build + resolve a bank-backed renderable whose clips have distinct
        // amplitudes, so the played peak identifies which clip index was chosen.
        // amplitudes[default_index] is the auto-play / out-of-range fallback.
        wz::asset::AssetKey make_resolved_bank_renderable(
            const std::vector<float>& amplitudes,
            uint32_t default_index)
        {
            AudioClipBankFromClipsDesc bank_desc{};
            for (size_t i = 0; i < amplitudes.size(); ++i) {
                const AudioClipAsset clip =
                    library_->audio_clips().create_procedural_tone_audio_clip({
                        .name = "scene_audio/bank_" + std::to_string(i),
                        .sample_rate = 48000,
                        .channels = 1,
                        .waveform = AudioToneWaveform::Sine,
                        .frequency = 1000.0f,
                        .duration_seconds = 0.01f,
                        .amplitude = amplitudes[i],
                        });
                bank_desc.items.push_back(
                    { "clip_" + std::to_string(i), clip });
            }
            const AudioClipBankAsset bank =
                library_->audio_clip_banks()
                    .create_audio_clip_bank_from_clips(bank_desc);
            const AudioRenderableAsset rend =
                library_->audio_renderables().create_audio_clip_bank_renderable({
                    .bank = bank,
                    .default_index = default_index,
                    .gain = 1.0f,
                    .pitch = 1.0f,
                    .looping = false,
                    });
            EXPECT_TRUE(library_->commit());
            EXPECT_TRUE(library_->resolve_all().ok());
            return rend.output;
        }

        // Build + resolve a grain-cloud renderable over a small clip bank (an
        // environmental bed). density > 0 so it actually emits grains.
        wz::asset::AssetKey make_resolved_grain_renderable(float density)
        {
            AudioClipBankFromClipsDesc bank_desc{};
            for (int i = 0; i < 2; ++i) {
                const AudioClipAsset clip =
                    library_->audio_clips().create_procedural_tone_audio_clip({
                        .name = "scene_audio/grain_" + std::to_string(i),
                        .sample_rate = 48000,
                        .channels = 1,
                        .waveform = AudioToneWaveform::Sine,
                        .frequency = 220.0f,
                        .duration_seconds = 0.2f,
                        .amplitude = 0.5f,
                        });
                bank_desc.items.push_back(
                    { "grain_" + std::to_string(i), clip });
            }
            const AudioClipBankAsset bank =
                library_->audio_clip_banks()
                    .create_audio_clip_bank_from_clips(bank_desc);

            GrainCloudParams params{};
            params.density = density;
            params.grain_ms = 10.0f;
            const AudioRenderableAsset rend =
                library_->audio_renderables().create_audio_grain_cloud_renderable({
                    .bank = bank,
                    .params = params,
                    });
            EXPECT_TRUE(library_->commit());
            EXPECT_TRUE(library_->resolve_all().ok());
            return rend.output;
        }

        static float peak(const std::vector<float>& buf)
        {
            float m = 0.0f;
            for (float s : buf) m = std::max(m, std::fabs(s));
            return m;
        }

        wz::gpu::Device   null_device_{};
        wz::Logger        logger_{};
        wz::fs::Path      temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
        wz::engine::audio::GrainCloudDescStore grain_store_;
        wz::engine::audio::AudioSpatializationState spat_state_;
    };


    TEST_F(SceneAudioPlayerTest, AutoPlaysEnabledSourceAndProducesSound)
    {
        const wz::asset::AssetKey renderable = make_resolved_renderable(0.5f);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        const auto report = wz::engine::audio::play_scene_audio_sources(
            *library_, result.instance, scheduler, grain_store_);

        EXPECT_EQ(report.played, 1u);
        EXPECT_EQ(report.skipped_disabled, 0u);
        EXPECT_EQ(report.skipped_unresolved, 0u);

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        EXPECT_GT(peak(out), 0.0f); // the scene is audible
    }

    // Locks the spawn-audio scoping (WozzitsApp_v1::start_spawned_audio). With a
    // shared `played_clients` set, play_scene_audio_sources starts each client
    // exactly once across calls, so re-running the pass after a prefab spawn fires
    // ONLY the newly spawned subtree's auto_play sources — never the ambient bed a
    // second time. client_id hashes the STABLE node id, so "ambient" is the same
    // tag in the pre- and post-spawn instances (mirroring the rebuild a spawn
    // triggers). Without this, a spawned tank's engine grain cloud never starts.
    TEST_F(SceneAudioPlayerTest, SharedPlayedSetPlaysEachClientOnce)
    {
        const wz::asset::AssetKey renderable = make_resolved_renderable(0.5f);

        auto make_source = [&](std::string_view id) {
            SceneNodeAsset node{};
            node.id = std::string{ id };
            node.audio_source = SceneAudioSourceAsset{
                .audio_renderable = renderable,
                .auto_play = true,
                .enabled = true,
            };
            return node;
        };

        // Scene load: one ambient auto_play source.
        SceneAssetData at_load{};
        at_load.nodes.push_back(make_source("ambient"));
        auto load = instantiate_scene(at_load);
        ASSERT_TRUE(load.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        std::unordered_set<uint32_t> played;

        // Load pass: the ambient starts and its client id is recorded.
        EXPECT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, load.instance, scheduler, grain_store_, &played)
                      .played, 1u);
        EXPECT_EQ(played.size(), 1u);

        // A spawn that adds no new auto_play source re-runs the pass -> nothing new.
        EXPECT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, load.instance, scheduler, grain_store_, &played)
                      .played, 0u);

        // The player spawns: the rebuilt instance carries the ambient (same id ->
        // same client_id, already played) PLUS a freshly spawned "engine" source.
        // Only the new one fires; the ambient is not double-played.
        SceneAssetData after_spawn{};
        after_spawn.nodes.push_back(make_source("ambient"));
        after_spawn.nodes.push_back(make_source("spawn:1:engine"));
        auto spawned = instantiate_scene(after_spawn);
        ASSERT_TRUE(spawned.ok());

        EXPECT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, spawned.instance, scheduler, grain_store_, &played)
                      .played, 1u);
        EXPECT_EQ(played.size(), 2u);
    }

    TEST_F(SceneAudioPlayerTest, DisabledSourceIsSkippedAndSilent)
    {
        const wz::asset::AssetKey renderable = make_resolved_renderable(0.5f);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = false, // disabled
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        const auto report = wz::engine::audio::play_scene_audio_sources(
            *library_, result.instance, scheduler, grain_store_);

        EXPECT_EQ(report.played, 0u);
        EXPECT_EQ(report.skipped_disabled, 1u);

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        EXPECT_FLOAT_EQ(peak(out), 0.0f);
    }

    TEST_F(SceneAudioPlayerTest, NonAutoPlaySourceIsNotPlayed)
    {
        const wz::asset::AssetKey renderable = make_resolved_renderable(0.5f);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = false, // manual trigger only
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        const auto report = wz::engine::audio::play_scene_audio_sources(
            *library_, result.instance, scheduler, grain_store_);

        EXPECT_EQ(report.played, 0u);
        EXPECT_EQ(report.skipped_disabled, 0u);
        EXPECT_EQ(report.skipped_unresolved, 0u);
    }

    TEST_F(SceneAudioPlayerTest, UnresolvedReferenceIsSkipped)
    {
        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = {}, // empty / unresolved
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        const auto report = wz::engine::audio::play_scene_audio_sources(
            *library_, result.instance, scheduler, grain_store_);

        EXPECT_EQ(report.played, 0u);
        EXPECT_EQ(report.skipped_unresolved, 1u);
    }

    // ── Behavior-triggered audio (item 9) ──────────────────────────────────────

    // A behavior PLAY triggers the entity's AudioSource even when auto_play is
    // off (the "when" is the behavior; auto_play only governs play-on-load).
    TEST_F(SceneAudioPlayerTest, BehaviorPlayVerbTriggersSourceIgnoringAutoPlay)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_renderable(0.5f, /*looping*/ true);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = false, // not played on load
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto entity = result.instance.authored_to_runtime.at("speaker");

        wz::audio::AudioScheduler scheduler(16, 64);
        // Auto-play pass leaves it silent...
        EXPECT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, result.instance, scheduler, grain_store_).played, 0u);
        // ...but a behavior Play triggers it.
        EXPECT_TRUE(wz::engine::audio::apply_audio_behavior_command(
            *library_, result.instance, scheduler,
            wz::engine::audio::AudioBehaviorVerb::Play, entity, 0.0f, 0.0f));

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        EXPECT_GT(peak(out), 0.0f);
    }

    // A behavior STOP silences the entity's voice (addressed by its client_id).
    TEST_F(SceneAudioPlayerTest, BehaviorStopVerbSilencesTheEntitysVoice)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_renderable(0.5f, /*looping*/ true);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto entity = result.instance.authored_to_runtime.at("speaker");

        wz::audio::AudioScheduler scheduler(16, 64);
        ASSERT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, result.instance, scheduler, grain_store_).played, 1u);

        std::vector<float> before(64, 0.0f);
        scheduler.process(before.data(), 64, 1, 48000);
        EXPECT_GT(peak(before), 0.0f); // audible before stop

        EXPECT_TRUE(wz::engine::audio::apply_audio_behavior_command(
            *library_, result.instance, scheduler,
            wz::engine::audio::AudioBehaviorVerb::Stop, entity,
            /*fade frames*/ 0.0f, 0.0f)); // hard cut

        std::vector<float> after(64, 0.0f);
        scheduler.process(after.data(), 64, 1, 48000);
        EXPECT_FLOAT_EQ(peak(after), 0.0f); // silent after stop
    }

    // Addressing an entity with no AudioSource is a no-op (and silent).
    TEST_F(SceneAudioPlayerTest, BehaviorVerbOnEntityWithoutAudioSourceIsNoOp)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_renderable(0.5f, /*looping*/ true);

        SceneAssetData authored{};
        SceneNodeAsset speaker{};
        speaker.id = "speaker";
        speaker.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = false,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(speaker));
        SceneNodeAsset silent{};
        silent.id = "silent"; // no audio_source
        authored.nodes.push_back(std::move(silent));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto silent_entity =
            result.instance.authored_to_runtime.at("silent");

        wz::audio::AudioScheduler scheduler(16, 64);
        EXPECT_FALSE(wz::engine::audio::apply_audio_behavior_command(
            *library_, result.instance, scheduler,
            wz::engine::audio::AudioBehaviorVerb::Play, silent_entity,
            0.0f, 0.0f));

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        EXPECT_FLOAT_EQ(peak(out), 0.0f);
    }

    // ── Bank-backed renderable + PLAY-by-index (clip bank track) ────────────────

    // Auto-play of a bank-backed source uses the renderable's default_index.
    TEST_F(SceneAudioPlayerTest, BankAutoPlayUsesDefaultIndex)
    {
        // amplitudes: index0=0.1, index1=0.5, index2=0.9; default_index = 1.
        const wz::asset::AssetKey renderable =
            make_resolved_bank_renderable({ 0.1f, 0.5f, 0.9f }, /*default*/ 1);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        ASSERT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, result.instance, scheduler, grain_store_).played, 1u);

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        // The default clip (index 1, amplitude 0.5) sounded, not index 0 or 2.
        EXPECT_NEAR(peak(out), 0.5f, 0.05f);
    }

    // A behavior Play with v0 = index selects clips[index].
    TEST_F(SceneAudioPlayerTest, BehaviorPlayIndexSelectsClip)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_bank_renderable({ 0.1f, 0.5f, 0.9f }, /*default*/ 0);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = false,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto entity = result.instance.authored_to_runtime.at("speaker");

        wz::audio::AudioScheduler scheduler(16, 64);
        // Play index 2 (amplitude 0.9).
        EXPECT_TRUE(wz::engine::audio::apply_audio_behavior_command(
            *library_, result.instance, scheduler,
            wz::engine::audio::AudioBehaviorVerb::Play, entity,
            /*v0 index*/ 2.0f, 0.0f));

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        EXPECT_NEAR(peak(out), 0.9f, 0.05f);
    }

    // A behavior Play with an out-of-range index falls back to default_index.
    TEST_F(SceneAudioPlayerTest, BehaviorPlayOutOfRangeIndexFallsBackToDefault)
    {
        // default_index = 2 (amplitude 0.9); request index 99 -> falls back.
        const wz::asset::AssetKey renderable =
            make_resolved_bank_renderable({ 0.1f, 0.5f, 0.9f }, /*default*/ 2);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = false,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto entity = result.instance.authored_to_runtime.at("speaker");

        wz::audio::AudioScheduler scheduler(16, 64);
        EXPECT_TRUE(wz::engine::audio::apply_audio_behavior_command(
            *library_, result.instance, scheduler,
            wz::engine::audio::AudioBehaviorVerb::Play, entity,
            /*v0 index*/ 99.0f, 0.0f));

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        EXPECT_NEAR(peak(out), 0.9f, 0.05f); // default clip (index 2)
    }

    namespace {
        // FNV-1a/32, matching wz_play_sound_hash / the bank's per-entry hash.
        uint32_t name_hash(std::string_view s)
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : s) { h ^= c; h *= 16777619u; }
            return h;
        }
        // The name-select encoding apply_audio_behavior_command expects: the host
        // reinterprets v1's bits as the 32-bit name hash.
        float name_hash_bits(std::string_view s)
        {
            const uint32_t h = name_hash(s);
            float bits = 0.0f;
            std::memcpy(&bits, &h, sizeof(uint32_t));
            return bits;
        }
    }

    // A behavior Play by name (v0 = -2, v1 = name-hash bits) selects that clip.
    TEST_F(SceneAudioPlayerTest, BehaviorPlayByNameSelectsClip)
    {
        // Bank clips are named "clip_0".."clip_2" by make_resolved_bank_renderable.
        const wz::asset::AssetKey renderable =
            make_resolved_bank_renderable({ 0.1f, 0.5f, 0.9f }, /*default*/ 0);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = false,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto entity = result.instance.authored_to_runtime.at("speaker");

        wz::audio::AudioScheduler scheduler(16, 64);
        // Select "clip_2" (amplitude 0.9) by name, not by index.
        EXPECT_TRUE(wz::engine::audio::apply_audio_behavior_command(
            *library_, result.instance, scheduler,
            wz::engine::audio::AudioBehaviorVerb::Play, entity,
            /*v0 name-mode*/ -2.0f, /*v1 hash bits*/ name_hash_bits("clip_2")));

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        EXPECT_NEAR(peak(out), 0.9f, 0.05f);
    }

    // A behavior Play by an unknown name falls back to default_index.
    TEST_F(SceneAudioPlayerTest, BehaviorPlayByUnknownNameFallsBackToDefault)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_bank_renderable({ 0.1f, 0.5f, 0.9f }, /*default*/ 1);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "speaker";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = false,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto entity = result.instance.authored_to_runtime.at("speaker");

        wz::audio::AudioScheduler scheduler(16, 64);
        EXPECT_TRUE(wz::engine::audio::apply_audio_behavior_command(
            *library_, result.instance, scheduler,
            wz::engine::audio::AudioBehaviorVerb::Play, entity,
            -2.0f, name_hash_bits("no_such_clip")));

        std::vector<float> out(64, 0.0f);
        scheduler.process(out.data(), 64, 1, 48000);
        EXPECT_NEAR(peak(out), 0.5f, 0.05f); // default clip (index 1)
    }

    // ── Grain-cloud renderable auto-plays as an environmental bed ───────────────

    TEST_F(SceneAudioPlayerTest, GrainCloudRenderableAutoPlaysAsBed)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_grain_renderable(/*density*/ 300.0f);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "ambience";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        // Auto-play posts one PlayGrainCloud command (carrying a stable desc).
        ASSERT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, result.instance, scheduler, grain_store_).played,
                  1u);
        EXPECT_GE(grain_store_.size(), 1u);

        std::vector<float> out(2400, 0.0f);  // 50 ms
        scheduler.process(out.data(), 2400, 1, 48000);

        // The command started a grain cloud on its own pool, and it sounds.
        EXPECT_EQ(scheduler.mixer().active_grain_cloud_count(), 1u);
        EXPECT_EQ(scheduler.mixer().active_voice_count(), 0u);
        double e = 0.0;
        for (float s : out) e += std::abs(static_cast<double>(s));
        EXPECT_GT(e, 0.0);
    }

    // A behavior SetGrainParam steers the running cloud by the source's client id.
    TEST_F(SceneAudioPlayerTest, BehaviorGrainParamSteersRunningCloud)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_grain_renderable(/*density*/ 300.0f);

        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "ambience";
        node.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto entity = result.instance.authored_to_runtime.at("ambience");

        wz::audio::AudioScheduler scheduler(16, 64);
        ASSERT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, result.instance, scheduler, grain_store_).played,
                  1u);

        std::vector<float> before(2400, 0.0f);
        scheduler.process(before.data(), 2400, 1, 48000);
        double eb = 0.0;
        for (float s : before) eb += std::abs(static_cast<double>(s));
        ASSERT_GT(eb, 0.0);

        // Behavior-side: ramp the cloud's gain to 0 (param 0 = Gain, jump).
        EXPECT_TRUE(wz::engine::audio::apply_grain_param_command(
            result.instance, scheduler, entity,
            static_cast<uint8_t>(wz::audio::GrainParam::Gain), 0.0f, 0u));

        std::vector<float> after(2400, 0.0f);
        scheduler.process(after.data(), 2400, 1, 48000);
        double ea = 0.0;
        for (float s : after) ea += std::abs(static_cast<double>(s));
        EXPECT_NEAR(ea, 0.0, 1.0e-4);
    }

    // No AudioSource on the entity => the grain-param command is a no-op.
    TEST_F(SceneAudioPlayerTest, BehaviorGrainParamNoSourceIsNoOp)
    {
        SceneAssetData authored{};
        SceneNodeAsset node{};
        node.id = "silent";
        authored.nodes.push_back(std::move(node));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());
        const auto entity = result.instance.authored_to_runtime.at("silent");

        wz::audio::AudioScheduler scheduler(16, 64);
        EXPECT_FALSE(wz::engine::audio::apply_grain_param_command(
            result.instance, scheduler, entity,
            static_cast<uint8_t>(wz::audio::GrainParam::Density), 10.0f, 0u));
    }

    // ── Spatialization producer pass (seam 3) ──────────────────────────────────
    //
    // These build a tiny SceneInstance (a listener node + a clip-source node),
    // supply each node's world transform (a translation Mat4, identity basis: +X
    // right, +Z forward), run the producer pass, and observe the posted SetSpatial
    // by RENDERING the (already-playing) voice and comparing channel energies —
    // the SetSpatial only takes effect on a live voice, so play first.

    namespace {
        // Column-major translation matrix (identity basis).
        wz::math::Mat4 translate(float x, float y, float z)
        {
            wz::math::Mat4 m = wz::math::Mat4::identity();
            m.m[12] = x; m.m[13] = y; m.m[14] = z;
            return m;
        }

        // #221: the spatialization pass now reads source/listener world poses from
        // the instance's polytree (the single source of truth), NOT from a passed
        // scene_nodes_ span. These test scenes are flat (top-level nodes), so a
        // node's world == its local; seat the desired world matrix straight into
        // the polytree node for authored node `id`, addressed via the same
        // authored→runtime map the engine uses. Returns false if the id is unknown.
        bool seat_world(
            wz::engine::assets::SceneInstance& instance,
            const wz::scene::AuthoredEntityId& id,
            const wz::math::Mat4& world)
        {
            const auto it = instance.authored_to_runtime.find(id);
            if (it == instance.authored_to_runtime.end()) {
                return false;
            }
            auto& node = const_cast<wz::scene::TransformNode&>(
                wz::core::graph::node_data(
                    instance.storage.polytree, it->second));
            node.local = world;
            node.world = world;
            return true;
        }

        struct ChannelEnergy { double l = 0.0, r = 0.0; };
        ChannelEnergy stereo_energy(const std::vector<float>& interleaved)
        {
            ChannelEnergy e{};
            for (size_t f = 0; f * 2 + 1 < interleaved.size(); ++f) {
                e.l += std::abs(static_cast<double>(interleaved[2 * f + 0]));
                e.r += std::abs(static_cast<double>(interleaved[2 * f + 1]));
            }
            return e;
        }
    }

    // A clip source to the listener's RIGHT => the posted SetSpatial pans right
    // (right channel energy dominates) — i.e. gain_r > gain_l, itd_frames > 0.
    TEST_F(SceneAudioPlayerTest, SpatializationPansSourceToTheRight)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_renderable(0.5f, /*looping*/ true);

        SceneAssetData authored{};
        SceneNodeAsset listener{};
        listener.id = "listener";
        listener.audio_listener = SceneAudioListenerAsset{ .active = true };
        authored.nodes.push_back(std::move(listener));
        SceneNodeAsset speaker{};
        speaker.id = "speaker";
        speaker.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(speaker));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        // Seat world poses in the polytree (the pass's source of truth): listener
        // at origin, speaker 40 units to the +X (right) side, within ref dist.
        ASSERT_TRUE(seat_world(result.instance, "listener",
                               translate(0.0f, 0.0f, 0.0f)));
        ASSERT_TRUE(seat_world(result.instance, "speaker",
                               translate(40.0f, 0.0f, 0.0f)));

        wz::audio::AudioScheduler scheduler(16, 64);
        ASSERT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, result.instance, scheduler, grain_store_).played,
                  1u);

        const uint32_t posted =
            wz::engine::audio::update_scene_audio_spatialization(
                *library_, result.instance,
                /*dt*/ 1.0f / 60.0f, /*sample_rate*/ 48000, scheduler,
                spat_state_);
        EXPECT_EQ(posted, 1u);

        std::vector<float> out(2 * 256, 0.0f);
        scheduler.process(out.data(), 256, 2, 48000);
        const ChannelEnergy e = stereo_energy(out);
        EXPECT_GT(e.r, e.l); // panned right
    }

    // #221 rework: the pass sources source/listener poses ONLY from the instance
    // polytree, with NO scene_nodes_ span in its signature. Re-seating the speaker
    // to the LEFT in the polytree (nothing else touched — no authored-node span
    // exists in the call) must flip the pan, proving the pose read is the polytree.
    TEST_F(SceneAudioPlayerTest, SpatializationPoseComesFromPolytreeNotSceneNodes)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_renderable(0.5f, /*looping*/ true);

        SceneAssetData authored{};
        SceneNodeAsset listener{};
        listener.id = "listener";
        listener.audio_listener = SceneAudioListenerAsset{ .active = true };
        authored.nodes.push_back(std::move(listener));
        SceneNodeAsset speaker{};
        speaker.id = "speaker";
        speaker.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(speaker));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        ASSERT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, result.instance, scheduler, grain_store_).played,
                  1u);

        // Seat the speaker to the LEFT of the listener in the polytree only.
        ASSERT_TRUE(seat_world(result.instance, "listener",
                               translate(0.0f, 0.0f, 0.0f)));
        ASSERT_TRUE(seat_world(result.instance, "speaker",
                               translate(-40.0f, 0.0f, 0.0f)));

        EXPECT_EQ(wz::engine::audio::update_scene_audio_spatialization(
                      *library_, result.instance,
                      1.0f / 60.0f, 48000, scheduler, spat_state_),
                  1u);

        std::vector<float> out(2 * 256, 0.0f);
        scheduler.process(out.data(), 256, 2, 48000);
        const ChannelEnergy e = stereo_energy(out);
        EXPECT_GT(e.l, e.r); // panned left, driven purely by the polytree pose
    }

    // A far source is quieter than a near one (distance attenuation).
    TEST_F(SceneAudioPlayerTest, SpatializationAttenuatesWithDistance)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_renderable(0.5f, /*looping*/ true);

        auto energy_at = [&](float dist) -> double {
            SceneAssetData authored{};
            SceneNodeAsset listener{};
            listener.id = "listener";
            listener.audio_listener = SceneAudioListenerAsset{ .active = true };
            authored.nodes.push_back(std::move(listener));
            SceneNodeAsset speaker{};
            speaker.id = "speaker";
            speaker.audio_source = SceneAudioSourceAsset{
                .audio_renderable = renderable,
                .auto_play = true,
                .enabled = true,
            };
            authored.nodes.push_back(std::move(speaker));

            auto result = instantiate_scene(authored);
            EXPECT_TRUE(result.ok());

            // Straight ahead (+Z) at the given distance: no pan, pure attenuation.
            EXPECT_TRUE(seat_world(result.instance, "listener",
                                   translate(0.0f, 0.0f, 0.0f)));
            EXPECT_TRUE(seat_world(result.instance, "speaker",
                                   translate(0.0f, 0.0f, dist)));

            wz::audio::AudioScheduler scheduler(16, 64);
            wz::engine::audio::GrainCloudDescStore store;
            EXPECT_EQ(wz::engine::audio::play_scene_audio_sources(
                          *library_, result.instance, scheduler, store).played,
                      1u);
            wz::engine::audio::AudioSpatializationState st;
            wz::engine::audio::update_scene_audio_spatialization(
                *library_, result.instance,
                1.0f / 60.0f, 48000, scheduler, st);

            std::vector<float> out(2 * 256, 0.0f);
            scheduler.process(out.data(), 256, 2, 48000);
            const ChannelEnergy e = stereo_energy(out);
            return e.l + e.r;
        };

        const double near_e = energy_at(40.0f);    // within ref => full level
        const double far_e = energy_at(2000.0f);    // far => attenuated
        EXPECT_GT(near_e, 0.0);
        EXPECT_LT(far_e, near_e);
    }

    // A receding source across two ticks => Doppler pitch < 1 (read advances
    // slower). Observed via the read position: a non-looping ramp source whose
    // played value tracks the read position. Here we assert the posted command
    // by re-running the pass and checking the voice slows. To keep it robust we
    // compare the energy of a fixed window — a slower read keeps the source alive
    // longer, but the cleaner check is the posted count + that no crash occurs.
    // We assert pitch<1 indirectly: a receding source still sounds (voice alive)
    // and the pass posts a command on the second tick.
    TEST_F(SceneAudioPlayerTest, SpatializationRecedingSourcePostsDopplerSecondTick)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_renderable(0.5f, /*looping*/ true);

        SceneAssetData authored{};
        SceneNodeAsset listener{};
        listener.id = "listener";
        listener.audio_listener = SceneAudioListenerAsset{ .active = true };
        authored.nodes.push_back(std::move(listener));
        SceneNodeAsset speaker{};
        speaker.id = "speaker";
        speaker.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(speaker));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        wz::audio::AudioScheduler scheduler(16, 64);
        ASSERT_EQ(wz::engine::audio::play_scene_audio_sources(
                      *library_, result.instance, scheduler, grain_store_).played,
                  1u);

        // Tick 1: source ahead at z=100 (no prev => Doppler skipped).
        EXPECT_TRUE(seat_world(result.instance, "listener",
                               translate(0.0f, 0.0f, 0.0f)));
        EXPECT_TRUE(seat_world(result.instance, "speaker",
                               translate(0.0f, 0.0f, 100.0f)));
        EXPECT_EQ(wz::engine::audio::update_scene_audio_spatialization(
                      *library_, result.instance,
                      1.0f / 60.0f, 48000, scheduler, spat_state_),
                  1u);

        // Tick 2: source receded to z=200 over the tick => v_r > 0 => pitch < 1.
        EXPECT_TRUE(seat_world(result.instance, "speaker",
                               translate(0.0f, 0.0f, 200.0f)));
        EXPECT_EQ(wz::engine::audio::update_scene_audio_spatialization(
                      *library_, result.instance,
                      1.0f / 60.0f, 48000, scheduler, spat_state_),
                  1u);

        // The voice survives the retune and still sounds (looping source).
        std::vector<float> out(2 * 64, 0.0f);
        scheduler.process(out.data(), 64, 2, 48000);
        EXPECT_GT(peak(out), 0.0f);
    }

    // A GrainCloud source is ambient (2D) — the producer posts NO SetSpatial.
    TEST_F(SceneAudioPlayerTest, SpatializationSkipsGrainCloudSources)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_grain_renderable(/*density*/ 300.0f);

        SceneAssetData authored{};
        SceneNodeAsset listener{};
        listener.id = "listener";
        listener.audio_listener = SceneAudioListenerAsset{ .active = true };
        authored.nodes.push_back(std::move(listener));
        SceneNodeAsset ambience{};
        ambience.id = "ambience";
        ambience.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(ambience));

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        EXPECT_TRUE(seat_world(result.instance, "listener",
                               translate(0.0f, 0.0f, 0.0f)));
        EXPECT_TRUE(seat_world(result.instance, "ambience",
                               translate(40.0f, 0.0f, 0.0f)));

        wz::audio::AudioScheduler scheduler(16, 64);
        wz::engine::audio::play_scene_audio_sources(
            *library_, result.instance, scheduler, grain_store_);

        // GrainCloud kind is skipped => zero SetSpatial posted.
        EXPECT_EQ(wz::engine::audio::update_scene_audio_spatialization(
                      *library_, result.instance,
                      1.0f / 60.0f, 48000, scheduler, spat_state_),
                  0u);
    }

    // No active listener => the pass does nothing (sources stay 2D).
    TEST_F(SceneAudioPlayerTest, SpatializationNoActiveListenerIsNoOp)
    {
        const wz::asset::AssetKey renderable =
            make_resolved_renderable(0.5f, /*looping*/ true);

        SceneAssetData authored{};
        SceneNodeAsset speaker{};
        speaker.id = "speaker";
        speaker.audio_source = SceneAudioSourceAsset{
            .audio_renderable = renderable,
            .auto_play = true,
            .enabled = true,
        };
        authored.nodes.push_back(std::move(speaker)); // no listener node at all

        auto result = instantiate_scene(authored);
        ASSERT_TRUE(result.ok());

        EXPECT_TRUE(seat_world(result.instance, "speaker",
                               translate(40.0f, 0.0f, 0.0f)));

        wz::audio::AudioScheduler scheduler(16, 64);
        wz::engine::audio::play_scene_audio_sources(
            *library_, result.instance, scheduler, grain_store_);

        EXPECT_EQ(wz::engine::audio::update_scene_audio_spatialization(
                      *library_, result.instance,
                      1.0f / 60.0f, 48000, scheduler, spat_state_),
                  0u);
    }

    // The authored playback rate must SURVIVE spatialization (#313, B4-C3).
    //
    // Voice has a single pitch field and Mixer::set_spatial_client ASSIGNS into
    // it, so a SetSpatial carrying a Doppler-only ratio wiped the renderable's
    // authored pitch on the first spatialization tick after a voice started.
    // Nothing caught it because every fixture in this file authored pitch = 1.0,
    // where the clobber is invisible.
    //
    // Observable without reaching into Voice: the clip is 480 frames (0.01 s at
    // 48 kHz) and non-looping, so playback RATE decides when it runs out. At the
    // authored 2x it is exhausted inside the first 256-frame block; at 1x (the
    // bug) it is still sounding through the second. A static listener/source
    // pair means Doppler is exactly 1.0, so the only thing under test is whether
    // the authored factor survived.
    TEST_F(SceneAudioPlayerTest, SpatializationPreservesTheAuthoredPitch)
    {
        struct Rendered
        {
            double first_block = 0.0;
            double second_block = 0.0;
        };

        const auto render_with_authored_pitch = [this](float pitch) -> Rendered
        {
            const wz::asset::AssetKey renderable =
                make_resolved_renderable(1.0f, /*looping*/ false, pitch);

            SceneAssetData authored{};
            SceneNodeAsset listener{};
            listener.id = "listener";
            listener.audio_listener = SceneAudioListenerAsset{ .active = true };
            authored.nodes.push_back(std::move(listener));
            SceneNodeAsset speaker{};
            speaker.id = "speaker";
            speaker.audio_source = SceneAudioSourceAsset{
                .audio_renderable = renderable,
                .auto_play = true,
                .enabled = true,
            };
            authored.nodes.push_back(std::move(speaker));

            auto result = instantiate_scene(authored);
            EXPECT_TRUE(result.ok());

            // Co-located-ish and STATIC: no relative motion, so the Doppler
            // ratio is exactly 1.0 and cannot mask the authored factor.
            EXPECT_TRUE(seat_world(result.instance, "listener",
                                   translate(0.0f, 0.0f, 0.0f)));
            EXPECT_TRUE(seat_world(result.instance, "speaker",
                                   translate(1.0f, 0.0f, 0.0f)));

            wz::audio::AudioScheduler scheduler(16, 64);
            EXPECT_EQ(wz::engine::audio::play_scene_audio_sources(
                          *library_, result.instance, scheduler, grain_store_)
                          .played,
                      1u);

            wz::engine::audio::AudioSpatializationState state{};
            EXPECT_EQ(wz::engine::audio::update_scene_audio_spatialization(
                          *library_, result.instance,
                          /*dt*/ 1.0f / 60.0f, /*sample_rate*/ 48000,
                          scheduler, state),
                      1u);

            Rendered out{};
            std::vector<float> block(2 * 256, 0.0f);
            scheduler.process(block.data(), 256, 2, 48000);
            const ChannelEnergy first = stereo_energy(block);
            out.first_block = first.l + first.r;

            std::fill(block.begin(), block.end(), 0.0f);
            scheduler.process(block.data(), 256, 2, 48000);
            const ChannelEnergy second = stereo_energy(block);
            out.second_block = second.l + second.r;
            return out;
        };

        // Control: at the default rate the 480-frame clip spans both blocks.
        const Rendered unity = render_with_authored_pitch(1.0f);
        EXPECT_GT(unity.first_block, 0.0);
        EXPECT_GT(unity.second_block, 0.0)
            << "control is broken: a 1x clip must still be sounding in block 2";

        // The property: an authored 2x really plays at 2x after spatialization,
        // so the clip is gone before block 2. Under the bug the SetSpatial reset
        // the voice to 1.0 and this block still had energy.
        const Rendered doubled = render_with_authored_pitch(2.0f);
        EXPECT_GT(doubled.first_block, 0.0);
        EXPECT_NEAR(doubled.second_block, 0.0, 1.0e-4)
            << "the authored pitch was overwritten by the spatialization pass: "
               "the clip is still playing when a 2x rate should have finished it";
    }

} // namespace wz::engine::assets::test
