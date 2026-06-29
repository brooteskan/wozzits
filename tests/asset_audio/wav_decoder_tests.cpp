// tests/asset_audio/wav_decoder_tests.cpp
//
// Unit tests for the pure RIFF/WAVE decoder. These build WAV byte buffers in
// memory and decode them directly — no asset system, file system, or device.

#include <gtest/gtest.h>

#include <engine/assets/audio/wav_decoder.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wz::engine::assets::test {

    namespace {

        void put_u16(std::vector<uint8_t>& b, uint16_t v)
        {
            b.push_back(static_cast<uint8_t>(v & 0xFF));
            b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        }

        void put_u32(std::vector<uint8_t>& b, uint32_t v)
        {
            b.push_back(static_cast<uint8_t>(v & 0xFF));
            b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
            b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        }

        void put_tag(std::vector<uint8_t>& b, const char* t)
        {
            for (int i = 0; i < 4; ++i)
                b.push_back(static_cast<uint8_t>(t[i]));
        }

        // Build a WAV from interleaved float samples. format_tag is 1 (PCM int) or
        // 3 (IEEE float). For PCM, bits is 16; for float, bits is 32.
        std::vector<uint8_t> build_wav(
            const std::vector<float>& interleaved,
            uint16_t channels,
            uint32_t sample_rate,
            uint16_t format_tag,
            uint16_t bits)
        {
            const uint16_t bytes_per_sample = bits / 8u;
            const uint32_t data_size =
                static_cast<uint32_t>(interleaved.size()) * bytes_per_sample;

            std::vector<uint8_t> body;
            for (float f : interleaved) {
                if (format_tag == 3) {
                    uint32_t bitsv = 0;
                    std::memcpy(&bitsv, &f, sizeof(float));
                    put_u32(body, bitsv);
                }
                else { // PCM16
                    int32_t s = static_cast<int32_t>(std::lround(f * 32767.0f));
                    if (s > 32767) s = 32767;
                    if (s < -32768) s = -32768;
                    put_u16(body, static_cast<uint16_t>(static_cast<int16_t>(s)));
                }
            }

            std::vector<uint8_t> wav;
            put_tag(wav, "RIFF");
            put_u32(wav, 36u + data_size);
            put_tag(wav, "WAVE");
            put_tag(wav, "fmt ");
            put_u32(wav, 16u);
            put_u16(wav, format_tag);
            put_u16(wav, channels);
            put_u32(wav, sample_rate);
            put_u32(wav, sample_rate * channels * bytes_per_sample); // byte rate
            put_u16(wav, static_cast<uint16_t>(channels * bytes_per_sample));
            put_u16(wav, bits);
            put_tag(wav, "data");
            put_u32(wav, data_size);
            wav.insert(wav.end(), body.begin(), body.end());
            return wav;
        }

    } // namespace


    TEST(WavDecoderTests, DecodesPcm16MonoHeader)
    {
        const std::vector<float> samples = { 0.0f, 0.5f, -0.5f, 1.0f };
        const auto wav = build_wav(samples, 1, 44100, 1, 16);

        AudioClipData clip;
        std::string error;
        ASSERT_TRUE(decode_wav(wav, clip, error)) << error;

        EXPECT_EQ(clip.sample_rate, 44100u);
        EXPECT_EQ(clip.channels, 1u);
        EXPECT_EQ(clip.frame_count, 4u);
        EXPECT_EQ(clip.source, AudioClipSource::Wav);
        EXPECT_TRUE(clip.valid());
    }

    TEST(WavDecoderTests, DecodesPcm16ValuesNearOriginal)
    {
        const std::vector<float> samples = { 0.0f, 0.5f, -0.5f, 1.0f };
        const auto wav = build_wav(samples, 1, 48000, 1, 16);

        AudioClipData clip;
        std::string error;
        ASSERT_TRUE(decode_wav(wav, clip, error)) << error;
        ASSERT_EQ(clip.samples.size(), samples.size());

        // 16-bit quantisation error is well under 1/1000.
        for (size_t i = 0; i < samples.size(); ++i)
            EXPECT_NEAR(clip.samples[i], samples[i], 1.0f / 1000.0f);
    }

    TEST(WavDecoderTests, DecodesFloat32Exactly)
    {
        const std::vector<float> samples = { 0.0f, 0.25f, -0.75f, 0.123456f };
        const auto wav = build_wav(samples, 1, 48000, 3, 32);

        AudioClipData clip;
        std::string error;
        ASSERT_TRUE(decode_wav(wav, clip, error)) << error;
        ASSERT_EQ(clip.samples.size(), samples.size());

        for (size_t i = 0; i < samples.size(); ++i)
            EXPECT_FLOAT_EQ(clip.samples[i], samples[i]);
    }

    TEST(WavDecoderTests, DecodesStereoInterleaving)
    {
        // Two frames of stereo: L0,R0, L1,R1.
        const std::vector<float> samples = { 0.1f, -0.1f, 0.2f, -0.2f };
        const auto wav = build_wav(samples, 2, 48000, 3, 32);

        AudioClipData clip;
        std::string error;
        ASSERT_TRUE(decode_wav(wav, clip, error)) << error;

        ASSERT_EQ(clip.channels, 2u);
        ASSERT_EQ(clip.frame_count, 2u);
        EXPECT_FLOAT_EQ(clip.at(0, 0), 0.1f);
        EXPECT_FLOAT_EQ(clip.at(0, 1), -0.1f);
        EXPECT_FLOAT_EQ(clip.at(1, 0), 0.2f);
        EXPECT_FLOAT_EQ(clip.at(1, 1), -0.2f);
    }

    TEST(WavDecoderTests, RejectsNonRiff)
    {
        std::vector<uint8_t> junk = { 'N', 'O', 'P', 'E', 0, 0, 0, 0,
                                      'W', 'A', 'V', 'E' };
        AudioClipData clip;
        std::string error;
        EXPECT_FALSE(decode_wav(junk, clip, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(WavDecoderTests, RejectsTooSmall)
    {
        std::vector<uint8_t> tiny = { 'R', 'I', 'F', 'F' };
        AudioClipData clip;
        std::string error;
        EXPECT_FALSE(decode_wav(tiny, clip, error));
    }

    TEST(WavDecoderTests, RejectsUnsupportedFormatTag)
    {
        // Build a valid container but with an unsupported (ADPCM=2) format tag.
        auto wav = build_wav({ 0.0f, 0.0f }, 1, 48000, 1, 16);
        // Patch the format tag (offset 20: after RIFF(4)+size(4)+WAVE(4)+
        // "fmt "(4)+size(4) == 20).
        wav[20] = 2;
        wav[21] = 0;

        AudioClipData clip;
        std::string error;
        EXPECT_FALSE(decode_wav(wav, clip, error));
    }

    TEST(WavDecoderTests, SkipsUnknownChunkBeforeData)
    {
        // Hand-build a WAV with a LIST chunk between fmt and data.
        std::vector<uint8_t> wav;
        put_tag(wav, "RIFF");
        const uint32_t data_size = 4u; // two int16 samples
        // RIFF size: WAVE(4) + fmt hdr(8)+16 + LIST(8)+6(+pad) + data(8)+4
        // Compute precisely below after assembling; patch later.
        const size_t riff_size_pos = wav.size();
        put_u32(wav, 0);
        put_tag(wav, "WAVE");

        put_tag(wav, "fmt ");
        put_u32(wav, 16u);
        put_u16(wav, 1);              // PCM
        put_u16(wav, 1);              // mono
        put_u32(wav, 48000u);
        put_u32(wav, 48000u * 2u);    // byte rate
        put_u16(wav, 2);              // block align
        put_u16(wav, 16);             // bits

        put_tag(wav, "LIST");
        put_u32(wav, 5u);             // odd size -> one pad byte
        for (int i = 0; i < 5; ++i) wav.push_back(static_cast<uint8_t>('x'));
        wav.push_back(0);             // pad to even

        put_tag(wav, "data");
        put_u32(wav, data_size);
        put_u16(wav, 0x4000);         // ~0.5
        put_u16(wav, 0xC000);         // ~-0.5

        const uint32_t riff_size =
            static_cast<uint32_t>(wav.size() - (riff_size_pos + 4));
        wav[riff_size_pos + 0] = static_cast<uint8_t>(riff_size & 0xFF);
        wav[riff_size_pos + 1] = static_cast<uint8_t>((riff_size >> 8) & 0xFF);
        wav[riff_size_pos + 2] = static_cast<uint8_t>((riff_size >> 16) & 0xFF);
        wav[riff_size_pos + 3] = static_cast<uint8_t>((riff_size >> 24) & 0xFF);

        AudioClipData clip;
        std::string error;
        ASSERT_TRUE(decode_wav(wav, clip, error)) << error;
        EXPECT_EQ(clip.frame_count, 2u);
        EXPECT_EQ(clip.channels, 1u);
    }

} // namespace wz::engine::assets::test
