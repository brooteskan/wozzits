#include "scene_authoring_materialize_test_support.h"

TEST(SceneAuthoringMaterialize, TerrainMeshSourceSupportsDirectAndChildMeshAssets)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_terrain_mesh_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const MeshAsset direct_mesh =
        assets.meshes().create_procedural_mesh({
            .name = "terrain/direct_mesh",
            .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(direct_mesh.valid());

    SceneAssetData scene{};
    scene.name = "terrain_mesh_sources";

    SceneNodeAsset direct = make_scene_node("direct_terrain");
    direct.terrain = SceneTerrainAsset{};
    direct.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = direct_mesh.output,
    };
    scene.nodes.push_back(std::move(direct));

    SceneNodeAsset parent = make_scene_node("child_terrain");
    parent.terrain = SceneTerrainAsset{};
    parent.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::SceneNode,
        .source_node = "child_mesh",
    };
    scene.nodes.push_back(std::move(parent));

    SceneNodeAsset child = make_scene_node("child_mesh");
    child.parent_id = "child_terrain";
    child.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralQuad,
    };
    scene.nodes.push_back(std::move(child));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_EQ(scene.nodes[0].terrain_mesh_source->mesh_asset, direct_mesh.output);
    EXPECT_NE(scene.nodes[0].terrain->terrain_asset, wz::asset::AssetKey{});
    EXPECT_NE(scene.nodes[1].terrain_mesh_source->mesh_asset, wz::asset::AssetKey{});
    EXPECT_NE(scene.nodes[1].terrain->terrain_asset, wz::asset::AssetKey{});
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[1].renderable_asset.has_value());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
    EXPECT_TRUE(
        assets.terrains()
            .get_terrain(TerrainAsset{
                .output = scene.nodes[0].terrain->terrain_asset,
            })
            .valid());
    EXPECT_TRUE(
        assets.terrains()
            .get_terrain(TerrainAsset{
                .output = scene.nodes[1].terrain->terrain_asset,
            })
            .valid());

    const auto direct_renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(direct_renderable.valid());
    const auto* direct_renderable_data =
        assets.renderables().get_renderable_data(direct_renderable);
    ASSERT_NE(direct_renderable_data, nullptr);
    EXPECT_EQ(
        direct_renderable_data->program,
        BuiltinRenderProgram::TerrainMeshSurface);
    EXPECT_EQ(direct_renderable_data->domain, RenderDomain::Opaque);
    EXPECT_EQ(direct_renderable_data->companion_asset,
        scene.nodes[0].terrain->terrain_asset);
}

TEST(SceneAuthoringMaterialize, TerrainRenderStyleSelectsRenderablePath)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_terrain_render_style_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const MeshAsset mesh =
        assets.meshes().create_procedural_mesh({
            .name = "terrain/render_style_mesh",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    SceneAssetData scene{};
    scene.name = "terrain_render_styles";

    SceneNodeAsset debug_node = make_scene_node("debug_terrain");
    debug_node.terrain = SceneTerrainAsset{};
    debug_node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = mesh.output,
    };
    debug_node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::DebugWireframe,
        .depth_test = true,
        .depth_write = true,
    };
    scene.nodes.push_back(std::move(debug_node));

    SceneNodeAsset none_node = make_scene_node("hidden_render_terrain");
    none_node.terrain = SceneTerrainAsset{};
    none_node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = mesh.output,
    };
    none_node.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::None,
    };
    scene.nodes.push_back(std::move(none_node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    EXPECT_FALSE(scene.nodes[1].renderable_asset.has_value());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* data = assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->program, BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_EQ(data->domain, RenderDomain::Debug);
    EXPECT_TRUE((data->policy_flags & RenderPolicy_Wireframe) != 0);
}

TEST(SceneAuthoringMaterialize, TerrainRenderStyleResolvesHDRILighting)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_terrain_hdri_lighting_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::vector<uint8_t> hdri_bytes{
        '#', '?', 'R', 'A', 'D', 'I', 'A', 'N', 'C', 'E', '\n'
    };
    ASSERT_EQ(
        wz::fs::write_file(wz::fs::join(root, "ridge.hdr"), hdri_bytes, true),
        wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const MeshAsset mesh =
        assets.meshes().create_procedural_mesh({
            .name = "terrain/hdri_lit_mesh",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    SceneAssetData scene{};
    scene.name = "terrain_hdri_lighting";

    SceneNodeAsset environment = make_scene_node("sky");
    environment.hdri_environment = SceneHDRIEnvironmentAsset{
        .path = "ridge.hdr",
        .lighting_intensity = 0.5f,
        .dominant_light_direction = { 0.0f, -0.5f, 0.8660254f },
        .dominant_light_color = { 1.0f, 0.9f, 0.75f },
        .dominant_light_intensity = 2.0f,
        .dominant_light_confidence = 0.8f,
    };
    scene.nodes.push_back(std::move(environment));

    SceneNodeAsset terrain = make_scene_node("terrain");
    terrain.terrain = SceneTerrainAsset{};
    terrain.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = mesh.output,
    };
    terrain.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::Surface,
        .lighting_source = SceneTerrainLightingSource::EnvironmentNode,
        .environment_node = "sky",
        .ambient_strength = 0.6f,
        .sky_visibility_strength = 0.75f,
        .normal_lighting_strength = 0.5f,
        .terrain_bounce_strength = 0.1f,
        .target_pixels_per_triangle = 7.0f,
        .visual_chunk_count = 1024u,
    };
    scene.nodes.push_back(std::move(terrain));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[1].renderable_asset.has_value());
    EXPECT_TRUE(scene.lights.empty());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto terrain_handle = assets.terrains().get_terrain(
        TerrainAsset{ .output = scene.nodes[1].terrain->terrain_asset });
    ASSERT_TRUE(terrain_handle.valid());
    const auto* terrain_data =
        assets.terrains().get_terrain_data(terrain_handle);
    ASSERT_NE(terrain_data, nullptr);
    EXPECT_EQ(terrain_data->mesh_visual_chunk_count, 1024u);

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[1].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* data = assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->program, BuiltinRenderProgram::TerrainMeshSurface);
    EXPECT_EQ(data->terrain_lighting.mode, TerrainLightingMode::HDRIEnvironment);
    EXPECT_FLOAT_EQ(data->terrain_lighting.environment_color[1], 0.9f);
    EXPECT_FLOAT_EQ(data->terrain_lighting.environment_intensity, 0.3f);
    EXPECT_FLOAT_EQ(data->terrain_lighting.dominant_light_intensity, 0.5f);
    EXPECT_FLOAT_EQ(data->terrain_lighting.sky_visibility_strength, 0.75f);
    EXPECT_FLOAT_EQ(data->terrain_lighting.terrain_bounce_strength, 0.1f);
    EXPECT_FLOAT_EQ(data->terrain_target_pixels_per_triangle, 7.0f);
}

TEST(SceneAuthoringMaterialize, TerrainHDRILightingCanBeDerivedFromEXR)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_terrain_exr_lighting_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const wz::fs::Path exr_path = wz::fs::join(root, "derived_sky.exr");
    const std::vector<float> rgba{
        0.1f, 0.1f, 0.1f, 1.0f,
        4.0f, 3.0f, 2.0f, 1.0f,
        0.2f, 0.2f, 0.3f, 1.0f,
        0.1f, 0.1f, 0.2f, 1.0f,
    };
    const wz::fs::Buffer exr_bytes = make_test_exr_bytes(rgba);
    ASSERT_FALSE(exr_bytes.empty());
    ASSERT_EQ(
        wz::fs::write_file(exr_path, exr_bytes, true),
        wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const MeshAsset mesh =
        assets.meshes().create_procedural_mesh({
            .name = "terrain/exr_lit_mesh",
            .kind = ProceduralMeshKind::Quad,
        });
    ASSERT_TRUE(mesh.valid());

    SceneAssetData scene{};
    scene.name = "terrain_exr_lighting";

    SceneNodeAsset environment = make_scene_node("sky");
    environment.hdri_environment = SceneHDRIEnvironmentAsset{
        .path = exr_path,
        .format = HDRIEnvironmentFormat::OpenEXR,
        .lighting_intensity = 0.5f,
    };
    scene.nodes.push_back(std::move(environment));

    SceneNodeAsset terrain = make_scene_node("terrain");
    terrain.terrain = SceneTerrainAsset{};
    terrain.terrain_mesh_source = SceneTerrainMeshSourceAsset{
        .mode = SceneTerrainMeshSourceMode::MeshAsset,
        .mesh_asset = mesh.output,
    };
    terrain.terrain_render_style = SceneTerrainRenderStyleAsset{
        .path = SceneTerrainRenderPath::Surface,
        .lighting_source = SceneTerrainLightingSource::EnvironmentNode,
        .environment_node = "sky",
        .ambient_strength = 0.6f,
        .normal_lighting_strength = 0.5f,
    };
    scene.nodes.push_back(std::move(terrain));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].hdri_environment.has_value());
    ASSERT_TRUE(scene.nodes[1].renderable_asset.has_value());
    EXPECT_GT(
        scene.nodes[0].hdri_environment->environment_light_intensity,
        0.0f);
    EXPECT_GT(
        scene.nodes[0].hdri_environment->dominant_light_intensity,
        0.0f);
    const float unrotated_x =
        scene.nodes[0].hdri_environment->dominant_light_direction[0];
    const float unrotated_z =
        scene.nodes[0].hdri_environment->dominant_light_direction[2];

    scene.nodes[0].hdri_environment->rotation_y_radians = 1.57079632679f;
    const auto rotated_report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(rotated_report.ok) << rotated_report.error;
    EXPECT_NE(
        scene.nodes[0].hdri_environment->dominant_light_direction[0],
        unrotated_x);
    EXPECT_NE(
        scene.nodes[0].hdri_environment->dominant_light_direction[2],
        unrotated_z);
    EXPECT_NEAR(
        scene.nodes[0].hdri_environment->dominant_light_direction[0],
        unrotated_z,
        0.0001f);
    EXPECT_NEAR(
        scene.nodes[0].hdri_environment->dominant_light_direction[2],
        -unrotated_x,
        0.0001f);
    EXPECT_TRUE(scene.lights.empty());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[1].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* data = assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->terrain_lighting.mode, TerrainLightingMode::HDRIEnvironment);
    EXPECT_GT(data->terrain_lighting.environment_intensity, 0.0f);
    EXPECT_GT(data->terrain_lighting.dominant_light_intensity, 0.0f);
}

TEST(SceneAuthoringMaterialize, TerrainHeightFieldSourceSupportsDirectAndChildFields)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_terrain_heightfield_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const ScalarFieldAsset direct_field =
        assets.scalar_fields().create_procedural_scalar_field({
            .name = "terrain/direct_field",
            .width = 8,
            .height = 8,
            .depth = 1,
            .generator = ScalarFieldGenerator::GradientY,
        });
    ASSERT_TRUE(direct_field.valid());

    SceneAssetData scene{};
    scene.name = "terrain_heightfield_sources";

    SceneNodeAsset direct = make_scene_node("direct_terrain");
    direct.terrain = SceneTerrainAsset{};
    direct.terrain_height_field_source = SceneTerrainHeightFieldSourceAsset{
        .mode = SceneTerrainHeightFieldSourceMode::ScalarFieldAsset,
        .scalar_field_asset = direct_field.output,
        .size = { 4.0f, 5.0f },
        .vertical_scale = 2.0f,
    };
    scene.nodes.push_back(std::move(direct));

    SceneNodeAsset parent = make_scene_node("child_terrain");
    parent.terrain = SceneTerrainAsset{};
    parent.terrain_height_field_source = SceneTerrainHeightFieldSourceAsset{
        .mode = SceneTerrainHeightFieldSourceMode::SceneNode,
        .source_node = "child_field",
        .size = { 6.0f, 7.0f },
    };
    scene.nodes.push_back(std::move(parent));

    SceneNodeAsset child = make_scene_node("child_field");
    child.parent_id = "child_terrain";
    child.scalar_field_source = SceneScalarFieldSourceAsset{
        .kind = SceneScalarFieldSourceKind::ProceduralCheckerboard,
        .width = 4,
        .height = 4,
        .depth = 1,
    };
    scene.nodes.push_back(std::move(child));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    EXPECT_EQ(
        scene.nodes[0].terrain_height_field_source->scalar_field_asset,
        direct_field.output);
    EXPECT_NE(scene.nodes[0].terrain->terrain_asset, wz::asset::AssetKey{});
    EXPECT_NE(
        scene.nodes[1].terrain_height_field_source->scalar_field_asset,
        wz::asset::AssetKey{});
    EXPECT_NE(scene.nodes[1].terrain->terrain_asset, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
    EXPECT_TRUE(
        assets.terrains()
            .get_terrain(TerrainAsset{
                .output = scene.nodes[0].terrain->terrain_asset,
            })
            .valid());
    EXPECT_TRUE(
        assets.terrains()
            .get_terrain(TerrainAsset{
                .output = scene.nodes[1].terrain->terrain_asset,
            })
            .valid());
}

TEST(SceneAuthoringMaterialize, TerrainSourceValidationAndVisibilityAreExplicit)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_terrain_validation_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    {
        EngineAssetLibrary assets{ device, logger, root };
        const MeshAsset mesh =
            assets.meshes().create_procedural_mesh({
                .name = "terrain/both_mesh",
                .kind = ProceduralMeshKind::Cube,
            });
        const ScalarFieldAsset field =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "terrain/both_field",
                .width = 4,
                .height = 4,
                .depth = 1,
            });

        SceneAssetData scene{};
        scene.name = "invalid_both";
        SceneNodeAsset node = make_scene_node("terrain");
        node.terrain = SceneTerrainAsset{};
        node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
            .mode = SceneTerrainMeshSourceMode::MeshAsset,
            .mesh_asset = mesh.output,
        };
        node.terrain_height_field_source = SceneTerrainHeightFieldSourceAsset{
            .mode = SceneTerrainHeightFieldSourceMode::ScalarFieldAsset,
            .scalar_field_asset = field.output,
        };
        scene.nodes.push_back(std::move(node));

        const auto report =
            materialize_scene_authoring_components(scene, assets);
        EXPECT_FALSE(report.ok);
        EXPECT_NE(report.error.find("both mesh and heightfield"), std::string::npos);
    }

    {
        EngineAssetLibrary assets{ device, logger, root };
        SceneAssetData scene{};
        scene.name = "missing_child";
        SceneNodeAsset node = make_scene_node("terrain");
        node.terrain = SceneTerrainAsset{};
        node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
            .mode = SceneTerrainMeshSourceMode::SceneNode,
            .source_node = "missing",
            .mesh_asset = wz::asset::AssetKey{
                .content_hash = { 1, 2 },
            },
        };
        scene.nodes.push_back(std::move(node));

        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
        EXPECT_EQ(
            scene.nodes[0].terrain_mesh_source->mesh_asset,
            wz::asset::AssetKey{});
        EXPECT_EQ(scene.nodes[0].terrain->terrain_asset, wz::asset::AssetKey{});
        EXPECT_FALSE(scene.nodes[0].renderable_asset.has_value());
    }

    {
        EngineAssetLibrary assets{ device, logger, root };
        const MeshAsset mesh =
            assets.meshes().create_procedural_mesh({
                .name = "terrain/invisible_mesh",
                .kind = ProceduralMeshKind::Cube,
            });

        SceneAssetData scene{};
        scene.name = "invisible";
        SceneNodeAsset node = make_scene_node("terrain");
        node.terrain = SceneTerrainAsset{ .visible = false };
        node.terrain_mesh_source = SceneTerrainMeshSourceAsset{
            .mode = SceneTerrainMeshSourceMode::MeshAsset,
            .mesh_asset = mesh.output,
        };
        scene.nodes.push_back(std::move(node));

        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
        EXPECT_NE(scene.nodes[0].terrain->terrain_asset, wz::asset::AssetKey{});
        EXPECT_FALSE(scene.nodes[0].renderable_asset.has_value());
        EXPECT_TRUE(report.renderables_to_realize.empty());
    }
}

