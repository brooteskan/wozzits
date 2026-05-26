// tests/asset_gaussian_splat/terrain_splat_from_gaea_r32_compiler.cpp
//
// Tests for the TerrainSplatFromGaeaR32 recipe compiler.
//
// Each test writes a small synthetic .r32 + .json pair into a temp directory,
// registers the recipe, commits + resolves, and inspects the result.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/gaussian_splat/gaussian_splat_cloud.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wz::engine::assets::test
{
    namespace stdfs = std::filesystem;

    class TerrainSplatFromGaeaR32Test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            temp_dir_ = wz::fs::Path{
                (stdfs::temp_directory_path()
                    / "wz_terrain_splat_recipe_tests").string()
            };
            stdfs::create_directories(temp_dir_);

            library_ = std::make_unique<EngineAssetLibrary>(
                null_device_,
                logger_,
                temp_dir_);
        }

        void TearDown() override
        {
            library_.reset();
            stdfs::remove_all(temp_dir_);
        }

        // Write raw bytes to {temp_dir}/{relative_path}.
        void write_bytes(
            const std::string& relative_path,
            const void* data,
            size_t size)
        {
            const stdfs::path full =
                stdfs::path(temp_dir_) / stdfs::path(relative_path);
            stdfs::create_directories(full.parent_path());
            std::ofstream f(full, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(f.is_open()) << "failed to open " << full.string();
            f.write(static_cast<const char*>(data), size);
        }

        // Build a .r32 from a float vector and write it.
        void write_r32(
            const std::string& relative_path,
            const std::vector<float>& values)
        {
            write_bytes(
                relative_path,
                values.data(),
                values.size() * sizeof(float));
        }

        // Write a JSON sidecar file (no escaping — caller controls content).
        void write_json(
            const std::string& relative_path,
            const std::string& json)
        {
            write_bytes(relative_path, json.data(), json.size());
        }

        // Register the recipe and run a full commit + resolve_all.
        // Returns the recipe asset (which may have valid=true even if the
        // compile failed — query the cloud below to verify).
        GaussianSplatCloudAsset register_and_resolve(
            const std::string& r32_relative,
            const std::string& sidecar_relative)
        {
            const wz::asset::AssetKey r32_key =
                library_->files().register_file_node(
                    r32_relative, kRawFileSchema, kAssetTypeRawFile);

            const auto sidecar_json =
                library_->json().create_json({
                    .name = "sidecar",
                    .path = sidecar_relative,
                });

            const auto recipe = library_->gaussian_splats()
                .create_terrain_splat_from_gaea_r32({
                    .name              = "test/terrain_recipe",
                    .r32_file_key      = r32_key,
                    .sidecar_json_key  = sidecar_json.output,
                });

            EXPECT_TRUE(library_->commit());
            library_->resolve_all();

            return recipe;
        }

        // After a successful compile, fetch the resulting cloud data.
        const GaussianSplatCloudData* fetch_cloud_data(
            const GaussianSplatCloudAsset& recipe)
        {
            const auto handle = library_->gaussian_splats().get_cloud(recipe);
            if (!handle.valid())
                return nullptr;
            return library_->gaussian_splats().get_cloud_data(handle);
        }

        wz::gpu::Device   null_device_{};
        wz::Logger        logger_{};
        wz::fs::Path      temp_dir_{};
        std::unique_ptr<EngineAssetLibrary> library_;
    };


    // ─── Happy paths ────────────────────────────────────────────────────────

    // Square inference: 4x4 = 16 samples; no width/height in sidecar.
    TEST_F(TerrainSplatFromGaeaR32Test, SquareDimensionInferenceWorks)
    {
        std::vector<float> heights(16);
        for (int i = 0; i < 16; ++i)
            heights[i] = static_cast<float>(i) * 0.1f;

        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_x": 1.0, "step_z": 1.0})");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        const auto* cloud = fetch_cloud_data(recipe);
        ASSERT_NE(cloud, nullptr);
        EXPECT_TRUE(cloud->valid());
        // 4x4 = 16 cells emitted at subsample_step=1 (default).
        EXPECT_EQ(cloud->splat_count(), 16u);
        EXPECT_TRUE(cloud->bounds.valid);
    }

    // Explicit non-square width/height override.
    TEST_F(TerrainSplatFromGaeaR32Test, ExplicitWidthHeightOverrideWorks)
    {
        // 8 wide × 2 tall = 16 samples.
        std::vector<float> heights(16, 0.5f);

        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_x": 1.0, "step_z": 1.0,
                "width": 8, "height": 2})");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        const auto* cloud = fetch_cloud_data(recipe);
        ASSERT_NE(cloud, nullptr);
        EXPECT_TRUE(cloud->valid());
        // 8x2 = 16 cells.
        EXPECT_EQ(cloud->splat_count(), 16u);
    }

    // Optional params propagate.  subsample_step = 2 on a 4x4 grid emits
    // 2x2 = 4 cells.
    TEST_F(TerrainSplatFromGaeaR32Test, SubsampleStepReducesCloud)
    {
        std::vector<float> heights(16, 0.0f);
        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_x": 1.0, "step_z": 1.0,
                "subsample_step": 2})");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");
        ASSERT_TRUE(recipe.valid());

        const auto* cloud = fetch_cloud_data(recipe);
        ASSERT_NE(cloud, nullptr);
        // 4x4 with subsample_step=2: ceil(4/2) * ceil(4/2) = 2*2 = 4 cells.
        EXPECT_EQ(cloud->splat_count(), 4u);
    }


    // ─── Negative paths ─────────────────────────────────────────────────────
    //
    // For each rejection case we expect commit() to succeed (the recipe
    // registers fine) but resolve_all to NOT produce a compiled cloud
    // (handle invalid → cloud data null).

    // .r32 byte count not a multiple of 4 — invalid float buffer.
    TEST_F(TerrainSplatFromGaeaR32Test, RejectsNonMultipleOfFourBytes)
    {
        const uint8_t junk[15] = { 0 };
        write_bytes("terrain.r32", junk, sizeof(junk));
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_x": 1.0, "step_z": 1.0})");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        // Recipe key registers fine; compile fails → no cloud data.
        ASSERT_TRUE(recipe.valid());
        EXPECT_EQ(fetch_cloud_data(recipe), nullptr);
    }

    // Sample count not a perfect square AND no width/height override.
    TEST_F(TerrainSplatFromGaeaR32Test, RejectsNonSquareWithoutOverride)
    {
        // 10 samples (10 floats = 40 bytes).  Not a perfect square.
        std::vector<float> heights(10, 0.0f);
        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_x": 1.0, "step_z": 1.0})");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        EXPECT_EQ(fetch_cloud_data(recipe), nullptr);
    }

    // width * height != sample_count when both are explicit.
    TEST_F(TerrainSplatFromGaeaR32Test, RejectsExplicitWidthHeightMismatch)
    {
        std::vector<float> heights(16, 0.0f);  // 16 samples
        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_x": 1.0, "step_z": 1.0,
                "width": 5, "height": 3})");  // 5*3 = 15, mismatch

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        EXPECT_EQ(fetch_cloud_data(recipe), nullptr);
    }

    // Only one of width/height specified is an error (both-or-neither).
    TEST_F(TerrainSplatFromGaeaR32Test, RejectsPartialDimensionOverride)
    {
        std::vector<float> heights(16, 0.0f);
        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_x": 1.0, "step_z": 1.0,
                "width": 4})");  // height missing

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        EXPECT_EQ(fetch_cloud_data(recipe), nullptr);
    }

    // Required field 'height_scale' missing.
    TEST_F(TerrainSplatFromGaeaR32Test, RejectsMissingHeightScale)
    {
        std::vector<float> heights(16, 0.0f);
        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"step_x": 1.0, "step_z": 1.0})");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        EXPECT_EQ(fetch_cloud_data(recipe), nullptr);
    }

    // Required field 'step_x' missing.
    TEST_F(TerrainSplatFromGaeaR32Test, RejectsMissingStepX)
    {
        std::vector<float> heights(16, 0.0f);
        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_z": 1.0})");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        EXPECT_EQ(fetch_cloud_data(recipe), nullptr);
    }

    // Required field 'step_z' missing.
    TEST_F(TerrainSplatFromGaeaR32Test, RejectsMissingStepZ)
    {
        std::vector<float> heights(16, 0.0f);
        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            R"({"height_scale": 1.0, "step_x": 1.0})");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        EXPECT_EQ(fetch_cloud_data(recipe), nullptr);
    }

    // Malformed JSON — not parseable.
    TEST_F(TerrainSplatFromGaeaR32Test, RejectsMalformedJson)
    {
        std::vector<float> heights(16, 0.0f);
        write_r32("terrain.r32", heights);
        write_json("terrain.r32.json",
            "{ not valid json at all }");

        const auto recipe = register_and_resolve(
            "terrain.r32", "terrain.r32.json");

        ASSERT_TRUE(recipe.valid());
        EXPECT_EQ(fetch_cloud_data(recipe), nullptr);
    }
}
