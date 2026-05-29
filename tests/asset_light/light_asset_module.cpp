#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

TEST(LightAssetModule, CreatesDirectAndAmbientLightingAssets)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_light_asset_module_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const DirectLightAsset direct =
        assets.lights().create_direct_light({
            .name = "test/sun",
            .kind = DirectLightKind::Directional,
            .color = { 1.0f, 0.9f, 0.75f },
            .intensity = 2.5f,
            .range = 100.0f,
        });
    ASSERT_TRUE(direct.valid());

    const AmbientLightingAsset ambient =
        assets.lights().create_ambient_lighting({
            .name = "test/ambient",
            .mode = AmbientLightingMode::Constant,
            .color = { 0.2f, 0.3f, 0.4f },
            .intensity = 0.35f,
        });
    ASSERT_TRUE(ambient.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const DirectLightHandle direct_handle =
        assets.lights().get_direct_light(direct);
    ASSERT_TRUE(direct_handle.valid());
    const DirectLightData* direct_data =
        assets.lights().get_direct_light_data(direct_handle);
    ASSERT_NE(direct_data, nullptr);
    EXPECT_EQ(direct_data->kind, DirectLightKind::Directional);
    EXPECT_FLOAT_EQ(direct_data->color[1], 0.9f);
    EXPECT_FLOAT_EQ(direct_data->intensity, 2.5f);

    const AmbientLightingHandle ambient_handle =
        assets.lights().get_ambient_lighting(ambient);
    ASSERT_TRUE(ambient_handle.valid());
    const AmbientLightingData* ambient_data =
        assets.lights().get_ambient_lighting_data(ambient_handle);
    ASSERT_NE(ambient_data, nullptr);
    EXPECT_EQ(ambient_data->mode, AmbientLightingMode::Constant);
    EXPECT_FLOAT_EQ(ambient_data->color[2], 0.4f);
    EXPECT_FLOAT_EQ(ambient_data->intensity, 0.35f);
}
