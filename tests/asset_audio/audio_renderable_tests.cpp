// tests/asset_audio/audio_renderable_tests.cpp
//
// Tests for the audio renderable asset (audio-track item 5): the terminal of an
// audio chain compiles a source clip + params into an executable descriptor, and
// the runtime mixer executes that descriptor.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/audio/audio_clip_bank.h>
#include <engine/assets/audio/audio_renderable.h>

#include <audio/mixer.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace wz::engine::assets::test {

    namespace stdfs = std::filesystem;

    class AudioRenderableTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (stdfs::temp_directory_path() / "wz_audio_renderable_tests")
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

        // Make a resolved tone clip for use as a renderable source.
        AudioClipAsset make_tone(const char* name,
                                 float amplitude,
                                 float duration)
        {
            return library_->audio_clips().create_procedural_tone_audio_clip({
                .name = name,
                .sample_rate = 48000,
                .channels = 1,
                .waveform = AudioToneWaveform::Sine,
                .frequency = 1000.0f,
                .duration_seconds = duration,
                .amplitude = amplitude,
                });
        }

        wz::gpu::Device   null_device_{};
        wz::Logger        logger_{};
        wz::fs::Path      temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };


    TEST_F(AudioRenderableTest, CompilesClipHandleAndParams)
    {
        const AudioClipAsset clip = make_tone("debug/src", 0.5f, 0.01f);
        ASSERT_TRUE(clip.valid());

        const AudioRenderableAsset rend =
            library_->audio_renderables().create_audio_renderable({
                .clip = clip,
                .gain = 0.5f,
                .pitch = 2.0f,
                .looping = true,
                });
        ASSERT_TRUE(rend.valid());

        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioRenderableHandle h =
            library_->audio_renderables().get_audio_renderable(rend);
        ASSERT_TRUE(h.valid());

        const AudioRenderableData* d =
            library_->audio_renderables().get_audio_renderable_data(h);
        ASSERT_NE(d, nullptr);

        // Single-clip path now yields a one-element clip vector.
        ASSERT_EQ(d->clips.size(), 1u);
        EXPECT_EQ(d->default_index, 0u);
        EXPECT_TRUE(d->clips[0].valid());
        EXPECT_TRUE(d->valid());
        EXPECT_FLOAT_EQ(d->gain, 0.5f);
        EXPECT_FLOAT_EQ(d->pitch, 2.0f);
        EXPECT_TRUE(d->looping);

        // The stored clip handle resolves to valid clip data in AudioClipTable.
        const AudioClipData* clip_data =
            library_->audio_clips().get_audio_clip_data(
                AudioClipHandle{ d->clips[0] });
        ASSERT_NE(clip_data, nullptr);
        EXPECT_TRUE(clip_data->valid());
    }

    TEST_F(AudioRenderableTest, RuntimeExecutesDescriptorThroughMixer)
    {
        const AudioClipAsset clip = make_tone("debug/exec", 0.5f, 0.005f);
        ASSERT_TRUE(clip.valid());

        const AudioRenderableAsset rend =
            library_->audio_renderables().create_audio_renderable({
                .clip = clip,
                .gain = 0.5f,
                .pitch = 1.0f,
                .looping = false,
                });
        ASSERT_TRUE(rend.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioRenderableData* d =
            library_->audio_renderables().get_audio_renderable_data(
                library_->audio_renderables().get_audio_renderable(rend));
        ASSERT_NE(d, nullptr);

        ASSERT_EQ(d->clips.size(), 1u);
        const AudioClipData* clip_data =
            library_->audio_clips().get_audio_clip_data(
                AudioClipHandle{ d->clips[0] });
        ASSERT_NE(clip_data, nullptr);

        // Execute the descriptor: play the source with the terminal's params.
        wz::audio::Mixer mixer(8);
        mixer.play(clip_data->view(), d->gain, d->pitch, d->looping);

        const uint32_t frames = static_cast<uint32_t>(clip_data->frame_count);
        std::vector<float> out(frames, 0.0f);
        mixer.render(out.data(), frames, 1, 48000);

        // gain 0.5, pitch 1, matching rate: output is the clip scaled by gain.
        for (uint32_t i = 0; i < frames; ++i)
            EXPECT_FLOAT_EQ(out[i], clip_data->samples[i] * 0.5f) << "frame " << i;
    }

    TEST_F(AudioRenderableTest, InvalidClipProducesNoRenderable)
    {
        const AudioRenderableAsset rend =
            library_->audio_renderables().create_audio_renderable({
                .clip = AudioClipAsset{}, // invalid source
                .gain = 1.0f,
                });
        EXPECT_FALSE(rend.valid());
    }


    // ── Bank-backed renderable ─────────────────────────────────────────────────

    // Build a resolved 3-clip bank for the bank-renderable tests.
    static AudioClipBankAsset make_bank(EngineAssetLibrary& lib)
    {
        auto tone = [&lib](const char* name) {
            return lib.audio_clips().create_procedural_tone_audio_clip({
                .name = name,
                .sample_rate = 48000,
                .channels = 1,
                .waveform = AudioToneWaveform::Sine,
                .frequency = 1000.0f,
                .duration_seconds = 0.01f,
                .amplitude = 0.5f,
                });
        };

        AudioClipBankFromClipsDesc desc{};
        desc.items = {
            { "a", tone("bankrend/a") },
            { "b", tone("bankrend/b") },
            { "c", tone("bankrend/c") },
        };
        return lib.audio_clip_banks().create_audio_clip_bank_from_clips(desc);
    }

    TEST_F(AudioRenderableTest, BankRenderableHoldsAllClipsAndHonorsDefaultIndex)
    {
        const AudioClipBankAsset bank = make_bank(*library_);
        ASSERT_TRUE(bank.valid());

        const AudioRenderableAsset rend =
            library_->audio_renderables().create_audio_clip_bank_renderable({
                .bank = bank,
                .default_index = 1,
                .gain = 0.5f,
                .pitch = 1.0f,
                .looping = false,
                });
        ASSERT_TRUE(rend.valid());

        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioRenderableData* d =
            library_->audio_renderables().get_audio_renderable_data(
                library_->audio_renderables().get_audio_renderable(rend));
        ASSERT_NE(d, nullptr);

        // The terminal holds the whole bank (3 clips) with the honored default.
        ASSERT_EQ(d->clips.size(), 3u);
        EXPECT_EQ(d->default_index, 1u);
        EXPECT_TRUE(d->valid());

        // clip_at selects in-range; out-of-range falls back to default_index.
        EXPECT_EQ(d->clip_at(0).id, d->clips[0].id);
        EXPECT_EQ(d->clip_at(2).id, d->clips[2].id);
        EXPECT_EQ(d->clip_at(99).id, d->clips[1].id); // -> default_index 1
    }

    TEST_F(AudioRenderableTest, BankRenderableClampsOutOfRangeDefaultIndex)
    {
        const AudioClipBankAsset bank = make_bank(*library_);
        ASSERT_TRUE(bank.valid());

        const AudioRenderableAsset rend =
            library_->audio_renderables().create_audio_clip_bank_renderable({
                .bank = bank,
                .default_index = 99, // beyond the 3-clip bank
                .gain = 1.0f,
                });
        ASSERT_TRUE(rend.valid());

        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioRenderableData* d =
            library_->audio_renderables().get_audio_renderable_data(
                library_->audio_renderables().get_audio_renderable(rend));
        ASSERT_NE(d, nullptr);

        ASSERT_EQ(d->clips.size(), 3u);
        EXPECT_EQ(d->default_index, 2u); // clamped to clips.size() - 1
        EXPECT_TRUE(d->valid());
    }

    TEST_F(AudioRenderableTest, InvalidBankProducesNoRenderable)
    {
        const AudioRenderableAsset rend =
            library_->audio_renderables().create_audio_clip_bank_renderable({
                .bank = AudioClipBankAsset{}, // invalid source
                .gain = 1.0f,
                });
        EXPECT_FALSE(rend.valid());
    }

} // namespace wz::engine::assets::test
