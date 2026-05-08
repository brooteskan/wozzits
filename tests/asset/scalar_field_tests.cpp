// engine/assets/scalar_field/scalar_field_tests.cpp
//
// GTests for the scalar field asset type.
//
// ── Test fixture ──────────────────────────────────────────────────────────────
//
// ScalarFieldTest creates a temporary resource directory and an
// EngineAssetLibrary backed by a null device. The null device is safe here
// because scalar field compilation is entirely CPU-side — the device reference
// is only dereferenced inside the HLSL compiler lambda, which these tests never
// trigger.
//
// The fixture writes raw .f32 files to the temp directory using a helper, so
// tests can register real file-backed scalar fields without external fixtures.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scalar_field/scalar_field.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

namespace wz::engine::assets::test {

    namespace fs = std::filesystem;


    // ─── Helpers ─────────────────────────────────────────────────────────────────

    // Write a vector of float32 values as a raw binary file.
    // Returns true on success.
    // overwrite=true so tests can re-run cleanly into the same temp directory.
    static bool write_raw_f32(
        const wz::fs::Path& path,
        const std::vector<float>& values)
    {
        const size_t byte_count = values.size() * sizeof(float);
        wz::fs::Buffer bytes(byte_count);
        std::memcpy(bytes.data(), values.data(), byte_count);
        return wz::fs::write_file(path, bytes, /*overwrite=*/true) == wz::fs::FileError::None;
    }

    // Generate a flat 2D gradient in X: value = x / (width - 1).
    static std::vector<float> make_gradient_x(uint32_t width, uint32_t height)
    {
        std::vector<float> values(width * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                values[x + y * width] =
                    (width > 1) ? static_cast<float>(x) / static_cast<float>(width - 1)
                    : 0.0f;
            }
        }
        return values;
    }

    // Generate a flat 2x2 field with known values for precise validation.
    // Layout: [0.0, 0.25, 0.75, 1.0]  (row-major, top-left origin)
    static std::vector<float> make_2x2_known()
    {
        return { 0.0f, 0.25f, 0.75f, 1.0f };
    }


    // ─── Test fixture ─────────────────────────────────────────────────────────────

    class ScalarFieldTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (fs::temp_directory_path() / "wz_scalar_field_tests").string()
            };
            fs::create_directories(temp_dir_);

            // Construct the library. A null/default-constructed device is safe because
            // scalar field compilation never touches the GPU path.
            library_ = std::make_unique<EngineAssetLibrary>(
                null_device_,
                logger_,
                temp_dir_
            );
        }

        void TearDown() override
        {
            library_.reset();
            fs::remove_all(temp_dir_);
        }

        // Write a raw f32 file relative to temp_dir_ and return its relative path.
        wz::fs::Path write_field_file(
            const std::string& name,
            const std::vector<float>& values)
        {
            const wz::fs::Path rel{ name };
            const wz::fs::Path full = wz::fs::join(temp_dir_, rel);
            EXPECT_TRUE(write_raw_f32(full, values));
            return rel;
        }

        wz::gpu::Device   null_device_{};   // never dereferenced by scalar field code
        wz::Logger        logger_{};
        wz::fs::Path      temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };


    // ─── Tests ───────────────────────────────────────────────────────────────────

    // A default-constructed ScalarFieldAsset is invalid.
    // Verifies that the null/sentinel state is correctly detected before any
    // registration or resolution takes place.
    TEST(ScalarFieldAssetTests, DefaultScalarFieldAssetIsInvalid)
    {
        ScalarFieldAsset asset{};
        EXPECT_FALSE(asset.valid());
    }

    // create_scalar_field() returns a valid asset key after successful registration.
    // Verifies registration path and that the output key is non-null.
    TEST_F(ScalarFieldTest, CreateScalarFieldReturnsValidAssetKey)
    {
        const auto values = make_gradient_x(4, 4);
        const auto rel = write_field_file("gradient_4x4.rawf32", values);

        ScalarFieldFileDesc desc{
            .name = "gradient_4x4",
            .path = rel,
            .width = 4,
            .height = 4,
            .depth = 1,
            .format = ScalarFieldFormat::Float32,
            .domain_kind = ScalarFieldDomainKind::Spatial2D,
        };

        const ScalarFieldAsset asset = library_->create_scalar_field(desc);

        EXPECT_TRUE(asset.valid());
    }

    // commit() succeeds after a scalar field has been registered.
    // Verifies the DAG is acyclic and all dependencies are present.
    TEST_F(ScalarFieldTest, CommitSucceedsAfterScalarFieldRegistration)
    {
        const auto values = make_gradient_x(4, 4);
        const auto rel = write_field_file("commit_test.rawf32", values);

        ScalarFieldFileDesc desc{
            .name = "commit_test",
            .path = rel,
            .width = 4,
            .height = 4,
            .depth = 1,
        };

        const ScalarFieldAsset asset = library_->create_scalar_field(desc);
        ASSERT_TRUE(asset.valid());

        EXPECT_TRUE(library_->commit());
    }

    // resolve_all() produces a valid ResourceHandle for a registered scalar field.
    // Verifies the full compile path executes without error.
    TEST_F(ScalarFieldTest, ResolveScalarFieldProducesValidHandle)
    {
        const auto values = make_gradient_x(4, 4);
        const auto rel = write_field_file("resolve_test.rawf32", values);

        ScalarFieldFileDesc desc{
            .name = "resolve_test",
            .path = rel,
            .width = 4,
            .height = 4,
            .depth = 1,
        };

        const ScalarFieldAsset asset = library_->create_scalar_field(desc);
        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());

        const auto report = library_->resolve_all();
        EXPECT_GT(report.resolved_count, 0u);

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        EXPECT_TRUE(handle.valid());
    }

    // The resolved ScalarFieldData contains exactly the values written to the file.
    // Uses a 2x2 field with four known float values for precise per-sample checking.
    TEST_F(ScalarFieldTest, ScalarFieldDataMatchesRawF32Input)
    {
        const auto values = make_2x2_known(); // [0.0, 0.25, 0.75, 1.0]
        const auto rel = write_field_file("known_2x2.rawf32", values);

        ScalarFieldFileDesc desc{
            .name = "known_2x2",
            .path = rel,
            .width = 2,
            .height = 2,
            .depth = 1,
        };

        const ScalarFieldAsset asset = library_->create_scalar_field(desc);
        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        ASSERT_TRUE(handle.valid());

        const ScalarFieldData* data = library_->get_scalar_field_data(handle);
        ASSERT_NE(data, nullptr);

        ASSERT_TRUE(data->valid());
        EXPECT_EQ(data->width, 2u);
        EXPECT_EQ(data->height, 2u);
        EXPECT_EQ(data->depth, 1u);

        EXPECT_FLOAT_EQ(data->at(0, 0), 0.00f);
        EXPECT_FLOAT_EQ(data->at(1, 0), 0.25f);
        EXPECT_FLOAT_EQ(data->at(0, 1), 0.75f);
        EXPECT_FLOAT_EQ(data->at(1, 1), 1.00f);
    }

    // The compiler rejects a file whose byte count does not match the declared
    // dimensions. Verifies that get_scalar_field() returns an invalid handle.
    TEST_F(ScalarFieldTest, ScalarFieldRejectsWrongByteCount)
    {
        // Write a 4x4 file (64 bytes) but declare 8x8 dimensions (256 bytes expected).
        const auto values = make_gradient_x(4, 4); // 16 floats = 64 bytes
        const auto rel = write_field_file("wrong_size.rawf32", values);

        ScalarFieldFileDesc desc{
            .name = "wrong_size",
            .path = rel,
            .width = 8,   // mismatch — expects 8*8*4 = 256 bytes
            .height = 8,
            .depth = 1,
        };

        const ScalarFieldAsset asset = library_->create_scalar_field(desc);
        ASSERT_TRUE(asset.valid()); // registration itself succeeds

        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        // Compilation should fail; handle must be invalid.
        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        EXPECT_FALSE(handle.valid());
    }

    // The compiler rejects a registration with any zero dimension.
    // Zero width, height, or depth makes count() == 0 which is incoherent.
    TEST_F(ScalarFieldTest, ScalarFieldRejectsZeroDimensions)
    {
        // Write a non-empty file so the failure comes from the dimension check,
        // not from a missing file.
        const auto values = make_gradient_x(4, 4);
        const auto rel = write_field_file("zero_dim.rawf32", values);

        ScalarFieldFileDesc desc{
            .name = "zero_dim",
            .path = rel,
            .width = 0,   // invalid
            .height = 4,
            .depth = 1,
        };

        const ScalarFieldAsset asset = library_->create_scalar_field(desc);
        ASSERT_TRUE(asset.valid());

        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        EXPECT_FALSE(handle.valid());
    }

    // The compiler computes correct min_value and max_value from the field contents.
    // Uses a 2x2 field with four known values; checks both extremes exactly.
    TEST_F(ScalarFieldTest, ScalarFieldComputesMinMax)
    {
        const auto values = make_2x2_known(); // min = 0.0, max = 1.0
        const auto rel = write_field_file("minmax_2x2.rawf32", values);

        ScalarFieldFileDesc desc{
            .name = "minmax_2x2",
            .path = rel,
            .width = 2,
            .height = 2,
            .depth = 1,
        };

        const ScalarFieldAsset asset = library_->create_scalar_field(desc);
        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        ASSERT_TRUE(handle.valid());

        const ScalarFieldData* data = library_->get_scalar_field_data(handle);
        ASSERT_NE(data, nullptr);

        EXPECT_FLOAT_EQ(data->min_value, 0.00f);
        EXPECT_FLOAT_EQ(data->max_value, 1.00f);
    }

    TEST(ScalarFieldTableTests, DestroyRestoresNullSentinel)
    {
        ScalarFieldTable table;

        ScalarFieldData field;
        field.width = 1;
        field.height = 1;
        field.depth = 1;
        field.values = { 42.0f };

        auto h1 = table.add(field);
        EXPECT_TRUE(h1.valid());
        EXPECT_NE(h1.id, 0u);

        table.destroy();

        auto h2 = table.add(field);
        EXPECT_TRUE(h2.valid());
        EXPECT_NE(h2.id, 0u);
    }


} // namespace wz::engine::assets::test

