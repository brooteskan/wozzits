#include <gtest/gtest.h>

#include <asset/types.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <gpu/gpu.h>
#include <logging/logger.h>

#ifndef WZ_TEST_FIXTURE_DIR
#define WZ_TEST_FIXTURE_DIR "tests/fixtures"
#endif

TEST(GaussianSplatPLYAsset, ImportsAsciiPLYThroughAssetGraph)
{
    wz::gpu::Device device{};
    wz::Logger logger{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        WZ_TEST_FIXTURE_DIR);

    const wz::asset::AssetKey file_key =
        assets.files().register_file_node(
            "splats/three_splats_ascii.ply",
            wz::engine::assets::kRawFileSchema,
            wz::engine::assets::kAssetTypeRawFile);

    ASSERT_FALSE(file_key == wz::asset::AssetKey{});

    auto cloud = assets.gaussian_splats().create_from_ply({
        .name = "three_splats_ascii",
        .source_file = file_key,
        });

    ASSERT_TRUE(cloud.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    if (!report.ok()) {
        ADD_FAILURE() << "resolve_all failed with "
            << report.failures.size()
            << " failure(s)";
    }

    ASSERT_TRUE(report.ok());

    const auto handle = assets.gaussian_splats().get_cloud(cloud);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.gaussian_splats().get_cloud_data(handle);
    ASSERT_NE(data, nullptr);

    ASSERT_TRUE(data->valid());
    EXPECT_EQ(data->splat_count(), uint64_t{3});

    EXPECT_FLOAT_EQ(data->splats[0].position[0], 0.0f);
    EXPECT_FLOAT_EQ(data->splats[0].position[1], 0.0f);
    EXPECT_FLOAT_EQ(data->splats[0].position[2], 0.0f);

    EXPECT_FLOAT_EQ(data->splats[1].position[0], 1.0f);
    EXPECT_FLOAT_EQ(data->splats[1].position[1], 0.0f);
    EXPECT_FLOAT_EQ(data->splats[1].position[2], 0.0f);

    EXPECT_FLOAT_EQ(data->splats[2].position[0], 0.0f);
    EXPECT_FLOAT_EQ(data->splats[2].position[1], 1.0f);
    EXPECT_FLOAT_EQ(data->splats[2].position[2], 0.0f);

    // color_dc stores raw SH DC coefficients as written in the PLY file.
    EXPECT_FLOAT_EQ(data->splats[0].color_dc[0], 1.0f);
    EXPECT_FLOAT_EQ(data->splats[0].color_dc[1], 0.0f);
    EXPECT_FLOAT_EQ(data->splats[0].color_dc[2], 0.0f);

    ASSERT_TRUE(data->bounds.valid);

    EXPECT_LE(data->bounds.min[0], 0.0f);
    EXPECT_LE(data->bounds.min[1], 0.0f);
    EXPECT_LE(data->bounds.min[2], 0.0f);

    EXPECT_GE(data->bounds.max[0], 1.0f);
    EXPECT_GE(data->bounds.max[1], 1.0f);
    EXPECT_GE(data->bounds.max[2], 0.0f);
}