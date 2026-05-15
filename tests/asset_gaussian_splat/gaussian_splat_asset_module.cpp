#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

TEST(GaussianSplatAssetModule, ResolvesProceduralCloud)
{
    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_gaussian_splat_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        root
    );

    const auto cloud = assets.gaussian_splats().create_procedural_cloud({
        .name = "debug_sphere",
        .count = 64,
        .radius = 2.0f,
        .splat_scale = 0.1f,
        });

    ASSERT_TRUE(cloud.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.gaussian_splats().get_cloud(cloud);

    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.handle.type, wz::engine::assets::kAssetTypeGaussianSplatCloud);

    const auto* data = assets.gaussian_splats().get_cloud_data(handle);

    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->valid());
    EXPECT_EQ(data->splat_count(), 64u);

    EXPECT_LE(data->bounds_min[0], -2.0f);
    EXPECT_GE(data->bounds_max[0], 2.0f);
}

TEST(GaussianSplatCloudTable, RejectsWrongHandleType)
{
    wz::engine::assets::GaussianSplatCloudTable table;

    wz::engine::assets::GaussianSplatCloudData data;
    data.splats.push_back({});

    const auto handle = table.add(std::move(data));
    ASSERT_TRUE(handle.valid());

    wz::asset::ResourceHandle wrong = handle;
    wrong.type = wz::asset::AssetType::Mesh;

    EXPECT_EQ(table.get(wrong), nullptr);
}