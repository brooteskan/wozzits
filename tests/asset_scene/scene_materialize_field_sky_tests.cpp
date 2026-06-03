#include "scene_authoring_materialize_test_support.h"

TEST(SceneAuthoringMaterialize, ScalarFieldSourceCreatesScalarFieldAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_scalar_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "scalar_source";
    SceneNodeAsset node = make_scene_node("field");
    node.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralSineWaves,
        .width = 8,
        .height = 4,
        .depth = 1,
        .frequency = 2.0f,
        .amplitude = 0.5f,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].scalar_field_source.has_value());
    const auto field_key =
        scene.nodes[0].scalar_field_source->scalar_field_asset;
    EXPECT_NE(field_key, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto field =
        assets.scalar_fields().get_scalar_field(
            ScalarFieldAsset{ .output = field_key });
    ASSERT_TRUE(field.valid());
    const auto* field_data =
        assets.scalar_fields().get_scalar_field_data(field);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->width, 8u);
    EXPECT_EQ(field_data->height, 4u);
}

TEST(SceneAuthoringMaterialize, SkySurfaceMaterializesScalarFieldDraw)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_sky_scalar_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "sky_scalar";

    SceneNodeAsset field = make_scene_node("sky_field");
    field.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralGradientY,
        .width = 8,
        .height = 4,
        .depth = 1,
    };
    scene.nodes.push_back(std::move(field));

    SceneNodeAsset visual = make_scene_node("sky_visual");
    visual.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::ScalarField,
        .scalar_field_node = "sky_field",
        .exposure = -1.0f,
        .rotation_y_radians = 0.5f,
    };
    scene.nodes.push_back(std::move(visual));

    SceneNodeAsset surface = make_scene_node("sky_surface");
    surface.sky_surface = SceneSkySurfaceAsset{
        .visual_node = "sky_visual",
        .projection = SceneSkyProjection::Sphere,
        .radius = 10.0f,
        .visible_to_camera = true,
    };
    scene.nodes.push_back(std::move(surface));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].scalar_field_source.has_value());
    ASSERT_NE(
        scene.nodes[0].scalar_field_source->scalar_field_asset,
        wz::asset::AssetKey{});
    ASSERT_EQ(scene.sky_draws.size(), 1u);

    const SceneSkyDrawAsset& draw = scene.sky_draws[0];
    EXPECT_EQ(draw.surface_node, "sky_surface");
    EXPECT_EQ(draw.visual_node, "sky_visual");
    EXPECT_EQ(draw.visual_kind, SceneSkyVisualKind::ScalarField);
    EXPECT_EQ(draw.projection, SceneSkyProjection::Sphere);
    EXPECT_FLOAT_EQ(draw.radius, 10.0f);
    EXPECT_FLOAT_EQ(draw.exposure, -1.0f);
    EXPECT_FLOAT_EQ(draw.rotation_y_radians, 0.5f);
    EXPECT_NE(draw.scalar_field_asset, wz::asset::AssetKey{});
    EXPECT_EQ(
        draw.scalar_field_asset,
        scene.nodes[0].scalar_field_source->scalar_field_asset);
}

TEST(SceneAuthoringMaterialize, SkySurfaceMaterializesGradientDraw)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{
        device,
        logger,
        wz::fs::temp_directory_path(),
    };

    SceneAssetData scene{};
    scene.name = "sky_gradient";
    SceneNodeAsset node = make_scene_node("sky");
    node.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::Gradient,
        .gradient_top_color = { 0.05f, 0.15f, 0.4f },
        .gradient_bottom_color = { 0.8f, 0.9f, 1.0f },
        .exposure = 1.25f,
        .rotation_x_radians = 0.125f,
    };
    node.sky_surface = SceneSkySurfaceAsset{
        .projection = SceneSkyProjection::Sphere,
        .radius = 25.0f,
        .visible_to_camera = true,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.sky_draws.size(), 1u);

    const SceneSkyDrawAsset& draw = scene.sky_draws[0];
    EXPECT_EQ(draw.visual_kind, SceneSkyVisualKind::Gradient);
    EXPECT_FLOAT_EQ(draw.gradient_top_color[2], 0.4f);
    EXPECT_FLOAT_EQ(draw.gradient_bottom_color[0], 0.8f);
    EXPECT_FLOAT_EQ(draw.exposure, 1.25f);
    EXPECT_FLOAT_EQ(draw.rotation_x_radians, 0.125f);
    EXPECT_FLOAT_EQ(draw.radius, 25.0f);
}

TEST(SceneAuthoringMaterialize, SkySurfaceMaterializesTexturePathDraw)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{
        device,
        logger,
        wz::fs::temp_directory_path(),
    };

    SceneAssetData scene{};
    scene.name = "sky_texture";
    SceneNodeAsset node = make_scene_node("sky");
    node.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::EquirectangularTexture,
        .texture_path = "skies/studio.exr",
        .texture_format = HDRIEnvironmentFormat::OpenEXR,
        .exposure = 0.5f,
        .rotation_z_radians = -0.25f,
    };
    node.sky_surface = SceneSkySurfaceAsset{};
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.sky_draws.size(), 1u);

    const SceneSkyDrawAsset& draw = scene.sky_draws[0];
    EXPECT_EQ(draw.visual_kind, SceneSkyVisualKind::EquirectangularTexture);
    EXPECT_EQ(draw.texture_path, "skies/studio.exr");
    EXPECT_EQ(draw.texture_format, HDRIEnvironmentFormat::OpenEXR);
    EXPECT_FLOAT_EQ(draw.exposure, 0.5f);
    EXPECT_FLOAT_EQ(draw.rotation_z_radians, -0.25f);
}

TEST(SceneAuthoringMaterialize, SkyScalarFieldCanUseSameNodeSource)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_sky_scalar_same_node_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "sky_scalar_same_node";
    SceneNodeAsset node = make_scene_node("sky_field_visual");
    node.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralGradientY,
        .width = 8,
        .height = 4,
        .depth = 1,
    };
    node.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::ScalarField,
    };
    node.sky_surface = SceneSkySurfaceAsset{};
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.sky_draws.size(), 1u);
    EXPECT_EQ(scene.sky_draws[0].visual_kind, SceneSkyVisualKind::ScalarField);
    EXPECT_NE(scene.sky_draws[0].scalar_field_asset, wz::asset::AssetKey{});
    EXPECT_EQ(
        scene.sky_draws[0].scalar_field_asset,
        scene.nodes[0].scalar_field_source->scalar_field_asset);
}

TEST(SceneAuthoringMaterialize, SkySurfaceSkipsNoneVisual)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{
        device,
        logger,
        wz::fs::temp_directory_path(),
    };

    SceneAssetData scene{};
    scene.name = "sky_none";
    SceneNodeAsset node = make_scene_node("sky");
    node.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::None,
    };
    node.sky_surface = SceneSkySurfaceAsset{};
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_TRUE(scene.sky_draws.empty());
}

TEST(SceneAuthoringMaterialize, VectorFieldSourceCreatesVectorFieldAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_vector_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path rel_path{ "normal.raw" };
    const wz::fs::Path full_path = wz::fs::join(root, rel_path);
    ASSERT_TRUE(write_raw_f32(
        full_path,
        {
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f,
        }));

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "vector_source";
    SceneNodeAsset node = make_scene_node("normal");
    node.vector_field_source = SceneVectorFieldSourceAsset{
        .kind = SceneVectorFieldSourceKind::RawF32,
        .path = rel_path,
        .width = 2,
        .height = 1,
        .depth = 1,
        .components_per_channel = 3,
        .channels = { VectorFieldChannelDesc{ .name = "normal" } },
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].vector_field_source.has_value());
    const auto field_key =
        scene.nodes[0].vector_field_source->vector_field_asset;
    EXPECT_NE(field_key, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto field =
        assets.vector_fields().get_vector_field(
            VectorFieldAsset{ .output = field_key });
    ASSERT_TRUE(field.valid());
    const auto* field_data =
        assets.vector_fields().get_vector_field_data(field);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->width, 2u);
    EXPECT_EQ(field_data->height, 1u);
    EXPECT_EQ(field_data->components_per_channel, 3u);
    ASSERT_EQ(field_data->channels.size(), 1u);
    EXPECT_EQ(field_data->channels[0].name, "normal");
}

TEST(SceneAuthoringMaterialize, SkyVectorFieldCanUseSameNodeSource)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_sky_vector_same_node_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path rel_path{ "flow.raw" };
    ASSERT_TRUE(write_raw_f32(
        wz::fs::join(root, rel_path),
        {
            1.0f, 0.0f,
            0.0f, 1.0f,
        }));

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "sky_vector_same_node";
    SceneNodeAsset node = make_scene_node("sky_vector_visual");
    node.vector_field_source = SceneVectorFieldSourceAsset{
        .kind = SceneVectorFieldSourceKind::RawF32,
        .path = rel_path,
        .width = 2,
        .height = 1,
        .depth = 1,
        .components_per_channel = 2,
        .channels = { VectorFieldChannelDesc{ .name = "flow" } },
    };
    node.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::VectorField,
        .exposure = 0.75f,
    };
    node.sky_surface = SceneSkySurfaceAsset{};
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.sky_draws.size(), 1u);
    EXPECT_EQ(scene.sky_draws[0].visual_kind, SceneSkyVisualKind::VectorField);
    EXPECT_NE(scene.sky_draws[0].vector_field_asset, wz::asset::AssetKey{});
    EXPECT_EQ(
        scene.sky_draws[0].vector_field_asset,
        scene.nodes[0].vector_field_source->vector_field_asset);
    EXPECT_FLOAT_EQ(scene.sky_draws[0].exposure, 0.75f);
}

