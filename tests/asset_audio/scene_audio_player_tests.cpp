// tests/asset_audio/scene_audio_player_tests.cpp
//
// End-to-end test for audio-track item 7: a scene AudioSource is auto-played
// through the scheduler by resolving its audio-renderable -> clip -> PCM and
// posting a Play command. Fully headless — the scheduler renders into a buffer
// (no device).

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/audio/scene_audio.h>

#include <audio/audio_scheduler.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <cmath>
#include <filesystem>
#include <memory>
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
            *library_, result.instance, scheduler);

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
            *library_, result.instance, scheduler);

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
            *library_, result.instance, scheduler);

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
            *library_, result.instance, scheduler);

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
                      *library_, result.instance, scheduler).played, 0u);
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
                      *library_, result.instance, scheduler).played, 1u);

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

} // namespace wz::engine::assets::test
