// tests/asset_scene/scene_authoring_materialize.cpp

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/scene/scene_authoring_materialize.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>

#include <cstring>
#include <vector>

namespace
{
    bool write_raw_f32(
        const wz::fs::Path& path,
        const std::vector<float>& values)
    {
        const size_t byte_count = values.size() * sizeof(float);
        wz::fs::Buffer bytes(byte_count);
        std::memcpy(bytes.data(), values.data(), byte_count);
        return wz::fs::write_file(path, bytes, true) == wz::fs::FileError::None;
    }
}

TEST(SceneAuthoringMaterialize, MeshSourceCreatesRenderableAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_source";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = true,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_EQ(report.renderables_to_realize.size(), 1u);
    EXPECT_EQ(report.renderables_to_realize[0], *scene.nodes[0].renderable_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->kind, RenderableKind::Mesh);
    EXPECT_EQ(
        renderable_data->program,
        BuiltinRenderProgram::MeshWireframeDepthDebug);
}

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
    EXPECT_NE(
        scene.nodes[1].direct_light_source->light_asset,
        wz::asset::AssetKey{});
    EXPECT_NE(
        scene.nodes[2].ambient_lighting->lighting_asset,
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
}

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

TEST(SceneAuthoringMaterialize, DefaultSceneHelperCreatesRootCameraScene)
{
    using namespace wz::engine::assets;

    SceneAssetData scene = make_default_scene_authoring_scene();

    ASSERT_EQ(scene.nodes.size(), 2u);
    EXPECT_EQ(scene.name, "scene_editor_scene");
    EXPECT_EQ(scene.nodes[0].id, "root");
    EXPECT_EQ(scene.nodes[1].id, "camera_01");
    ASSERT_TRUE(scene.nodes[1].parent_id.has_value());
    EXPECT_EQ(*scene.nodes[1].parent_id, "root");
    EXPECT_TRUE(scene.nodes[1].camera.has_value());
    ASSERT_TRUE(scene.defaults.active_camera_node.has_value());
    EXPECT_EQ(*scene.defaults.active_camera_node, "camera_01");
}
