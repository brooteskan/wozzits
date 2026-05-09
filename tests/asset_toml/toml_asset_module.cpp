#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

TEST(TOMLAssetModule, ResolvesValidTOMLFile)
{
    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_toml_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path relative_path = "material.toml";
    const wz::fs::Path full_path = wz::fs::join(root, relative_path);

    ASSERT_EQ(
        wz::fs::write_file_text(
            full_path,
            R"(
                [material]
                name = "brick"
                roughness = 0.8

                [material.textures]
                albedo = "brick_albedo.png"
            )",
            true
        ),
        wz::fs::FileError::None
    );

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        root
    );

    const auto toml = assets.toml().create_toml({
        .name = "material",
        .path = relative_path,
        });

    ASSERT_TRUE(toml.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle = assets.toml().get_toml(toml);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.toml().get_toml_data(handle);

    ASSERT_NE(data, nullptr);
    ASSERT_NE(data->document.root, nullptr);
    EXPECT_EQ(data->document.root->kind, wz::toml::TOMLValueKind::Table);
}

TEST(TOMLAssetModule, RejectsInvalidTOMLFile)
{
    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_toml_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path relative_path = "bad.toml";
    const wz::fs::Path full_path = wz::fs::join(root, relative_path);

    ASSERT_EQ(
        wz::fs::write_file_text(
            full_path,
            R"(
                name = "brick
            )",
            true
        ),
        wz::fs::FileError::None
    );

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        root
    );

    const auto toml = assets.toml().create_toml({
        .name = "bad",
        .path = relative_path,
        });

    ASSERT_TRUE(toml.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_FALSE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.toml().get_toml(toml);
    EXPECT_FALSE(handle.valid());
}

