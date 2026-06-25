// tests/asset_gaussian_splat/gaussian_splat_from_scalar_field.cpp
//
// Tests for the GaussianSplatFromScalarField asset compiler.
//
// Each test creates a procedural scalar field, builds a splat cloud from it,
// commits and resolves, then asserts on the resulting GaussianSplatCloudData.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <engine/assets/key_factories/gaussian_splat_from_scalar_field.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <cmath>
#include <filesystem>

namespace wz::engine::assets::test {

    namespace stdfs = std::filesystem;

    class GaussianSplatFromScalarFieldTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (stdfs::temp_directory_path()
                    / "wz_splat_from_scalar_field_tests").string()
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

        // Helper: create a procedural GradientX field and return its ScalarFieldAsset.
        ScalarFieldAsset make_gradient_field(uint32_t width, uint32_t height,
                                             const std::string& name = "test/gradient")
        {
            return library_->scalar_fields().create_procedural_scalar_field({
                .name      = name,
                .width     = width,
                .height    = height,
                .generator = ScalarFieldGenerator::GradientX,
            });
        }

        wz::gpu::Device   null_device_{};
        wz::Logger        logger_{};
        wz::fs::Path      temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };


    // ─── Key identity tests ───────────────────────────────────────────────────────
    //
    // Run against the key factory in isolation (no library needed).
    // Changing any generation parameter must produce a different AssetKey.

    TEST(GaussianSplatFromScalarFieldKeyTests, HeightScaleContributesToIdentity)
    {
        // Use a dummy scalar field key as the dependency.
        const wz::asset::AssetKey sf_key{
            .content_hash  = { 0xABCDull, 0 },
            .schema_hash   = { 0x1234ull, 0 },
            .compiler_hash = { 0x5678ull, 0 },
            .deps_hash     = { 0x9ABCull, 0 },
        };

        const GaussianSplatFromScalarFieldCompileDesc base{
            .height_scale = 1.0f,
            .step_x = 1.0f,
            .step_z = 1.0f,
            .splat_scale = 0.05f,
            .opacity = 0.9f,
        };

        GaussianSplatFromScalarFieldCompileDesc changed = base;
        changed.height_scale = 2.0f;

        EXPECT_NE(
            make_gaussian_splat_from_scalar_field_key(sf_key, base),
            make_gaussian_splat_from_scalar_field_key(sf_key, changed)
        );
    }

    TEST(GaussianSplatFromScalarFieldKeyTests, StepXContributesToIdentity)
    {
        const wz::asset::AssetKey sf_key{
            .content_hash = { 0x1111ull, 0 }, .schema_hash = { 0x2222ull, 0 },
            .compiler_hash = { 0x3333ull, 0 }, .deps_hash = { 0x4444ull, 0 },
        };
        const GaussianSplatFromScalarFieldCompileDesc base{ .step_x = 1.0f };
        GaussianSplatFromScalarFieldCompileDesc changed = base;
        changed.step_x = 2.0f;
        EXPECT_NE(
            make_gaussian_splat_from_scalar_field_key(sf_key, base),
            make_gaussian_splat_from_scalar_field_key(sf_key, changed)
        );
    }

    TEST(GaussianSplatFromScalarFieldKeyTests, SplatScaleContributesToIdentity)
    {
        const wz::asset::AssetKey sf_key{
            .content_hash = { 0xAAAAull, 0 }, .schema_hash = { 0xBBBBull, 0 },
            .compiler_hash = { 0xCCCCull, 0 }, .deps_hash = { 0xDDDDull, 0 },
        };
        const GaussianSplatFromScalarFieldCompileDesc base{ .splat_scale = 0.05f };
        GaussianSplatFromScalarFieldCompileDesc changed = base;
        changed.splat_scale = 0.1f;
        EXPECT_NE(
            make_gaussian_splat_from_scalar_field_key(sf_key, base),
            make_gaussian_splat_from_scalar_field_key(sf_key, changed)
        );
    }

    TEST(GaussianSplatFromScalarFieldKeyTests, ThresholdContributesToIdentity)
    {
        const wz::asset::AssetKey sf_key{
            .content_hash = { 0xEEEEull, 0 }, .schema_hash = { 0xFFFFull, 0 },
            .compiler_hash = { 0x0001ull, 0 }, .deps_hash = { 0x0002ull, 0 },
        };
        const GaussianSplatFromScalarFieldCompileDesc base{
            .use_threshold = false, .emit_threshold = 0.0f
        };
        GaussianSplatFromScalarFieldCompileDesc changed = base;
        changed.use_threshold = true;
        changed.emit_threshold = 0.5f;
        EXPECT_NE(
            make_gaussian_splat_from_scalar_field_key(sf_key, base),
            make_gaussian_splat_from_scalar_field_key(sf_key, changed)
        );
    }

    TEST(GaussianSplatFromScalarFieldKeyTests, DifferentSourceFieldProducesDifferentKey)
    {
        const GaussianSplatFromScalarFieldCompileDesc desc{};

        const wz::asset::AssetKey sf_a{
            .content_hash = { 0x0001ull, 0 }, .schema_hash = { 0x0002ull, 0 },
            .compiler_hash = { 0x0003ull, 0 }, .deps_hash = { 0x0004ull, 0 },
        };
        const wz::asset::AssetKey sf_b{
            .content_hash = { 0x1001ull, 0 }, .schema_hash = { 0x0002ull, 0 },
            .compiler_hash = { 0x0003ull, 0 }, .deps_hash = { 0x0004ull, 0 },
        };

        EXPECT_NE(
            make_gaussian_splat_from_scalar_field_key(sf_a, desc),
            make_gaussian_splat_from_scalar_field_key(sf_b, desc)
        );
    }


    // ─── Registration and resolution tests ───────────────────────────────────────

    // Basic end-to-end: 3×2 GradientX field → splat cloud with 6 splats.
    TEST_F(GaussianSplatFromScalarFieldTest, ResolvesToCloudWithCorrectSplatCount)
    {
        const ScalarFieldAsset sf = make_gradient_field(3, 2);
        ASSERT_TRUE(sf.valid());

        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/cloud",
                .scalar_field_key = sf.output,
                .height_scale     = 1.0f,
                .step_x           = 1.0f,
                .step_z           = 1.0f,
                .splat_scale      = 0.05f,
                .opacity          = 0.9f,
            });

        ASSERT_TRUE(cloud.valid());
        ASSERT_TRUE(library_->commit());
        const auto report = library_->resolve_all();
        ASSERT_TRUE(report.ok());

        const auto handle = library_->gaussian_splats().get_cloud(cloud);
        ASSERT_TRUE(handle.valid());

        const GaussianSplatCloudData* data =
            library_->gaussian_splats().get_cloud_data(handle);
        ASSERT_NE(data, nullptr);
        ASSERT_TRUE(data->valid());

        // 3 columns × 2 rows = 6 splats, no threshold applied.
        EXPECT_EQ(data->splat_count(), uint64_t{6});
    }

    // Bounds must be valid after resolve and contain the emitted positions.
    TEST_F(GaussianSplatFromScalarFieldTest, BoundsAreValidAndContainSplats)
    {
        const ScalarFieldAsset sf = make_gradient_field(3, 2);
        ASSERT_TRUE(sf.valid());

        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/bounds",
                .scalar_field_key = sf.output,
                .height_scale     = 2.0f,
                .step_x           = 1.0f,
                .step_z           = 1.0f,
                .splat_scale      = 0.05f,
                .opacity          = 0.9f,
            });

        ASSERT_TRUE(cloud.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const auto handle = library_->gaussian_splats().get_cloud(cloud);
        ASSERT_TRUE(handle.valid());

        const GaussianSplatCloudData* data =
            library_->gaussian_splats().get_cloud_data(handle);
        ASSERT_NE(data, nullptr);
        ASSERT_TRUE(data->bounds.valid);

        // Positions are normalized to a UNIT footprint (fix-2): XZ in [0,1]
        // across the field, Y the raw normalized height in [0,1]. height_scale
        // and step_x/z no longer bake into the coordinates -- the scene-node
        // transform owns world size/placement -- so a 3x2 field spans ~[0,1] on
        // every axis (+/- the splat radius). The cloud bounds must contain that.
        EXPECT_LE(data->bounds.min[0], 0.0f);   // X left edge (~0)
        EXPECT_GE(data->bounds.max[0], 1.0f);   // X right edge (~1)
        EXPECT_LE(data->bounds.min[1], 0.0f);   // Y min (~0)
        EXPECT_GE(data->bounds.max[1], 1.0f);   // Y max (~1, height_scale ignored)
        EXPECT_LE(data->bounds.min[2], 0.0f);   // Z min (~0)
        EXPECT_GE(data->bounds.max[2], 1.0f);   // Z max (~1)
    }

    // Positions: GradientX 3×1 → values are 0, 0.5, 1.0 left-to-right.
    // With normalize_values, height_scale=1, world Y == normalized value.
    TEST_F(GaussianSplatFromScalarFieldTest, SplatPositionsMatchFieldValues)
    {
        const ScalarFieldAsset sf = make_gradient_field(3, 1, "test/positions");
        ASSERT_TRUE(sf.valid());

        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/pos_cloud",
                .scalar_field_key = sf.output,
                .height_scale     = 1.0f,
                .step_x           = 1.0f,
                .step_z           = 1.0f,
                .splat_scale      = 0.05f,
                .opacity          = 0.9f,
                .normalize_values = true,
            });

        ASSERT_TRUE(cloud.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const auto handle = library_->gaussian_splats().get_cloud(cloud);
        ASSERT_TRUE(handle.valid());

        const GaussianSplatCloudData* data =
            library_->gaussian_splats().get_cloud_data(handle);
        ASSERT_NE(data, nullptr);
        ASSERT_EQ(data->splat_count(), uint64_t{3});

        // Positions are normalized to the unit footprint (fix-2): GradientX 3x1
        // -> X = 0, 0.5, 1.0 across width=3; Z = 0 (single row); Y = the
        // normalized field value (0, 0.5, 1.0). height_scale / step no longer
        // scale the coordinates -- the scene-node transform does.
        const auto& s0 = data->splats[0];
        const auto& s1 = data->splats[1];
        const auto& s2 = data->splats[2];

        EXPECT_FLOAT_EQ(s0.position[0],  0.0f);
        EXPECT_FLOAT_EQ(s0.position[1],  0.0f);
        EXPECT_FLOAT_EQ(s0.position[2],  0.0f);

        EXPECT_FLOAT_EQ(s1.position[0],  0.5f);
        EXPECT_NEAR    (s1.position[1],  0.5f, 1e-5f);
        EXPECT_FLOAT_EQ(s1.position[2],  0.0f);

        EXPECT_FLOAT_EQ(s2.position[0],  1.0f);
        EXPECT_FLOAT_EQ(s2.position[1],  1.0f);
        EXPECT_FLOAT_EQ(s2.position[2],  0.0f);
    }

    // Threshold: with emit_threshold=0.6 on GradientX 4×1 (values 0, 0.33, 0.66, 1.0),
    // only the two samples with normalized value >= 0.6 should emit splats.
    TEST_F(GaussianSplatFromScalarFieldTest, ThresholdReducesSplatCount)
    {
        const ScalarFieldAsset sf = make_gradient_field(4, 1, "test/threshold_field");
        ASSERT_TRUE(sf.valid());

        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/threshold_cloud",
                .scalar_field_key = sf.output,
                .height_scale     = 1.0f,
                .step_x           = 1.0f,
                .step_z           = 1.0f,
                .splat_scale      = 0.05f,
                .opacity          = 0.9f,
                .normalize_values = true,
                .use_threshold    = true,
                .emit_threshold   = 0.6f,
            });

        ASSERT_TRUE(cloud.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const auto handle = library_->gaussian_splats().get_cloud(cloud);
        ASSERT_TRUE(handle.valid());

        const GaussianSplatCloudData* data =
            library_->gaussian_splats().get_cloud_data(handle);
        ASSERT_NE(data, nullptr);
        ASSERT_TRUE(data->valid());

        // GradientX on width=4: 0.0, 0.33, 0.66, 1.0.
        // Threshold=0.6 skips 0.0 and 0.33 → 2 splats emitted.
        EXPECT_EQ(data->splat_count(), uint64_t{2});
    }

    // Threshold that excludes everything → cloud fails to resolve.
    TEST_F(GaussianSplatFromScalarFieldTest, ThresholdExcludingAllProducesInvalidHandle)
    {
        const ScalarFieldAsset sf = make_gradient_field(3, 1, "test/all_excluded");
        ASSERT_TRUE(sf.valid());

        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/all_excluded_cloud",
                .scalar_field_key = sf.output,
                .height_scale     = 1.0f,
                .step_x           = 1.0f,
                .step_z           = 1.0f,
                .splat_scale      = 0.05f,
                .opacity          = 0.9f,
                .normalize_values = true,
                .use_threshold    = true,
                .emit_threshold   = 2.0f,  // above max normalized value
            });

        ASSERT_TRUE(cloud.valid());  // registration succeeds; compile validates
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        // Compiler produces empty cloud → compile_failed_node → handle invalid.
        const auto handle = library_->gaussian_splats().get_cloud(cloud);
        EXPECT_FALSE(handle.valid());
    }

    // Thresholded bounds should be tight to emitted splats, not the full grid.
    TEST_F(GaussianSplatFromScalarFieldTest, ThresholdedBoundsAreTightToEmittedSplats)
    {
        // GradientX 4×1: values 0.0, 0.33, 0.66, 1.0. Threshold at 0.6 keeps right 2.
        const ScalarFieldAsset sf = make_gradient_field(4, 1, "test/tight_bounds_field");
        ASSERT_TRUE(sf.valid());

        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/tight_bounds_cloud",
                .scalar_field_key = sf.output,
                .height_scale     = 1.0f,
                .step_x           = 1.0f,
                .step_z           = 1.0f,
                .splat_scale      = 0.05f,
                .opacity          = 0.9f,
                .normalize_values = true,
                .use_threshold    = true,
                .emit_threshold   = 0.6f,
            });

        ASSERT_TRUE(cloud.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const auto handle = library_->gaussian_splats().get_cloud(cloud);
        ASSERT_TRUE(handle.valid());

        const GaussianSplatCloudData* data =
            library_->gaussian_splats().get_cloud_data(handle);
        ASSERT_NE(data, nullptr);

        // Full grid: half_x = (4-1)/2 * 1.0 = 1.5. So X runs from -1.5 to +1.5.
        // Emitted: ix=2 (wx=0.5) and ix=3 (wx=1.5). Bounds min[0] should be ~0.5.
        EXPECT_GT(data->bounds.min[0], -1.0f);  // tighter than full left edge
    }

    // Opacity and scale metadata must be present and reasonable.
    TEST_F(GaussianSplatFromScalarFieldTest, OpacityAndScaleMetadataAreSet)
    {
        const ScalarFieldAsset sf = make_gradient_field(2, 2, "test/meta_field");
        ASSERT_TRUE(sf.valid());

        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/meta_cloud",
                .scalar_field_key = sf.output,
                .height_scale     = 1.0f,
                .step_x           = 1.0f,
                .step_z           = 1.0f,
                .splat_scale      = 0.1f,
                .opacity          = 0.9f,
            });

        ASSERT_TRUE(cloud.valid());
        ASSERT_TRUE(library_->commit());
        library_->resolve_all();

        const auto handle = library_->gaussian_splats().get_cloud(cloud);
        ASSERT_TRUE(handle.valid());

        const GaussianSplatCloudData* data =
            library_->gaussian_splats().get_cloud_data(handle);
        ASSERT_NE(data, nullptr);

        // logit(0.9) ≈ 2.197; all splats have the same opacity.
        EXPECT_NEAR(data->opacity_min, data->opacity_max, 1e-5f);
        EXPECT_GT(data->opacity_min, 2.0f);   // logit(0.9) > 2

        // log(0.1) ≈ -2.303; all splats have the same scale.
        EXPECT_NEAR(data->scale_min, data->scale_max, 1e-5f);
        EXPECT_LT(data->scale_min, 0.0f);     // log(any scale < 1) < 0
    }

    // Empty scalar field key is rejected at registration time.
    TEST_F(GaussianSplatFromScalarFieldTest, EmptyScalarFieldKeyReturnsInvalidAsset)
    {
        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/empty_key",
                .scalar_field_key = {},   // invalid
                .height_scale     = 1.0f,
                .splat_scale      = 0.05f,
            });

        EXPECT_FALSE(cloud.valid());
    }

    // Non-positive splat_scale is rejected at registration time.
    TEST_F(GaussianSplatFromScalarFieldTest, NonPositiveSplatScaleReturnsInvalidAsset)
    {
        const ScalarFieldAsset sf = make_gradient_field(2, 2, "test/bad_scale_field");
        ASSERT_TRUE(sf.valid());

        const GaussianSplatCloudAsset cloud =
            library_->gaussian_splats().create_from_scalar_field({
                .name             = "test/bad_scale",
                .scalar_field_key = sf.output,
                .splat_scale      = 0.0f,  // invalid
            });

        EXPECT_FALSE(cloud.valid());
    }

} // namespace wz::engine::assets::test
