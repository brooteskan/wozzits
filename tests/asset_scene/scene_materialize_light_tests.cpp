#include "scene_authoring_materialize_test_support.h"

TEST(SceneAuthoringMaterialize, LightComponentsCreateLightingAssets)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_light_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const wz::fs::Path hdri_path = wz::fs::join(root, "studio.hdr");
    const std::vector<uint8_t> hdri_bytes{
        '#', '?', 'R', 'A', 'D', 'I', 'A', 'N', 'C', 'E', '\n'
    };
    ASSERT_EQ(
        wz::fs::write_file(hdri_path, hdri_bytes, true),
        wz::fs::FileError::None);

    SceneAssetData scene{};
    scene.name = "light_sources";

    SceneNodeAsset fill = make_scene_node("fill_light");
    fill.direct_light_source = SceneDirectLightSourceAsset{
        .kind = DirectLightKind::Directional,
        .color = { 0.5f, 0.6f, 1.0f },
        .intensity = 1.0f,
        .range = 1000.0f,
    };
    scene.nodes.push_back(std::move(fill));

    SceneNodeAsset sun = make_scene_node("sun");
    sun.local.translation[1] = 8.0f;
    sun.direct_light_source = SceneDirectLightSourceAsset{
        .kind = DirectLightKind::Directional,
        .color = { 1.0f, 0.95f, 0.8f },
        .intensity = 3.0f,
        .range = 1000.0f,
    };
    scene.nodes.push_back(std::move(sun));

    SceneNodeAsset ambient = make_scene_node("sky_ambient");
    ambient.ambient_lighting = SceneAmbientLightingAsset{
        .mode = AmbientLightingMode::Constant,
        .color = { 0.25f, 0.35f, 0.5f },
        .intensity = 0.4f,
    };
    ambient.hdri_environment = SceneHDRIEnvironmentAsset{
        .path = "studio.hdr",
        .format = HDRIEnvironmentFormat::RadianceHDR,
        .exposure = 0.25f,
        .lighting_intensity = 0.7f,
        .reflection_intensity = 0.5f,
        .background_intensity = 0.0f,
        .dominant_light_confidence = 0.6f,
    };
    scene.nodes.push_back(std::move(ambient));

    SceneNodeAsset terrain = make_scene_node("terrain");
    terrain.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::Surface,
        .depth_test = true,
        .depth_write = true,
        .directional_light_node = "sun",
        .ambient_light_node = "sky_ambient",
    };
    scene.nodes.push_back(std::move(terrain));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].direct_light_source.has_value());
    ASSERT_TRUE(scene.nodes[1].direct_light_source.has_value());
    ASSERT_TRUE(scene.nodes[2].ambient_lighting.has_value());
    ASSERT_TRUE(scene.nodes[2].hdri_environment.has_value());
    EXPECT_NE(
        scene.nodes[1].direct_light_source->light_asset,
        wz::asset::AssetKey{});
    EXPECT_NE(
        scene.nodes[2].ambient_lighting->lighting_asset,
        wz::asset::AssetKey{});
    EXPECT_NE(
        scene.nodes[2].hdri_environment->environment_asset,
        wz::asset::AssetKey{});
    ASSERT_EQ(scene.lights.size(), 3u);
    EXPECT_EQ(scene.lights[0].node_id, "sun");
    EXPECT_EQ(scene.lights[0].light.type, wz::scene::LightType::Directional);
    EXPECT_FLOAT_EQ(scene.lights[0].light.intensity, 3.0f);
    EXPECT_EQ(scene.lights[1].node_id, "sky_ambient");
    EXPECT_EQ(scene.lights[1].light.type, wz::scene::LightType::Ambient);
    EXPECT_FLOAT_EQ(scene.lights[1].light.intensity, 0.4f);
    EXPECT_EQ(scene.lights[2].node_id, "fill_light");

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const DirectLightHandle direct =
        assets.lights().get_direct_light(DirectLightAsset{
            .output = scene.nodes[1].direct_light_source->light_asset,
        });
    ASSERT_TRUE(direct.valid());
    const DirectLightData* direct_data =
        assets.lights().get_direct_light_data(direct);
    ASSERT_NE(direct_data, nullptr);
    EXPECT_EQ(direct_data->kind, DirectLightKind::Directional);

    const AmbientLightingHandle ambient_handle =
        assets.lights().get_ambient_lighting(AmbientLightingAsset{
            .output = scene.nodes[2].ambient_lighting->lighting_asset,
        });
    ASSERT_TRUE(ambient_handle.valid());
    const AmbientLightingData* ambient_data =
        assets.lights().get_ambient_lighting_data(ambient_handle);
    ASSERT_NE(ambient_data, nullptr);
    EXPECT_FLOAT_EQ(ambient_data->intensity, 0.4f);

    const HDRIEnvironmentHandle hdri_handle =
        assets.lights().get_hdri_environment(HDRIEnvironmentAsset{
            .output = scene.nodes[2].hdri_environment->environment_asset,
        });
    ASSERT_TRUE(hdri_handle.valid());
    const HDRIEnvironmentData* hdri_data =
        assets.lights().get_hdri_environment_data(hdri_handle);
    ASSERT_NE(hdri_data, nullptr);
    EXPECT_EQ(hdri_data->format, HDRIEnvironmentFormat::RadianceHDR);
    EXPECT_FLOAT_EQ(hdri_data->lighting_intensity, 0.7f);
    EXPECT_FLOAT_EQ(hdri_data->dominant_light_confidence, 0.6f);
}

