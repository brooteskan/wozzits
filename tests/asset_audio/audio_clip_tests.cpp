// tests/asset_audio/audio_clip_tests.cpp
//
// Asset-system tests for the audio clip family: key identity, procedural tone
// generation (file-free), WAV file-backed import, and validation/rejection.
// Mirrors the structure of scalar_field_procedural_tests.cpp.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/key_factories/audio_clip.h>

#include <audio/mixer.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace wz::engine::assets::test {

    namespace stdfs = std::filesystem;

    namespace {

        // Minimal float32 mono WAV builder for the file-backed import test.
        std::vector<uint8_t> build_float_wav_mono(
            const std::vector<float>& samples, uint32_t sample_rate)
        {
            auto put_u16 = [](std::vector<uint8_t>& b, uint16_t v) {
                b.push_back(static_cast<uint8_t>(v & 0xFF));
                b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            };
            auto put_u32 = [](std::vector<uint8_t>& b, uint32_t v) {
                b.push_back(static_cast<uint8_t>(v & 0xFF));
                b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
                b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
            };
            auto put_tag = [](std::vector<uint8_t>& b, const char* t) {
                for (int i = 0; i < 4; ++i)
                    b.push_back(static_cast<uint8_t>(t[i]));
            };

            const uint32_t data_size =
                static_cast<uint32_t>(samples.size()) * 4u;

            std::vector<uint8_t> wav;
            put_tag(wav, "RIFF");
            put_u32(wav, 36u + data_size);
            put_tag(wav, "WAVE");
            put_tag(wav, "fmt ");
            put_u32(wav, 16u);
            put_u16(wav, 3);                       // IEEE float
            put_u16(wav, 1);                       // mono
            put_u32(wav, sample_rate);
            put_u32(wav, sample_rate * 4u);        // byte rate
            put_u16(wav, 4);                       // block align
            put_u16(wav, 32);                      // bits
            put_tag(wav, "data");
            put_u32(wav, data_size);
            for (float f : samples) {
                uint32_t bits = 0;
                std::memcpy(&bits, &f, sizeof(float));
                put_u32(wav, bits);
            }
            return wav;
        }

    } // namespace


    // ─── Test fixture ─────────────────────────────────────────────────────────────

    class AudioClipTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (stdfs::temp_directory_path() / "wz_audio_clip_tests").string()
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

        wz::gpu::Device   null_device_{};
        wz::Logger        logger_{};
        wz::fs::Path      temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };


    // ─── Key identity tests ────────────────────────────────────────────────────────

    TEST(AudioClipKeyTests, WaveformContributesToToneIdentity)
    {
        const auto sine = make_procedural_tone_audio_clip_key(
            "debug/tone", 48000, 1,
            static_cast<uint8_t>(AudioToneWaveform::Sine),
            440.0f, 1.0f, 0.5f);
        const auto saw = make_procedural_tone_audio_clip_key(
            "debug/tone", 48000, 1,
            static_cast<uint8_t>(AudioToneWaveform::Saw),
            440.0f, 1.0f, 0.5f);
        ASSERT_NE(sine, saw);
    }

    TEST(AudioClipKeyTests, FrequencyContributesToToneIdentity)
    {
        const auto a440 = make_procedural_tone_audio_clip_key(
            "debug/tone", 48000, 1,
            static_cast<uint8_t>(AudioToneWaveform::Sine),
            440.0f, 1.0f, 0.5f);
        const auto a880 = make_procedural_tone_audio_clip_key(
            "debug/tone", 48000, 1,
            static_cast<uint8_t>(AudioToneWaveform::Sine),
            880.0f, 1.0f, 0.5f);
        ASSERT_NE(a440, a880);
    }

    TEST(AudioClipKeyTests, NameContributesToToneIdentity)
    {
        const auto a = make_procedural_tone_audio_clip_key(
            "debug/a", 48000, 1,
            static_cast<uint8_t>(AudioToneWaveform::Sine),
            440.0f, 1.0f, 0.5f);
        const auto b = make_procedural_tone_audio_clip_key(
            "debug/b", 48000, 1,
            static_cast<uint8_t>(AudioToneWaveform::Sine),
            440.0f, 1.0f, 0.5f);
        ASSERT_NE(a, b);
    }


    // ─── Procedural tone generation ────────────────────────────────────────────────

    TEST_F(AudioClipTest, CreateProceduralToneReturnsValidAssetKey)
    {
        const AudioClipAsset asset =
            library_->audio_clips().create_procedural_tone_audio_clip({
                .name = "debug/a440",
                .frequency = 440.0f,
                .duration_seconds = 0.1f,
                });
        EXPECT_TRUE(asset.valid());
    }

    TEST_F(AudioClipTest, ProceduralToneResolvesToValidHandleAndData)
    {
        const AudioClipAsset asset =
            library_->audio_clips().create_procedural_tone_audio_clip({
                .name = "debug/a440",
                .sample_rate = 48000,
                .channels = 1,
                .waveform = AudioToneWaveform::Sine,
                .frequency = 440.0f,
                .duration_seconds = 0.5f,
                .amplitude = 0.5f,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioClipHandle handle =
            library_->audio_clips().get_audio_clip(asset);
        ASSERT_TRUE(handle.valid());

        const AudioClipData* data =
            library_->audio_clips().get_audio_clip_data(handle);
        ASSERT_NE(data, nullptr);

        EXPECT_EQ(data->sample_rate, 48000u);
        EXPECT_EQ(data->channels, 1u);
        EXPECT_EQ(data->frame_count, 24000u); // 0.5s * 48000
        EXPECT_EQ(data->source, AudioClipSource::ProceduralTone);
        EXPECT_TRUE(data->valid());
    }

    TEST_F(AudioClipTest, ProceduralSineStartsNearZeroAndStaysInRange)
    {
        const AudioClipAsset asset =
            library_->audio_clips().create_procedural_tone_audio_clip({
                .name = "debug/sine_range",
                .sample_rate = 48000,
                .channels = 1,
                .waveform = AudioToneWaveform::Sine,
                .frequency = 1000.0f,
                .duration_seconds = 0.05f,
                .amplitude = 0.8f,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioClipHandle handle =
            library_->audio_clips().get_audio_clip(asset);
        ASSERT_TRUE(handle.valid());
        const AudioClipData* data =
            library_->audio_clips().get_audio_clip_data(handle);
        ASSERT_NE(data, nullptr);

        // Sine at phase 0 is 0.
        EXPECT_NEAR(data->at(0, 0), 0.0f, 1e-5f);

        // Amplitude bound holds across the whole clip.
        for (uint64_t f = 0; f < data->frame_count; ++f)
            EXPECT_LE(std::fabs(data->at(f, 0)), 0.8f + 1e-4f);
    }

    TEST_F(AudioClipTest, ProceduralToneFillsAllChannelsIdentically)
    {
        const AudioClipAsset asset =
            library_->audio_clips().create_procedural_tone_audio_clip({
                .name = "debug/stereo_tone",
                .sample_rate = 48000,
                .channels = 2,
                .waveform = AudioToneWaveform::Saw,
                .frequency = 220.0f,
                .duration_seconds = 0.02f,
                .amplitude = 0.5f,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioClipHandle handle =
            library_->audio_clips().get_audio_clip(asset);
        ASSERT_TRUE(handle.valid());
        const AudioClipData* data =
            library_->audio_clips().get_audio_clip_data(handle);
        ASSERT_NE(data, nullptr);
        ASSERT_EQ(data->channels, 2u);

        for (uint64_t f = 0; f < data->frame_count; ++f)
            EXPECT_FLOAT_EQ(data->at(f, 0), data->at(f, 1));
    }


    // ─── WAV file-backed import ────────────────────────────────────────────────────

    TEST_F(AudioClipTest, WavFileImportResolvesToExpectedData)
    {
        const std::vector<float> samples = { 0.0f, 0.25f, -0.25f, 0.5f, -0.5f };
        const auto wav = build_float_wav_mono(samples, 44100);

        const wz::fs::Path wav_path =
            wz::fs::Path{ (stdfs::path(std::string(temp_dir_))
                / "clip.wav").string() };
        {
            std::ofstream out(std::string(wav_path), std::ios::binary);
            out.write(reinterpret_cast<const char*>(wav.data()),
                static_cast<std::streamsize>(wav.size()));
        }

        const AudioClipAsset asset =
            library_->audio_clips().create_audio_clip_from_wav({
                .name = "debug/clip",
                .path = wz::fs::Path{ "clip.wav" }, // relative to resource_root
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioClipHandle handle =
            library_->audio_clips().get_audio_clip(asset);
        ASSERT_TRUE(handle.valid());
        const AudioClipData* data =
            library_->audio_clips().get_audio_clip_data(handle);
        ASSERT_NE(data, nullptr);

        EXPECT_EQ(data->sample_rate, 44100u);
        EXPECT_EQ(data->channels, 1u);
        EXPECT_EQ(data->frame_count, samples.size());
        EXPECT_EQ(data->source, AudioClipSource::Wav);
        ASSERT_EQ(data->samples.size(), samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            EXPECT_FLOAT_EQ(data->samples[i], samples[i]);
    }


    // ─── End-to-end: clip -> view -> mixer ──────────────────────────────────────────
    //
    // Proves item 1 (AudioClipData) and item 3 (Voice/Mixer) connect through the
    // AudioClipData::view() adapter: a resolved clip played at matching rate and
    // unity gain/pitch reproduces its own samples at the mixer output.

    TEST_F(AudioClipTest, ResolvedClipPlaysThroughMixer)
    {
        const AudioClipAsset asset =
            library_->audio_clips().create_procedural_tone_audio_clip({
                .name = "debug/bridge_tone",
                .sample_rate = 48000,
                .channels = 1,
                .waveform = AudioToneWaveform::Sine,
                .frequency = 1000.0f,
                .duration_seconds = 0.01f, // 480 frames
                .amplitude = 0.5f,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioClipHandle handle =
            library_->audio_clips().get_audio_clip(asset);
        ASSERT_TRUE(handle.valid());
        const AudioClipData* data =
            library_->audio_clips().get_audio_clip_data(handle);
        ASSERT_NE(data, nullptr);

        wz::audio::Mixer mixer(8);
        const wz::audio::VoiceHandle vh =
            mixer.play(data->view(), /*gain*/ 1.0f, /*pitch*/ 1.0f, false);
        ASSERT_TRUE(vh.valid());

        const uint32_t frames = static_cast<uint32_t>(data->frame_count);
        // Render one extra frame so the voice observes the end and deactivates.
        std::vector<float> out(frames + 1, 0.0f);
        mixer.render(out.data(), frames + 1, /*channels*/ 1, /*rate*/ 48000);

        // Unity gain/pitch, matching rate, amplitude 0.5 < limiter ceiling:
        // the output is the clip verbatim.
        for (uint32_t i = 0; i < frames; ++i)
            EXPECT_FLOAT_EQ(out[i], data->samples[i]) << "frame " << i;

        // Past the end is silence, and the exhausted voice has freed its slot.
        EXPECT_FLOAT_EQ(out[frames], 0.0f);
        EXPECT_EQ(mixer.active_voice_count(), 0u);
    }


    // ─── Validation / rejection ────────────────────────────────────────────────────

    TEST_F(AudioClipTest, ToneWithZeroDurationIsRejected)
    {
        const AudioClipAsset asset =
            library_->audio_clips().create_procedural_tone_audio_clip({
                .name = "debug/zero_duration",
                .frequency = 440.0f,
                .duration_seconds = 0.0f, // invalid
                });

        ASSERT_TRUE(asset.valid()); // registration succeeds; compile validates
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioClipHandle handle =
            library_->audio_clips().get_audio_clip(asset);
        EXPECT_FALSE(handle.valid());
    }

    TEST_F(AudioClipTest, MissingWavFileIsRejected)
    {
        const AudioClipAsset asset =
            library_->audio_clips().create_audio_clip_from_wav({
                .name = "debug/missing",
                .path = wz::fs::Path{ "does_not_exist.wav" },
                });

        // Registration may succeed (the file node is path-addressed), but resolve
        // must not produce a valid clip handle.
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const AudioClipHandle handle =
            library_->audio_clips().get_audio_clip(asset);
        EXPECT_FALSE(handle.valid());
    }

} // namespace wz::engine::assets::test
