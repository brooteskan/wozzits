#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

TEST(JSONAssetModule, ResolvesValidJSONFile)
{
    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_json_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path relative_path = "material.json";
    const wz::fs::Path full_path = wz::fs::join(root, relative_path);

    ASSERT_EQ(
        wz::fs::write_file_text(
            full_path,
            R"({
                "name": "brick",
                "roughness": 0.8,
                "textures": {
                    "albedo": "brick_albedo.png"
                }
            })",
            true
        ),
        wz::fs::FileError::None
    );

    wz::Logger logger;

    // Use your existing test GPU device setup here if EngineAssetLibrary
    // requires a real device reference.
    //
    // If existing asset tests already have a helper, use that instead.
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets(
        device,
        logger,
        root
    );

    const auto json = assets.json().create_json({
        .name = "material",
        .path = relative_path,
        });

    ASSERT_TRUE(json.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.resolved_count, 2u);

    const auto handle = assets.json().get_json(json);

    ASSERT_TRUE(handle.valid());

    const auto* data = assets.json().get_json_data(handle);

    ASSERT_NE(data, nullptr);
    ASSERT_NE(data->document.root, nullptr);
    EXPECT_EQ(data->document.root->kind, wz::json::JSONValueKind::Object);
}

TEST(JSONAssetModule, RejectsInvalidJSONFile)
{
    const wz::fs::Path root =
        wz::fs::join(wz::fs::temp_directory_path(),
            "wozzits_json_asset_tests");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path relative_path = "bad.json";
    const wz::fs::Path full_path = wz::fs::join(root, relative_path);

    ASSERT_EQ(
        wz::fs::write_file_text(
            full_path,
            R"({ "name": "brick", )",
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

    const auto json = assets.json().create_json({
        .name = "bad",
        .path = relative_path,
        });

    ASSERT_TRUE(json.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();

    EXPECT_FALSE(report.ok());
    EXPECT_EQ(report.resolved_count, 1u);

    const auto handle = assets.json().get_json(json);
    EXPECT_FALSE(handle.valid());
}

