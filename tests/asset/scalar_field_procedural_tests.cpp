// engine/assets/scalar_field/procedural_scalar_field_tests.cpp
//
// Tests for procedural scalar field recipes.
//
// Key identity tests run as plain TESTs (no fixture needed — they only call
// the key factory directly). Generation tests use ScalarFieldTest, the same
// fixture from scalar_field_tests.cpp, which provides a live EngineAssetLibrary
// backed by a null device and a fresh temp directory.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/key_factories/scalar_field_procedural.h>

#include <file/filesystem.h>
#include <logging/logger.h>

#include <cmath>
#include <filesystem>

namespace wz::engine::assets::test {

    namespace stdfs = std::filesystem;


    // ─── Test fixture (mirrors scalar_field_tests.cpp) ────────────────────────────
    //
    // Declared here as ProceduralScalarFieldTest so it can live in this translation
    // unit without a shared header. If the fixture is later centralised, this class
    // can be removed and the TEST_F macros updated.

    class ProceduralScalarFieldTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (stdfs::temp_directory_path()
                    / "wz_procedural_scalar_field_tests").string()
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

        wz::gpu::Device   null_device_{};
        wz::Logger        logger_{};
        wz::fs::Path      temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };


    // ─── Key identity tests ────────────────────────────────────────────────────────
    //
    // These validate the key factory in isolation — no library or file system needed.
    // Run first: if the key factory is broken the generation tests will produce
    // confusing results (aliased cache entries, duplicate registration failures).

    // Changing the generator ordinal must produce a different AssetKey.
    // This is the most important key test: it proves that distinct recipes do not
    // silently share a cache entry.
    TEST(ProceduralScalarFieldKeyTests, GeneratorContributesToIdentity)
    {
        const auto gradient = make_procedural_scalar_field_key(
            "debug/test", 4, 4, 1,
            static_cast<uint8_t>(ScalarFieldGenerator::GradientX),
            1.0f, 1.0f,
            static_cast<uint8_t>(ScalarFieldFormat::Float32),
            static_cast<uint8_t>(ScalarFieldDomainKind::Spatial2D)
        );

        const auto checker = make_procedural_scalar_field_key(
            "debug/test", 4, 4, 1,
            static_cast<uint8_t>(ScalarFieldGenerator::Checkerboard),
            1.0f, 1.0f,
            static_cast<uint8_t>(ScalarFieldFormat::Float32),
            static_cast<uint8_t>(ScalarFieldDomainKind::Spatial2D)
        );

        ASSERT_NE(gradient, checker);
    }

    // Changing only the name must produce a different AssetKey.
    // This locks in the intentional behavior: name is part of identity, so two
    // procedural fields with identical parameters but different names are distinct
    // assets and can coexist in the DAG without a duplicate registration failure.
    TEST(ProceduralScalarFieldKeyTests, NameContributesToIdentity)
    {
        const auto a = make_procedural_scalar_field_key(
            "debug/a", 4, 4, 1,
            static_cast<uint8_t>(ScalarFieldGenerator::GradientX),
            1.0f, 1.0f,
            static_cast<uint8_t>(ScalarFieldFormat::Float32),
            static_cast<uint8_t>(ScalarFieldDomainKind::Spatial2D)
        );

        const auto b = make_procedural_scalar_field_key(
            "debug/b", 4, 4, 1,
            static_cast<uint8_t>(ScalarFieldGenerator::GradientX),
            1.0f, 1.0f,
            static_cast<uint8_t>(ScalarFieldFormat::Float32),
            static_cast<uint8_t>(ScalarFieldDomainKind::Spatial2D)
        );

        ASSERT_NE(a, b);
    }


    // ─── Registration and resolution tests ────────────────────────────────────────

    // create_procedural_scalar_field() returns a valid ScalarFieldAsset.
    TEST_F(ProceduralScalarFieldTest, CreateProceduralScalarFieldReturnsValidAssetKey)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/gradient_x",
                .width = 4,
                .height = 4,
                .generator = ScalarFieldGenerator::GradientX,
                });

        EXPECT_TRUE(asset.valid());
    }

    // resolve_all() produces a valid handle for a procedural scalar field.
    TEST_F(ProceduralScalarFieldTest, ProceduralScalarFieldReturnsValidHandle)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/gradient_x",
                .width = 4,
                .height = 4,
                .generator = ScalarFieldGenerator::GradientX,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        EXPECT_TRUE(handle.valid());
    }


    // ─── Generator correctness tests ──────────────────────────────────────────────

    // GradientX: left edge == 0, right edge == 1, consistent across all rows.
    TEST_F(ProceduralScalarFieldTest, ProceduralGradientXHasExpectedCornerValues)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/gradient_x",
                .width = 4,
                .height = 2,
                .generator = ScalarFieldGenerator::GradientX,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        ASSERT_TRUE(handle.valid());

        const ScalarFieldData* data = library_->get_scalar_field_data(handle);
        ASSERT_NE(data, nullptr);

        EXPECT_FLOAT_EQ(data->at(0, 0), 0.0f);
        EXPECT_FLOAT_EQ(data->at(3, 0), 1.0f);
        EXPECT_FLOAT_EQ(data->at(0, 1), 0.0f);
        EXPECT_FLOAT_EQ(data->at(3, 1), 1.0f);
    }

    // GradientY: top edge == 0, bottom edge == 1, consistent across all columns.
    TEST_F(ProceduralScalarFieldTest, ProceduralGradientYHasExpectedCornerValues)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/gradient_y",
                .width = 2,
                .height = 4,
                .generator = ScalarFieldGenerator::GradientY,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        ASSERT_TRUE(handle.valid());

        const ScalarFieldData* data = library_->get_scalar_field_data(handle);
        ASSERT_NE(data, nullptr);

        EXPECT_FLOAT_EQ(data->at(0, 0), 0.0f);
        EXPECT_FLOAT_EQ(data->at(0, 3), 1.0f);
        EXPECT_FLOAT_EQ(data->at(1, 0), 0.0f);
        EXPECT_FLOAT_EQ(data->at(1, 3), 1.0f);
    }

    // Checkerboard (2x2, cell size 1): alternating 0 / amplitude in a known pattern.
    // Expected layout (row-major, top-left origin, cell=1):
    //   (0,0)=0  (1,0)=1
    //   (0,1)=1  (1,1)=0
    TEST_F(ProceduralScalarFieldTest, ProceduralCheckerboardAlternatesValues)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/checker",
                .width = 2,
                .height = 2,
                .generator = ScalarFieldGenerator::Checkerboard,
                .frequency = 1.0f,
                .amplitude = 1.0f,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        ASSERT_TRUE(handle.valid());

        const ScalarFieldData* data = library_->get_scalar_field_data(handle);
        ASSERT_NE(data, nullptr);

        EXPECT_FLOAT_EQ(data->at(0, 0), 0.0f);
        EXPECT_FLOAT_EQ(data->at(1, 0), 1.0f);
        EXPECT_FLOAT_EQ(data->at(0, 1), 1.0f);
        EXPECT_FLOAT_EQ(data->at(1, 1), 0.0f);
    }

    // RadialGradient (3x3, amplitude 1): center sample == 0, corner sample == 1.
    // 3x3 is chosen because it has a true integer center at (1,1).
    // Corner normalisation: dist(corner, center) == max_dist, so corner == 1.
    TEST_F(ProceduralScalarFieldTest, ProceduralRadialGradientHasCenterDarkCornerBright)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/radial",
                .width = 3,
                .height = 3,
                .generator = ScalarFieldGenerator::RadialGradient,
                .amplitude = 1.0f,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        ASSERT_TRUE(handle.valid());

        const ScalarFieldData* data = library_->get_scalar_field_data(handle);
        ASSERT_NE(data, nullptr);

        EXPECT_FLOAT_EQ(data->at(1, 1), 0.0f); // center
        EXPECT_FLOAT_EQ(data->at(0, 0), 1.0f); // corner — normalised max distance
    }

    // SineWaves (width=1): u falls back to 0, so sin(0) == 0 and the normalised
    // wave gives 0.5. Tests the width-one guard path explicitly.
    TEST_F(ProceduralScalarFieldTest, ProceduralSineWavesHandlesWidthOne)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/sine_width_one",
                .width = 1,
                .height = 1,
                .generator = ScalarFieldGenerator::SineWaves,
                .frequency = 4.0f,
                .amplitude = 1.0f,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        ASSERT_TRUE(handle.valid());

        const ScalarFieldData* data = library_->get_scalar_field_data(handle);
        ASSERT_NE(data, nullptr);

        // u == 0 → sin(0) == 0 → (0.5 + 0.5 * 0) * 1.0 == 0.5
        EXPECT_FLOAT_EQ(data->at(0, 0), 0.5f);
    }

    // GradientX min/max: for a 4x4 field, min == 0.0 and max == 1.0 exactly.
    TEST_F(ProceduralScalarFieldTest, ProceduralScalarFieldComputesMinMax)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/gradient_x_minmax",
                .width = 4,
                .height = 4,
                .generator = ScalarFieldGenerator::GradientX,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        ASSERT_TRUE(handle.valid());

        const ScalarFieldData* data = library_->get_scalar_field_data(handle);
        ASSERT_NE(data, nullptr);

        EXPECT_FLOAT_EQ(data->min_value, 0.0f);
        EXPECT_FLOAT_EQ(data->max_value, 1.0f);
    }


    // ─── Validation / rejection tests ─────────────────────────────────────────────

    // Zero width is rejected by the compiler; handle must be invalid after resolve.
    TEST_F(ProceduralScalarFieldTest, ProceduralScalarFieldRejectsZeroDimensions)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/zero_dim",
                .width = 0,   // invalid
                .height = 4,
                .generator = ScalarFieldGenerator::GradientX,
                });

        ASSERT_TRUE(asset.valid()); // registration succeeds; compile validates
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        EXPECT_FALSE(handle.valid());
    }

    // depth > 1 is rejected in V1; handle must be invalid after resolve.
    TEST_F(ProceduralScalarFieldTest, ProceduralScalarFieldRejectsDepthGreaterThanOne)
    {
        const ScalarFieldAsset asset =
            library_->create_procedural_scalar_field({
                .name = "debug/depth_two",
                .width = 4,
                .height = 4,
                .depth = 2,   // not supported in V1
                .generator = ScalarFieldGenerator::GradientX,
                });

        ASSERT_TRUE(asset.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const ScalarFieldHandle handle = library_->get_scalar_field(asset);
        EXPECT_FALSE(handle.valid());
    }

} // namespace wz::engine::assets::test
