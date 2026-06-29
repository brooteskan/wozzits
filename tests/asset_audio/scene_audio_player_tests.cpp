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
#include <logging/logger.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>
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
                                                     bool looping = false)
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
                    .pitch = 1.0f,
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

} // namespace wz::engine::assets::test
