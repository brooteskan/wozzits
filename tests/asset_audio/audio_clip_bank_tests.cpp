// tests/asset_audio/audio_clip_bank_tests.cpp
//
// Tests for the audio clip bank asset: a runtime table of N decoded clips
// referenced by name/index, built either from an explicit list of clips or from
// a directory of WAVs. Covers the AudioClipBankTable handle semantics, the
// from-clips build (resolved clip handles + name hashes + name lookup), and the
// from-directory enumeration (deterministic lexicographic ordering).

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/audio/audio_clip.h>
#include <engine/assets/audio/audio_clip_bank.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace wz::engine::assets::test {

    namespace stdfs = std::filesystem;

    namespace {

        // FNV-1a/32 over a name, matching the bank module's per-entry hash.
        uint32_t fnv1a_32(std::string_view s) noexcept
        {
            uint32_t h = 2166136261u;
            for (const unsigned char c : s) {
                h ^= static_cast<uint32_t>(c);
                h *= 16777619u;
            }
            return h;
        }

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

        // A minimal valid IEEE-float mono WAV with a single non-zero frame.
        std::vector<uint8_t> tiny_wav(uint32_t sample_rate = 48000)
        {
            const std::vector<float> samples = { 0.25f, -0.5f, 0.75f };

            std::vector<uint8_t> body;
            for (float f : samples) {
                uint32_t bits = 0;
                std::memcpy(&bits, &f, sizeof(float));
                put_u32(body, bits);
            }

            const uint32_t data_size = static_cast<uint32_t>(body.size());

            std::vector<uint8_t> wav;
            put_tag(wav, "RIFF");
            put_u32(wav, 36u + data_size);
            put_tag(wav, "WAVE");
            put_tag(wav, "fmt ");
            put_u32(wav, 16u);
            put_u16(wav, 3u);            // IEEE float
            put_u16(wav, 1u);            // mono
            put_u32(wav, sample_rate);
            put_u32(wav, sample_rate * 1u * 4u);
            put_u16(wav, static_cast<uint16_t>(1u * 4u));
            put_u16(wav, 32u);
            put_tag(wav, "data");
            put_u32(wav, data_size);
            wav.insert(wav.end(), body.begin(), body.end());
            return wav;
        }

        void write_file(const stdfs::path& p, const std::vector<uint8_t>& bytes)
        {
            std::ofstream out(p, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }

    } // namespace


    // ─── AudioClipBankTable handle semantics ──────────────────────────────────────

    TEST(AudioClipBankTableTest, SentinelGetReturnsNull)
    {
        AudioClipBankTable table;
        // The default/sentinel handle is invalid and resolves to nullptr.
        EXPECT_EQ(table.get(wz::asset::ResourceHandle{}), nullptr);
    }

    TEST(AudioClipBankTableTest, AddGetRoundTrips)
    {
        AudioClipBankTable table;

        AudioClipBankData bank{};
        bank.entries.push_back(AudioClipBankEntry{
            .name_hash = 42u,
            .clip = wz::asset::ResourceHandle{ .id = 1, .epoch = 1 },
        });
        bank.names.emplace_back("one");

        const wz::asset::ResourceHandle h = table.add(std::move(bank));
        ASSERT_TRUE(h.valid());

        const AudioClipBankData* got = table.get(h);
        ASSERT_NE(got, nullptr);
        ASSERT_EQ(got->size(), 1u);
        EXPECT_EQ(got->entries[0].name_hash, 42u);
        EXPECT_NE(got->find_by_name_hash(42u), nullptr);
        EXPECT_EQ(got->find_by_name_hash(99u), nullptr);
    }

    TEST(AudioClipBankTableTest, DestroyInvalidatesHandles)
    {
        AudioClipBankTable table;

        AudioClipBankData bank{};
        bank.entries.push_back(AudioClipBankEntry{
            .name_hash = 1u,
            .clip = wz::asset::ResourceHandle{ .id = 1, .epoch = 1 },
        });
        bank.names.emplace_back("c");

        const wz::asset::ResourceHandle h = table.add(std::move(bank));
        ASSERT_NE(table.get(h), nullptr);

        table.destroy();
        EXPECT_EQ(table.get(h), nullptr);
    }

    TEST(AudioClipBankDataTest, ValidRequiresNonEmptyAndAllValidHandles)
    {
        AudioClipBankData empty{};
        EXPECT_FALSE(empty.valid());

        AudioClipBankData bad{};
        bad.entries.push_back(AudioClipBankEntry{}); // invalid handle
        EXPECT_FALSE(bad.valid());

        AudioClipBankData good{};
        good.entries.push_back(AudioClipBankEntry{
            .clip = wz::asset::ResourceHandle{ .id = 1, .epoch = 1 },
        });
        EXPECT_TRUE(good.valid());
    }


    // ─── End-to-end bank builds through the asset library ─────────────────────────

    class AudioClipBankTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (stdfs::temp_directory_path() / "wz_audio_clip_bank_tests")
                    .string()
            };
            stdfs::remove_all(stdfs::path{ std::string(temp_dir_) });
            stdfs::create_directories(stdfs::path{ std::string(temp_dir_) });
            library_ = std::make_unique<EngineAssetLibrary>(
                null_device_, logger_, temp_dir_);
        }

        void TearDown() override
        {
            library_.reset();
            stdfs::remove_all(stdfs::path{ std::string(temp_dir_) });
        }

        AudioClipAsset make_tone(const char* name)
        {
            return library_->audio_clips().create_procedural_tone_audio_clip({
                .name = name,
                .sample_rate = 48000,
                .channels = 1,
                .waveform = AudioToneWaveform::Sine,
                .frequency = 1000.0f,
                .duration_seconds = 0.01f,
                .amplitude = 0.5f,
                });
        }

        wz::gpu::Device   null_device_{};
        wz::Logger        logger_{};
        wz::fs::Path      temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };


    TEST_F(AudioClipBankTest, FromClipsResolvesAllClipsAndNameHashes)
    {
        const AudioClipAsset a = make_tone("bank/a");
        const AudioClipAsset b = make_tone("bank/b");
        const AudioClipAsset c = make_tone("bank/c");
        ASSERT_TRUE(a.valid() && b.valid() && c.valid());

        AudioClipBankFromClipsDesc desc{};
        desc.items = {
            { "alpha", a },
            { "beta",  b },
            { "gamma", c },
        };
        const AudioClipBankAsset bank =
            library_->audio_clip_banks().create_audio_clip_bank_from_clips(desc);
        ASSERT_TRUE(bank.valid());

        ASSERT_TRUE(library_->commit());
        ASSERT_TRUE(library_->resolve_all().ok());

        const AudioClipBankHandle h =
            library_->audio_clip_banks().get_audio_clip_bank(bank);
        ASSERT_TRUE(h.valid());

        const AudioClipBankData* data =
            library_->audio_clip_banks().get_audio_clip_bank_data(h);
        ASSERT_NE(data, nullptr);
        ASSERT_EQ(data->size(), 3u);
        EXPECT_TRUE(data->valid());

        // Name hashes are in item order and find_by_name_hash resolves them.
        EXPECT_EQ(data->entries[0].name_hash, fnv1a_32("alpha"));
        EXPECT_EQ(data->entries[1].name_hash, fnv1a_32("beta"));
        EXPECT_EQ(data->entries[2].name_hash, fnv1a_32("gamma"));

        const AudioClipBankEntry* beta =
            data->find_by_name_hash(fnv1a_32("beta"));
        ASSERT_NE(beta, nullptr);
        EXPECT_EQ(beta->name_hash, fnv1a_32("beta"));

        // Every entry's clip handle resolves to valid clip data.
        for (const AudioClipBankEntry& e : data->entries) {
            const AudioClipData* clip =
                library_->audio_clips().get_audio_clip_data(
                    AudioClipHandle{ e.clip });
            ASSERT_NE(clip, nullptr);
            EXPECT_TRUE(clip->valid());
        }
    }

    TEST_F(AudioClipBankTest, EmptyClipListProducesInvalidAsset)
    {
        AudioClipBankFromClipsDesc desc{};
        const AudioClipBankAsset bank =
            library_->audio_clip_banks().create_audio_clip_bank_from_clips(desc);
        EXPECT_FALSE(bank.valid());
    }

    TEST_F(AudioClipBankTest, FromDirectoryEnumeratesWavsInSortedOrder)
    {
        // Write WAVs out of lexicographic order to a dedicated subdir; the bank
        // must enumerate them sorted by resolved path for determinism.
        const stdfs::path dir =
            stdfs::path{ std::string(temp_dir_) } / "wavs";
        stdfs::create_directories(dir);
        write_file(dir / "charlie.wav", tiny_wav());
        write_file(dir / "alpha.wav",   tiny_wav());
        write_file(dir / "bravo.wav",   tiny_wav());
        // A non-WAV file must be ignored.
        write_file(dir / "notes.txt", { 'x', 'y', 'z' });

        AudioClipBankFromDirectoryDesc desc{};
        desc.directory = dir.string(); // absolute path passes through resolve
        const AudioClipBankAsset bank =
            library_->audio_clip_banks()
                .create_audio_clip_bank_from_directory(desc);
        ASSERT_TRUE(bank.valid());

        ASSERT_TRUE(library_->commit());
        ASSERT_TRUE(library_->resolve_all().ok());

        const AudioClipBankData* data =
            library_->audio_clip_banks().get_audio_clip_bank_data(
                library_->audio_clip_banks().get_audio_clip_bank(bank));
        ASSERT_NE(data, nullptr);

        // Exactly the three WAVs, in sorted (alpha, bravo, charlie) order.
        ASSERT_EQ(data->size(), 3u);
        EXPECT_EQ(data->entries[0].name_hash, fnv1a_32("alpha"));
        EXPECT_EQ(data->entries[1].name_hash, fnv1a_32("bravo"));
        EXPECT_EQ(data->entries[2].name_hash, fnv1a_32("charlie"));
        EXPECT_TRUE(data->valid());

        // The directory recipe populates the parallel name vector (stems), unlike
        // the from-clips recipe (which carries only hashes).
        ASSERT_EQ(data->names.size(), 3u);
        EXPECT_EQ(data->names[0], "alpha");
        EXPECT_EQ(data->names[1], "bravo");
        EXPECT_EQ(data->names[2], "charlie");
    }

    TEST_F(AudioClipBankTest, FromDirectoryRecursiveDescendsSubfolders)
    {
        const stdfs::path dir =
            stdfs::path{ std::string(temp_dir_) } / "rwavs";
        stdfs::create_directories(dir / "sub");
        write_file(dir / "top.wav", tiny_wav());
        write_file(dir / "sub" / "deep.wav", tiny_wav());

        AudioClipBankFromDirectoryDesc desc{};
        desc.directory = dir.string();
        desc.recursive = true;
        const AudioClipBankAsset bank =
            library_->audio_clip_banks()
                .create_audio_clip_bank_from_directory(desc);
        ASSERT_TRUE(bank.valid());

        ASSERT_TRUE(library_->commit());
        ASSERT_TRUE(library_->resolve_all().ok());

        const AudioClipBankData* data =
            library_->audio_clip_banks().get_audio_clip_bank_data(
                library_->audio_clip_banks().get_audio_clip_bank(bank));
        ASSERT_NE(data, nullptr);
        EXPECT_EQ(data->size(), 2u); // top.wav + sub/deep.wav
    }

    TEST_F(AudioClipBankTest, EmptyDirectoryStringRejectedAtAuthoring)
    {
        // An empty directory string is rejected up front (no node registered).
        AudioClipBankFromDirectoryDesc desc{};
        const AudioClipBankAsset bank =
            library_->audio_clip_banks()
                .create_audio_clip_bank_from_directory(desc);
        EXPECT_FALSE(bank.valid());
    }

    TEST_F(AudioClipBankTest, NonexistentDirectoryFailsToCompile)
    {
        // A non-existent directory registers a node (valid key) but fails to
        // compile — the directory check is the compiler's, not the module's.
        AudioClipBankFromDirectoryDesc desc{};
        desc.directory =
            (stdfs::path{ std::string(temp_dir_) } / "does_not_exist").string();
        const AudioClipBankAsset bank =
            library_->audio_clip_banks()
                .create_audio_clip_bank_from_directory(desc);
        ASSERT_TRUE(bank.valid());

        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        // The bank node did not produce valid data.
        const AudioClipBankHandle h =
            library_->audio_clip_banks().get_audio_clip_bank(bank);
        const AudioClipBankData* data =
            library_->audio_clip_banks().get_audio_clip_bank_data(h);
        EXPECT_TRUE(data == nullptr || !data->valid());
    }

} // namespace wz::engine::assets::test
