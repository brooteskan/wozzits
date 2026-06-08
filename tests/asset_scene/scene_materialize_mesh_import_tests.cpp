#include "scene_authoring_materialize_test_support.h"

#include <algorithm>

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
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->style_asset,
        wz::asset::AssetKey{});
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
        renderable_data->companion_asset,
        scene.nodes[0].mesh_render_style->style_asset);
    EXPECT_EQ(
        renderable_data->program,
        BuiltinRenderProgram::MeshWireframeDepthDebug);
    EXPECT_TRUE(renderable_data->mesh_style.depth_test);
    EXPECT_TRUE(renderable_data->mesh_style.depth_write);
}

TEST(SceneAuthoringMaterialize, MeshSourceRegeneratesStaleStyleAsset)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_stale_style_asset_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const wz::asset::AssetKey stale_style_key{
        .content_hash = { 1, 2 },
        .schema_hash = { 3, 4 },
        .compiler_hash = { 5, 6 },
        .deps_hash = { 7, 8 },
    };

    SceneAssetData scene{};
    scene.name = "mesh_source_stale_style";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .style_asset = stale_style_key,
        .depth_test = true,
        .depth_write = true,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->style_asset,
        stale_style_key);
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->style_asset,
        wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
}

TEST(SceneAuthoringMaterialize, MeshWaveletAnalysisFeedsHeatmapRenderable)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_wavelet_analysis_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_wavelet_analysis";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_wavelet_analysis = SceneMeshWaveletAnalysisAsset{
        .enabled = true,
        .function = SceneMeshWaveletAnalysisFunction::BuiltinDetailHeatV0,
        .scale_count = 5,
        .lambda_max_estimate = 3.5f,
        .gamma = 0.75f,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = true,
        .field_visualization_enabled = true,
        .field_visualization_channel_id = MeshWaveletChannelID::kDetailCost,
        .field_visualization_value_min = 0.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 0.75f,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_wavelet_analysis.has_value());
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->field_visualization_asset,
        wz::asset::AssetKey{});
    EXPECT_EQ(
        scene.nodes[0].mesh_wavelet_analysis->field_asset,
        scene.nodes[0].mesh_render_style->field_visualization_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldAsset field{
        .output =
            scene.nodes[0].mesh_render_style->field_visualization_asset,
    };
    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(field_data->element_count, 8u);

    const auto detail_channel = std::find_if(
        field_data->channels.begin(),
        field_data->channels.end(),
        [](const MeshDerivedFieldChannel& channel) {
            return channel.channel_id == MeshWaveletChannelID::kDetailCost;
        });
    ASSERT_NE(detail_channel, field_data->channels.end());
    EXPECT_EQ(detail_channel->value_type, MeshDerivedFieldValueType::Float1);

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(
        renderable_data->mesh_field_visualization_asset,
        field.output);
    EXPECT_TRUE(renderable_data->mesh_style.field_visualization.enabled);
}

TEST(SceneAuthoringMaterialize, SceneImportSourceAppendsGLBHierarchy)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };

    SceneAssetData scene{};
    scene.name = "glb_scene_import";
    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/tank1",
    };
    scene.nodes.push_back(std::move(anchor));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 4u);

    const SceneNodeAsset* body =
        find_scene_node(scene, "tank_anchor/tank1/body");
    ASSERT_NE(body, nullptr);
    ASSERT_TRUE(body->parent_id.has_value());
    EXPECT_EQ(*body->parent_id, "tank_anchor");
    ASSERT_TRUE(body->mesh_source.has_value());
    EXPECT_EQ(body->mesh_source->kind, SceneMeshSourceKind::GLB);
    EXPECT_EQ(body->mesh_source->mesh_index, 2u);
    ASSERT_TRUE(body->renderable_asset.has_value());
    ASSERT_TRUE(body->imported_node.has_value());
    EXPECT_FALSE(body->imported_node->missing_source);

    const SceneNodeAsset* turret =
        find_scene_node(scene, "tank_anchor/tank1/turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->parent_id.has_value());
    EXPECT_EQ(*turret->parent_id, "tank_anchor/tank1/body");
    ASSERT_TRUE(turret->mesh_source.has_value());
    EXPECT_EQ(turret->mesh_source->mesh_index, 1u);
    ASSERT_TRUE(turret->renderable_asset.has_value());

    const SceneNodeAsset* gun =
        find_scene_node(scene, "tank_anchor/tank1/gun");
    ASSERT_NE(gun, nullptr);
    ASSERT_TRUE(gun->parent_id.has_value());
    EXPECT_EQ(*gun->parent_id, "tank_anchor/tank1/turret");
    ASSERT_TRUE(gun->mesh_source.has_value());
    EXPECT_EQ(gun->mesh_source->mesh_index, 0u);
    ASSERT_TRUE(gun->renderable_asset.has_value());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
}

TEST(SceneAuthoringMaterialize, SceneImportSourceRebuildPreservesChildBehavior)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};

    SceneAssetData scene{};
    scene.name = "glb_scene_import_rebuild";
    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/tank1",
    };
    scene.nodes.push_back(std::move(anchor));

    {
        EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };
        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
    }

    SceneNodeAsset* turret =
        find_scene_node(scene, "tank_anchor/tank1/turret");
    ASSERT_NE(turret, nullptr);
    turret->behavior = SceneBehaviorAsset{
        .module = "tank",
        .name = "rotate_turret",
        .enabled = true,
        .config = {
            SceneBehaviorConfigValue{
                .key = "speed",
                .kind = SceneBehaviorConfigValueKind::Number,
                .number_value = 2.0,
            },
        },
    };

    {
        EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };
        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());
    }

    ASSERT_EQ(scene.nodes.size(), 4u);
    turret = find_scene_node(scene, "tank_anchor/tank1/turret");
    ASSERT_NE(turret, nullptr);
    ASSERT_TRUE(turret->behavior.has_value());
    EXPECT_EQ(turret->behavior->module, "tank");
    EXPECT_EQ(turret->behavior->name, "rotate_turret");
    ASSERT_EQ(turret->behavior->config.size(), 1u);
    EXPECT_EQ(turret->behavior->config[0].key, "speed");
    EXPECT_DOUBLE_EQ(turret->behavior->config[0].number_value, 2.0);
    ASSERT_TRUE(turret->imported_node.has_value());
    EXPECT_FALSE(turret->imported_node->missing_source);
}

TEST(SceneAuthoringMaterialize, SceneImportSourceRejectsNodeIdCollision)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };

    SceneAssetData scene{};
    scene.name = "glb_scene_import_collision";
    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/tank1",
    };
    scene.nodes.push_back(std::move(anchor));
    scene.nodes.push_back(make_scene_node("tank_anchor/tank1/body"));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(
        report.error.find("collides with existing authored node"),
        std::string::npos);
}

TEST(SceneAuthoringMaterialize, SceneImportSourceMarksMissingNodes)
{
    using namespace wz::engine::assets;

    wz::Logger logger;
    wz::gpu::Device device{};

    SceneAssetData scene{};
    scene.name = "glb_scene_import_missing_source";
    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/tank1.glb",
        .import_prefix = "tank_anchor/import",
    };
    scene.nodes.push_back(std::move(anchor));

    {
        EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };
        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
    }

    ASSERT_NE(find_scene_node(scene, "tank_anchor/import/body"), nullptr);
    ASSERT_NE(find_scene_node(scene, "tank_anchor/import/turret"), nullptr);
    ASSERT_NE(find_scene_node(scene, "tank_anchor/import/gun"), nullptr);

    SceneNodeAsset* anchor_node = find_scene_node(scene, "tank_anchor");
    ASSERT_NE(anchor_node, nullptr);
    ASSERT_TRUE(anchor_node->scene_import_source.has_value());
    anchor_node->scene_import_source->path = "gltf/cube.glb";

    {
        EngineAssetLibrary assets{ device, logger, WZ_TEST_FIXTURE_DIR };
        const auto report =
            materialize_scene_authoring_components(scene, assets);
        ASSERT_TRUE(report.ok) << report.error;
    }

    const SceneNodeAsset* body =
        find_scene_node(scene, "tank_anchor/import/body");
    const SceneNodeAsset* turret =
        find_scene_node(scene, "tank_anchor/import/turret");
    const SceneNodeAsset* gun =
        find_scene_node(scene, "tank_anchor/import/gun");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(turret, nullptr);
    ASSERT_NE(gun, nullptr);
    ASSERT_TRUE(body->imported_node.has_value());
    ASSERT_TRUE(turret->imported_node.has_value());
    ASSERT_TRUE(gun->imported_node.has_value());
    EXPECT_TRUE(body->imported_node->missing_source);
    EXPECT_TRUE(turret->imported_node->missing_source);
    EXPECT_TRUE(gun->imported_node->missing_source);

    bool found_current_import_node = false;
    for (const auto& node : scene.nodes) {
        if (!node.imported_node || node.imported_node->missing_source) {
            continue;
        }
        if (node.imported_node->anchor_node == "tank_anchor"
            && node.imported_node->import_prefix == "tank_anchor/import")
        {
            found_current_import_node = true;
        }
    }
    EXPECT_TRUE(found_current_import_node);
}

