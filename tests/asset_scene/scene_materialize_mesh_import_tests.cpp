#include "scene_authoring_materialize_test_support.h"

#include <algorithm>

namespace
{
    std::vector<float> read_float_channel(
        const wz::engine::assets::MeshDerivedFieldData& field,
        uint32_t channel_id)
    {
        const auto found = std::find_if(
            field.channels.begin(),
            field.channels.end(),
            [channel_id](
                const wz::engine::assets::MeshDerivedFieldChannel& channel) {
                return channel.channel_id == channel_id;
            });
        if (found == field.channels.end()
            || found->value_type
                != wz::engine::assets::MeshDerivedFieldValueType::Float1)
        {
            return {};
        }

        std::vector<float> values(field.element_count, 0.0f);
        std::memcpy(
            values.data(),
            field.values.data() + found->byte_offset,
            values.size() * sizeof(float));
        return values;
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

TEST(SceneAuthoringMaterialize, MeshDerivedFieldSourceFeedsExplicitHeatmapRenderable)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_derived_field_source_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "mesh_derived_field_source";
    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_derived_field_source = SceneMeshDerivedFieldSourceAsset{
        .enabled = true,
        .field_id = "height",
        .domain = MeshDerivedFieldDomain::Vertex,
        .channel_id = 0x2000u,
        .value_type = MeshDerivedFieldValueType::Float1,
        .source_kind =
            SceneMeshDerivedFieldSourceKind::PositionGradient,
        .component = SceneMeshDerivedFieldComponent::Y,
        .normalize = true,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .depth_test = true,
        .depth_write = true,
        .field_visualization_enabled = true,
        .field_visualization_channel_id = 0x2000u,
        .field_visualization_value_min = 0.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 0.8f,
        .field_visualization_field_ref = "field:height",
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_derived_field_source.has_value());

    const wz::asset::AssetKey field_key =
        scene.nodes[0].mesh_derived_field_source->resolved_field_asset;
    ASSERT_NE(field_key, wz::asset::AssetKey{});
    EXPECT_EQ(
        scene.nodes[0].mesh_render_style->field_visualization_asset,
        field_key);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const MeshDerivedFieldAsset field{ .output = field_key };
    const MeshDerivedFieldHandle field_handle =
        assets.mesh_derived_fields().get_mesh_derived_field(field);
    ASSERT_TRUE(field_handle.valid());
    const MeshDerivedFieldData* field_data =
        assets.mesh_derived_fields().get_mesh_derived_field_data(
            field_handle);
    ASSERT_NE(field_data, nullptr);
    EXPECT_EQ(field_data->domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(field_data->element_count, 8u);

    const std::vector<float> values =
        read_float_channel(*field_data, 0x2000u);
    ASSERT_EQ(values.size(), field_data->element_count);
    EXPECT_NE(std::find(values.begin(), values.end(), 0.0f), values.end());
    EXPECT_NE(std::find(values.begin(), values.end(), 1.0f), values.end());

    const auto renderable = assets.renderables().get_renderable(
        RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());
    const auto* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(renderable_data->mesh_field_visualization_asset, field_key);
    EXPECT_TRUE(renderable_data->mesh_style.field_visualization.enabled);
    EXPECT_EQ(
        renderable_data->mesh_style.field_visualization.channel_id,
        0x2000u);
    EXPECT_FLOAT_EQ(
        renderable_data->mesh_style.field_visualization.gamma,
        0.8f);
}

TEST(SceneAuthoringMaterialize, MeshDerivedFieldSourceBuildsScalarRecipes)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_authoring_mesh_derived_field_recipes_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    auto make_field_node =
        [](const char* id,
            const char* field_id,
            uint32_t channel_id,
            SceneMeshDerivedFieldSourceKind kind) {
            SceneNodeAsset node = make_scene_node(id);
            node.mesh_source = SceneMeshSourceAsset{
                .kind = SceneMeshSourceKind::ProceduralCube,
            };
            node.mesh_derived_field_source =
                SceneMeshDerivedFieldSourceAsset{
                    .enabled = true,
                    .field_id = field_id,
                    .domain = MeshDerivedFieldDomain::Vertex,
                    .channel_id = channel_id,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .source_kind = kind,
                    .component = SceneMeshDerivedFieldComponent::Y,
                    .normalize = true,
                    .constant_value = 0.5f,
                };
            return node;
        };

    SceneAssetData scene{};
    scene.name = "mesh_derived_field_recipes";
    scene.nodes.push_back(make_field_node(
        "constant",
        "constant",
        0x2001u,
        SceneMeshDerivedFieldSourceKind::Constant));
    scene.nodes.push_back(make_field_node(
        "vertex_index",
        "vertex_index",
        0x2002u,
        SceneMeshDerivedFieldSourceKind::VertexIndexGradient));
    scene.nodes.push_back(make_field_node(
        "corner_count",
        "corner_count",
        0x2003u,
        SceneMeshDerivedFieldSourceKind::TriangleCornerCount));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 3u);
    for (const SceneNodeAsset& node : scene.nodes) {
        ASSERT_TRUE(node.mesh_derived_field_source.has_value());
        EXPECT_NE(
            node.mesh_derived_field_source->resolved_field_asset,
            wz::asset::AssetKey{});
    }

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto field_data_for_node =
        [&assets](const SceneNodeAsset& node) {
            const MeshDerivedFieldAsset field{
                .output =
                    node.mesh_derived_field_source->resolved_field_asset,
            };
            const MeshDerivedFieldHandle field_handle =
                assets.mesh_derived_fields().get_mesh_derived_field(field);
            return assets.mesh_derived_fields().get_mesh_derived_field_data(
                field_handle);
        };

    const MeshDerivedFieldData* constant_field =
        field_data_for_node(scene.nodes[0]);
    ASSERT_NE(constant_field, nullptr);
    const std::vector<float> constant_values =
        read_float_channel(*constant_field, 0x2001u);
    ASSERT_EQ(constant_values.size(), constant_field->element_count);
    for (const float value : constant_values) {
        EXPECT_FLOAT_EQ(value, 0.5f);
    }

    const MeshDerivedFieldData* vertex_index_field =
        field_data_for_node(scene.nodes[1]);
    ASSERT_NE(vertex_index_field, nullptr);
    const std::vector<float> vertex_index_values =
        read_float_channel(*vertex_index_field, 0x2002u);
    ASSERT_EQ(vertex_index_values.size(), vertex_index_field->element_count);
    for (uint32_t i = 0;
        i < static_cast<uint32_t>(vertex_index_values.size());
        ++i)
    {
        const float expected =
            vertex_index_values.size() > 1u
                ? static_cast<float>(i)
                    / static_cast<float>(vertex_index_values.size() - 1u)
                : 0.0f;
        EXPECT_FLOAT_EQ(vertex_index_values[i], expected);
    }

    const MeshDerivedFieldData* corner_count_field =
        field_data_for_node(scene.nodes[2]);
    ASSERT_NE(corner_count_field, nullptr);
    const std::vector<float> corner_count_values =
        read_float_channel(*corner_count_field, 0x2003u);
    ASSERT_EQ(corner_count_values.size(), corner_count_field->element_count);

    const MeshHandle corner_mesh_handle = assets.meshes().get_mesh(
        MeshAsset{ .output = corner_count_field->source_mesh_key });
    ASSERT_TRUE(corner_mesh_handle.valid());
    const MeshData* corner_mesh =
        assets.meshes().get_mesh_data(corner_mesh_handle);
    ASSERT_NE(corner_mesh, nullptr);

    std::vector<float> expected_counts(corner_mesh->vertex_count(), 0.0f);
    for (const uint32_t index : corner_mesh->indices) {
        ASSERT_LT(index, expected_counts.size());
        expected_counts[index] += 1.0f;
    }
    const float max_count =
        *std::max_element(expected_counts.begin(), expected_counts.end());
    ASSERT_GT(max_count, 0.0f);
    for (float& value : expected_counts) {
        value /= max_count;
    }
    ASSERT_EQ(corner_count_values.size(), expected_counts.size());
    for (size_t i = 0; i < expected_counts.size(); ++i) {
        EXPECT_FLOAT_EQ(corner_count_values[i], expected_counts[i]);
    }
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

