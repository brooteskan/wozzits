// tests/asset/csv_tests.cpp
//
// Tests for the CSV asset type.
//
// The fixture creates a temporary directory and an EngineAssetLibrary backed
// by a null device. CSV compilation is entirely CPU-side, so the device is
// never dereferenced.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/csv/csv.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <filesystem>
#include <string>

namespace wz::engine::assets::test {

    namespace stdfs = std::filesystem;

    // ─── Helpers ─────────────────────────────────────────────────────────────────

    static bool write_text_file(const wz::fs::Path& path, const std::string& text)
    {
        wz::fs::Buffer bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return wz::fs::write_file(path, bytes, /*overwrite=*/true)
               == wz::fs::FileError::None;
    }


    // ─── Test fixture ─────────────────────────────────────────────────────────────

    class CSVTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (stdfs::temp_directory_path() / "wz_csv_tests").string()
            };
            stdfs::create_directories(temp_dir_);

            library_ = std::make_unique<EngineAssetLibrary>(
                null_device_,
                logger_,
                temp_dir_
            );
        }

        void TearDown() override
        {
            library_.reset();
            stdfs::remove_all(temp_dir_);
        }

        wz::fs::Path write_csv(const std::string& filename, const std::string& text)
        {
            const wz::fs::Path rel{ filename };
            const wz::fs::Path full = wz::fs::join(temp_dir_, rel);
            EXPECT_TRUE(write_text_file(full, text));
            return rel;
        }

        const CSVData* resolve_and_get(const CSVFileDesc& desc)
        {
            const CSVAsset asset = library_->csv().create_csv(desc);
            if (!asset.valid()) return nullptr;
            if (!library_->commit()) return nullptr;
            library_->resolve_all();
            const CSVHandle handle = library_->csv().get_csv(asset);
            if (!handle.valid()) return nullptr;
            return library_->csv().get_csv_data(handle);
        }

        wz::gpu::Device null_device_{};
        wz::Logger      logger_{};
        wz::fs::Path    temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };


    // ─── Null-state tests ─────────────────────────────────────────────────────────

    TEST(CSVAssetTests, DefaultCSVAssetIsInvalid)
    {
        CSVAsset asset{};
        EXPECT_FALSE(asset.valid());
    }

    TEST(CSVAssetTests, DefaultCSVHandleIsInvalid)
    {
        CSVHandle handle{};
        EXPECT_FALSE(handle.valid());
    }


    // ─── Registration and resolution ──────────────────────────────────────────────

    TEST_F(CSVTest, CreateCSVReturnsValidAsset)
    {
        const auto rel = write_csv("simple.csv", "a,b,c\n1,2,3\n");

        const CSVAsset asset = library_->csv().create_csv({
            .name = "simple",
            .path = rel,
        });

        EXPECT_TRUE(asset.valid());
    }

    TEST_F(CSVTest, CommitSucceedsAfterCSVRegistration)
    {
        const auto rel = write_csv("commit.csv", "x,y\n1,2\n");

        const CSVAsset asset = library_->csv().create_csv({
            .name = "commit",
            .path = rel,
        });

        ASSERT_TRUE(asset.valid());
        EXPECT_TRUE(library_->commit());
    }

    TEST_F(CSVTest, ResolveCSVProducesValidHandle)
    {
        const auto rel = write_csv("resolve.csv", "a,b\n1,2\n");

        const CSVAsset asset = library_->csv().create_csv({
            .name = "resolve",
            .path = rel,
        });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const CSVHandle handle = library_->csv().get_csv(asset);
        EXPECT_TRUE(handle.valid());
    }


    // ─── Header detection (Auto mode) ─────────────────────────────────────────────

    // First row is all non-numeric strings → Auto detects a header.
    TEST_F(CSVTest, AutoDetectsHeaderWhenFirstRowIsNonNumeric)
    {
        const auto rel = write_csv("header_auto.csv", "id,name,score\n1,alice,99\n2,bob,87\n");

        const CSVData* data = resolve_and_get({
            .name = "header_auto",
            .path = rel,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_TRUE(data->has_header);
        ASSERT_EQ(data->headers.size(), 3u);
        EXPECT_EQ(data->headers[0], "id");
        EXPECT_EQ(data->headers[1], "name");
        EXPECT_EQ(data->headers[2], "score");
        EXPECT_EQ(data->rows.size(), 2u);
    }

    // First row contains a numeric cell → Auto treats it as data.
    TEST_F(CSVTest, AutoTreatsFirstRowAsDataWhenAnyNumeric)
    {
        const auto rel = write_csv("all_data.csv", "1,alice,99\n2,bob,87\n");

        const CSVData* data = resolve_and_get({
            .name = "all_data",
            .path = rel,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_FALSE(data->has_header);
        EXPECT_TRUE(data->headers.empty());
        EXPECT_EQ(data->rows.size(), 2u);
    }


    // ─── Header mode overrides ────────────────────────────────────────────────────

    // ForceHeader treats row 0 as header even when it contains numbers.
    TEST_F(CSVTest, ForceHeaderTreatsFirstRowAsHeaderAlways)
    {
        const auto rel = write_csv("force_header.csv", "1,2,3\n4,5,6\n");

        const CSVData* data = resolve_and_get({
            .name = "force_header",
            .path = rel,
            .header_mode = CSVHeaderMode::ForceHeader,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_TRUE(data->has_header);
        ASSERT_EQ(data->headers.size(), 3u);
        EXPECT_EQ(data->headers[0], "1");
        EXPECT_EQ(data->rows.size(), 1u);
    }

    // ForceData treats all rows as data even when first row is non-numeric.
    TEST_F(CSVTest, ForceDataTreatsAllRowsAsData)
    {
        const auto rel = write_csv("force_data.csv", "id,name,score\n1,alice,99\n");

        const CSVData* data = resolve_and_get({
            .name = "force_data",
            .path = rel,
            .header_mode = CSVHeaderMode::ForceData,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_FALSE(data->has_header);
        EXPECT_TRUE(data->headers.empty());
        EXPECT_EQ(data->rows.size(), 2u);
        EXPECT_EQ(data->rows[0][0], "id");
    }

    // Same file with different header modes must produce distinct asset keys.
    TEST_F(CSVTest, DifferentHeaderModeProducesDistinctAssetKey)
    {
        const auto rel = write_csv("mode_key.csv", "1,2\n3,4\n");

        const CSVAsset auto_asset = library_->csv().create_csv({
            .name = "mode_key_auto",
            .path = rel,
            .header_mode = CSVHeaderMode::Auto,
        });

        const CSVAsset force_asset = library_->csv().create_csv({
            .name = "mode_key_force",
            .path = rel,
            .header_mode = CSVHeaderMode::ForceHeader,
        });

        ASSERT_TRUE(auto_asset.valid());
        ASSERT_TRUE(force_asset.valid());
        EXPECT_NE(auto_asset.output, force_asset.output);
    }


    // ─── Cell content correctness ─────────────────────────────────────────────────

    TEST_F(CSVTest, CellValuesMatchRawInput)
    {
        const auto rel = write_csv("cells.csv", "x,y\n1.5,hello\n");

        const CSVData* data = resolve_and_get({
            .name = "cells",
            .path = rel,
        });

        ASSERT_NE(data, nullptr);
        ASSERT_EQ(data->rows.size(), 1u);
        ASSERT_EQ(data->rows[0].size(), 2u);
        EXPECT_EQ(data->rows[0][0], "1.5");
        EXPECT_EQ(data->rows[0][1], "hello");
    }

    // Quoted fields containing commas are parsed as a single cell.
    TEST_F(CSVTest, QuotedFieldWithCommaIsOneCell)
    {
        const auto rel = write_csv("quoted.csv", "a,\"hello, world\",b\n");

        const CSVData* data = resolve_and_get({
            .name = "quoted",
            .path = rel,
            .header_mode = CSVHeaderMode::ForceData,
        });

        ASSERT_NE(data, nullptr);
        ASSERT_EQ(data->rows.size(), 1u);
        ASSERT_EQ(data->rows[0].size(), 3u);
        EXPECT_EQ(data->rows[0][0], "a");
        EXPECT_EQ(data->rows[0][1], "hello, world");
        EXPECT_EQ(data->rows[0][2], "b");
    }

    // "" inside a quoted field is an escaped double-quote character.
    TEST_F(CSVTest, EscapedQuoteInQuotedField)
    {
        const auto rel = write_csv("escaped_quote.csv", "\"foo\"\"bar\"\n");

        const CSVData* data = resolve_and_get({
            .name = "escaped_quote",
            .path = rel,
            .header_mode = CSVHeaderMode::ForceData,
        });

        ASSERT_NE(data, nullptr);
        ASSERT_EQ(data->rows.size(), 1u);
        ASSERT_EQ(data->rows[0].size(), 1u);
        EXPECT_EQ(data->rows[0][0], "foo\"bar");
    }

    // CRLF line endings are accepted and produce the same rows as LF.
    TEST_F(CSVTest, CRLFLineEndingsAccepted)
    {
        const auto rel = write_csv("crlf.csv", "a,b\r\n1,2\r\n");

        const CSVData* data = resolve_and_get({
            .name = "crlf",
            .path = rel,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_TRUE(data->has_header);
        EXPECT_EQ(data->rows.size(), 1u);
        ASSERT_EQ(data->rows[0].size(), 2u);
        EXPECT_EQ(data->rows[0][0], "1");
        EXPECT_EQ(data->rows[0][1], "2");
    }


    // ─── Ragged rows ──────────────────────────────────────────────────────────────

    TEST_F(CSVTest, RaggedRowsAcceptedAndFlagged)
    {
        const auto rel = write_csv("ragged.csv", "a,b,c\n1,2\n3,4,5,6\n");

        const CSVData* data = resolve_and_get({
            .name = "ragged",
            .path = rel,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_TRUE(data->is_ragged);
        EXPECT_EQ(data->max_column_count, 4u); // widest data row
        EXPECT_EQ(data->rows.size(), 2u);
        EXPECT_EQ(data->rows[0].size(), 2u);
        EXPECT_EQ(data->rows[1].size(), 4u);
    }

    // Uniform rows are not flagged as ragged.
    TEST_F(CSVTest, UniformRowsNotFlaggedAsRagged)
    {
        const auto rel = write_csv("uniform.csv", "a,b\n1,2\n3,4\n");

        const CSVData* data = resolve_and_get({
            .name = "uniform",
            .path = rel,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_FALSE(data->is_ragged);
        EXPECT_EQ(data->max_column_count, 2u);
    }


    // ─── Edge cases ───────────────────────────────────────────────────────────────

    // An empty file produces an empty CSVData with no rows and no header.
    TEST_F(CSVTest, EmptyFileProducesEmptyCSVData)
    {
        const auto rel = write_csv("empty.csv", "");

        const CSVData* data = resolve_and_get({
            .name = "empty",
            .path = rel,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_FALSE(data->has_header);
        EXPECT_TRUE(data->rows.empty());
        EXPECT_EQ(data->max_column_count, 0u);
    }

    // A missing file causes resolve to fail; handle is invalid.
    TEST_F(CSVTest, MissingFileProducesInvalidHandle)
    {
        const CSVAsset asset = library_->csv().create_csv({
            .name = "missing",
            .path = wz::fs::Path{ "no_such_file.csv" },
        });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const CSVHandle handle = library_->csv().get_csv(asset);
        EXPECT_FALSE(handle.valid());
    }

    // A file with no trailing newline is parsed correctly.
    TEST_F(CSVTest, NoTrailingNewlineAccepted)
    {
        const auto rel = write_csv("no_newline.csv", "a,b\n1,2");

        const CSVData* data = resolve_and_get({
            .name = "no_newline",
            .path = rel,
        });

        ASSERT_NE(data, nullptr);
        EXPECT_TRUE(data->has_header);
        EXPECT_EQ(data->rows.size(), 1u);
        EXPECT_EQ(data->rows[0][0], "1");
        EXPECT_EQ(data->rows[0][1], "2");
    }

    // CSVTable::destroy() resets the table; new entries can be added after.
    TEST(CSVTableTests, DestroyRestoresNullSentinel)
    {
        CSVTable table;

        CSVData d;
        d.rows = { { "a", "b" } };

        auto h1 = table.add(d);
        EXPECT_TRUE(h1.valid());
        EXPECT_NE(h1.id, 0u);

        table.destroy();

        auto h2 = table.add(d);
        EXPECT_TRUE(h2.valid());
        EXPECT_NE(h2.id, 0u);
    }

} // namespace wz::engine::assets::test
