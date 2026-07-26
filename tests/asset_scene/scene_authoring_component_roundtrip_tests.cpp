#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, AssetReferenceComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_asset_reference_component_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "asset_reference_component_scene",
  "nodes": [
    {
      "id": "mesh_slot",
      "asset_reference": {
        "asset": "asset-key:0000000000000011:0000000000000022:0000000000000033:0000000000000044:0000000000000055:0000000000000066:0000000000000077:0000000000000088",
        "stable_asset_id": "asset-slot:hero_mesh",
        "expected_type": 1003,
        "label": "Hero mesh"
      }
    },
    {
      "id": "empty_slot",
      "asset_reference": {
        "expected_type": 1003
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "asset_reference_component.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "asset_reference_component",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 2u);

    const auto& assigned = scene_data->nodes[0];
    ASSERT_TRUE(assigned.asset_reference.has_value());
    EXPECT_FALSE(
        assigned.asset_reference->asset == wz::asset::AssetKey{});
    EXPECT_EQ(
        assigned.asset_reference->stable_asset_id,
        "asset-slot:hero_mesh");
    EXPECT_EQ(
        static_cast<uint16_t>(assigned.asset_reference->expected_type),
        1003u);
    EXPECT_EQ(assigned.asset_reference->label, "Hero mesh");
    EXPECT_FALSE(has_runtime_relevant_components(assigned));
    EXPECT_FALSE(has_asset_authoring_recipes(assigned));

    const auto& empty = scene_data->nodes[1];
    ASSERT_TRUE(empty.asset_reference.has_value());
    EXPECT_TRUE(empty.asset_reference->stable_asset_id.empty());
    EXPECT_EQ(
        static_cast<uint16_t>(empty.asset_reference->expected_type),
        1003u);

    const auto components = authored_components_for_node(assigned);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::AssetReference), 1);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.nodes_with_recipes, 0u);
    EXPECT_EQ(recipe_summary.total_recipes, 0u);

    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.asset_references, 2u);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.runtime_entities, 2u);
    EXPECT_EQ(runtime_summary.cameras, 0u);
    EXPECT_EQ(runtime_summary.lights, 0u);
    EXPECT_EQ(runtime_summary.terrains, 0u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"asset_reference\""), std::string::npos);
    EXPECT_NE(exported.find("\"asset-key:"), std::string::npos);
    EXPECT_NE(exported.find("\"asset-slot:hero_mesh\""), std::string::npos);
    EXPECT_NE(exported.find("\"expected_type\""), std::string::npos);
    EXPECT_NE(exported.find("\"Hero mesh\""), std::string::npos);
}

TEST(SceneAssetModule, MeshDerivedFieldSourceComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_derived_field_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_derived_field_source_scene",
  "nodes": [
    {
      "id": "mesh",
      "mesh_source": {
        "kind": "procedural_cube"
      },
      "mesh_derived_field_source": {
        "enabled": true,
        "field_id": "height",
        "domain": "vertex",
        "channel_id": 8192,
        "value_type": "float1",
        "source_kind": "position_gradient",
        "component": "y",
        "normalize": true,
        "constant_value": 0.25
      },
      "mesh_render_style": {
        "field_ref": "field:height",
        "channel_id": 8192,
        "value_min": 0.0,
        "value_max": 1.0,
        "gamma": 0.8,
        "palette": "diverging"
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_derived_field_source.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_derived_field_source",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_derived_field_source.has_value());
    const auto& source = *node.mesh_derived_field_source;
    EXPECT_TRUE(source.enabled);
    EXPECT_EQ(source.field_id, "height");
    EXPECT_EQ(source.domain, MeshDerivedFieldDomain::Vertex);
    EXPECT_EQ(source.channel_id, 8192u);
    EXPECT_EQ(source.value_type, MeshDerivedFieldValueType::Float1);
    EXPECT_EQ(
        source.source_kind,
        SceneMeshDerivedFieldSourceKind::PositionGradient);
    EXPECT_EQ(source.component, SceneMeshDerivedFieldComponent::Y);
    EXPECT_TRUE(source.normalize);
    EXPECT_FLOAT_EQ(source.constant_value, 0.25f);
    EXPECT_EQ(source.resolved_field_asset, wz::asset::AssetKey{});

    ASSERT_TRUE(node.mesh_render_style.has_value());
    EXPECT_TRUE(node.mesh_render_style->field_visualization_enabled);
    EXPECT_EQ(
        node.mesh_render_style->field_visualization_field_ref,
        "field:height");
    EXPECT_EQ(
        node.mesh_render_style->field_visualization_channel_id,
        8192u);
    EXPECT_FLOAT_EQ(
        node.mesh_render_style->field_visualization_value_min,
        0.0f);
    EXPECT_FLOAT_EQ(
        node.mesh_render_style->field_visualization_value_max,
        1.0f);
    EXPECT_FLOAT_EQ(
        node.mesh_render_style->field_visualization_gamma,
        0.8f);
    EXPECT_EQ(
        node.mesh_render_style->field_visualization_palette,
        MeshFieldVisualizationPalette::Diverging);

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::MeshDerivedFieldSource), 1);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.mesh_derived_field_sources, 1u);

    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.mesh_derived_field_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"mesh_derived_field_source\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"position_gradient\""), std::string::npos);
    EXPECT_NE(exported.find("\"field:height\""), std::string::npos);
    EXPECT_NE(exported.find("\"palette\""), std::string::npos);
    EXPECT_NE(exported.find("\"diverging\""), std::string::npos);
    EXPECT_NE(exported.find("\"channel_id\""), std::string::npos);
    EXPECT_EQ(exported.find("\"resolved_field_asset\""), std::string::npos);
}

TEST(SceneAssetModule, MeshMaskRenderStyleRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_mask_render_style_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_mask_render_style_scene",
  "nodes": [
    {
      "id": "mesh",
      "mesh_source": {
        "kind": "procedural_quad"
      },
      "mesh_derived_field_source": {
        "enabled": true,
        "field_id": "selected_faces",
        "domain": "face",
        "channel_id": 13056,
        "value_type": "float1",
        "source_kind": "triangle_area"
      },
      "mesh_render_style": {
        "surface": {
          "enabled": true,
          "color": [1.0, 1.0, 1.0, 1.0],
          "emissive_strength": 0.0
        },
        "wireframe": {
          "enabled": false,
          "color": [0.0, 0.0, 0.0, 1.0],
          "emissive_strength": 0.0
        }
      },
      "mesh_mask_render_style": {
        "enabled": true,
        "mesh_input": "processed",
        "source_field_ref": "field:selected_faces",
        "wireframe": {
          "enabled": true,
          "color": [1.0, 1.0, 1.0, 0.5],
          "emissive_strength": 1.0
        },
        "domain": "face",
        "projection_mode": "direct",
        "overlap_mode": "priority",
        "unmatched_color": [0.1, 0.1, 0.1, 1.0],
        "show_unmatched": false,
        "rules": [
          {
            "input_channel_id": 13056,
            "lo": 3.0,
            "hi": 3.0,
            "color": [0.9, 0.2, 0.1, 1.0],
            "priority": 5
          }
        ]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_mask_render_style.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_mask_render_style",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_render_style.has_value());
    EXPECT_FALSE(node.mesh_render_style->mask.enabled);
    ASSERT_TRUE(node.mesh_mask_render_style.has_value());
    const auto& style = *node.mesh_mask_render_style;
    EXPECT_TRUE(style.enabled);
    EXPECT_EQ(
        style.mesh_input,
        SceneMeshMaskRenderMeshInput::Processed);
    EXPECT_TRUE(style.wireframe.enabled);
    EXPECT_FLOAT_EQ(style.wireframe.color[3], 0.5f);
    EXPECT_TRUE(style.mask.enabled);
    EXPECT_EQ(style.source_field_ref, "field:selected_faces");
    EXPECT_EQ(style.source_field_asset, wz::asset::AssetKey{});
    EXPECT_EQ(style.mask.domain, MeshMaskDomain::Face);
    EXPECT_EQ(style.mask.projection_mode, MeshMaskProjectionMode::Direct);
    EXPECT_EQ(style.mask.overlap_mode, MeshMaskOverlapMode::Priority);
    EXPECT_FALSE(style.mask.show_unmatched);
    ASSERT_EQ(style.mask.rules.size(), 1u);
    EXPECT_EQ(style.mask.rules[0].input_channel_id, 13056u);
    EXPECT_FLOAT_EQ(style.mask.rules[0].lo, 3.0f);
    EXPECT_FLOAT_EQ(style.mask.rules[0].hi, 3.0f);
    EXPECT_EQ(style.mask.rules[0].priority, 5);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"mesh_mask_render_style\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"mesh_input\""), std::string::npos);
    EXPECT_NE(exported.find("\"processed\""), std::string::npos);
    EXPECT_NE(exported.find("\"overlap_mode\""), std::string::npos);
    EXPECT_NE(exported.find("\"source_field_ref\""), std::string::npos);
    EXPECT_NE(exported.find("\"wireframe\""), std::string::npos);
    EXPECT_NE(exported.find("\"field:selected_faces\""), std::string::npos);
    EXPECT_NE(exported.find("\"input_channel_id\""), std::string::npos);
    EXPECT_NE(exported.find("\"priority\""), std::string::npos);
    EXPECT_EQ(
        exported.find("\"mesh_render_style\":{\"mask\""),
        std::string::npos);
}

TEST(SceneAssetModule, MeshRegionSetRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_region_set_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_region_set_scene",
  "nodes": [
    {
      "id": "mesh",
      "mesh_source": {
        "kind": "procedural_quad"
      },
      "mesh_processing": {
        "enabled": true,
        "operation": "cluster_hierarchy_preview",
        "region_set_ref": "region_set:lod_regions",
        "preview_level_index": 0
      },
      "mesh_region_set": {
        "enabled": true,
        "region_set_id": "lod_regions",
        "intent": "remesh_subset",
        "mesh_input": "source",
        "source_field_ref": "field:detail_masks",
        "domain": "face",
        "projection_mode": "direct",
        "overlap_mode": "alpha_blend",
        "unmatched_color": [0.0, 0.0, 0.0, 1.0],
        "show_unmatched": true,
        "rules": [
          {
            "input_channel_id": 12288,
            "lo": 0.25,
            "hi": 1.0,
            "color": [1.0, 0.5, 0.0, 1.0],
            "priority": 2
          }
        ]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_region_set.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_region_set",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_processing.has_value());
    EXPECT_EQ(
        node.mesh_processing->region_set_ref,
        "region_set:lod_regions");
    ASSERT_TRUE(node.mesh_region_set.has_value());
    const auto& region_set = *node.mesh_region_set;
    EXPECT_TRUE(region_set.enabled);
    EXPECT_EQ(region_set.region_set_id, "lod_regions");
    EXPECT_EQ(region_set.intent, SceneMeshRegionSetIntent::RemeshSubset);
    EXPECT_EQ(region_set.mesh_input, SceneMeshMaskRenderMeshInput::Source);
    EXPECT_EQ(region_set.source_field_ref, "field:detail_masks");
    EXPECT_EQ(region_set.mask.domain, MeshMaskDomain::Face);
    EXPECT_EQ(region_set.mask.overlap_mode, MeshMaskOverlapMode::AlphaBlend);
    ASSERT_EQ(region_set.mask.rules.size(), 1u);
    EXPECT_EQ(region_set.mask.rules[0].input_channel_id, 12288u);
    EXPECT_EQ(region_set.mask.rules[0].priority, 2);

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(
        std::count(
            components.begin(),
            components.end(),
            wz::scene::SceneAuthoredComponentKind::MeshRegionSet),
        1);

    const auto summary = summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(summary.mesh_region_sets, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"mesh_region_set\""), std::string::npos);
    EXPECT_NE(exported.find("\"region_set_id\""), std::string::npos);
    EXPECT_NE(exported.find("\"lod_regions\""), std::string::npos);
    EXPECT_NE(exported.find("\"remesh_subset\""), std::string::npos);
    EXPECT_NE(exported.find("\"region_set_ref\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"region_set:lod_regions\""),
        std::string::npos);
}

TEST(SceneAssetModule, MeshSparseOperatorSourceComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_sparse_operator_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_sparse_operator_source_scene",
  "nodes": [
    {
      "id": "mesh",
      "mesh_source": {
        "kind": "procedural_cube"
      },
      "mesh_sparse_operator_source": {
        "enabled": true,
        "operator_id": "uniform_laplacian",
        "kind": "uniform_vertex_laplacian",
        "domain": "vertex",
        "value_convention": "neighbor_weights"
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_sparse_operator_source.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_sparse_operator_source",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_sparse_operator_source.has_value());
    const auto& source = *node.mesh_sparse_operator_source;
    EXPECT_TRUE(source.enabled);
    EXPECT_EQ(source.operator_id, "uniform_laplacian");
    EXPECT_EQ(
        source.kind,
        MeshSparseOperatorKind::UniformVertexLaplacian);
    EXPECT_EQ(source.domain, MeshOperatorDomain::Vertex);
    EXPECT_EQ(
        source.value_convention,
        MeshSparseOperatorValueConvention::NeighborWeights);
    EXPECT_EQ(source.resolved_operator_asset, wz::asset::AssetKey{});

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::MeshSparseOperatorSource), 1);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.mesh_sparse_operator_sources, 1u);

    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.mesh_sparse_operator_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"mesh_sparse_operator_source\""),
        std::string::npos);
    EXPECT_NE(
        exported.find("\"uniform_vertex_laplacian\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"neighbor_weights\""), std::string::npos);
    EXPECT_EQ(
        exported.find("\"resolved_operator_asset\""),
        std::string::npos);
}

TEST(SceneAssetModule, MeshSparseApplyFieldComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_sparse_apply_field_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_sparse_apply_field_scene",
  "nodes": [
    {
      "id": "mesh",
      "mesh_sparse_apply_field": {
        "enabled": true,
        "operator_ref": "operator:uniform_laplacian",
        "input_field_ref": "field:height",
        "input_channel_id": 8192,
        "output_channel_id": 8448,
        "apply_mode": "residual"
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_sparse_apply_field.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_sparse_apply_field",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_sparse_apply_field.has_value());
    const auto& field = *node.mesh_sparse_apply_field;
    EXPECT_TRUE(field.enabled);
    EXPECT_EQ(field.operator_ref, "operator:uniform_laplacian");
    EXPECT_EQ(field.input_field_ref, "field:height");
    EXPECT_EQ(field.input_channel_id, 8192u);
    EXPECT_EQ(field.output_channel_id, 8448u);
    EXPECT_EQ(field.apply_mode, SceneMeshSparseApplyMode::Residual);
    EXPECT_EQ(field.output_field_asset, wz::asset::AssetKey{});

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::MeshSparseApplyField), 1);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.mesh_sparse_apply_fields, 1u);

    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.mesh_sparse_apply_fields, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"mesh_sparse_apply_field\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"residual\""), std::string::npos);
    EXPECT_NE(exported.find("\"field:height\""), std::string::npos);
    EXPECT_EQ(
        exported.find("\"output_field_asset\""),
        std::string::npos);
}

TEST(SceneAssetModule, MeshSparseDiffusionBandsComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_sparse_diffusion_bands_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_sparse_diffusion_bands_scene",
  "nodes": [
    {
      "id": "mesh",
      "mesh_sparse_diffusion_bands": {
        "enabled": true,
        "operator_ref": "operator:uniform_laplacian",
        "input_field_ref": "field:corner_count",
        "input_channel_id": 8192,
        "output_base_channel_id": 8704,
        "band_count": 3,
        "iterations_per_band": 2,
        "mode": "diffusion_step",
        "tau": 0.5
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_sparse_diffusion_bands.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_sparse_diffusion_bands",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_sparse_diffusion_bands.has_value());
    const auto& bands = *node.mesh_sparse_diffusion_bands;
    EXPECT_TRUE(bands.enabled);
    EXPECT_EQ(bands.operator_ref, "operator:uniform_laplacian");
    EXPECT_EQ(bands.input_field_ref, "field:corner_count");
    EXPECT_EQ(bands.input_channel_id, 8192u);
    EXPECT_EQ(bands.output_base_channel_id, 8704u);
    EXPECT_EQ(bands.band_count, 3u);
    EXPECT_EQ(bands.iterations_per_band, 2u);
    EXPECT_EQ(bands.mode, SceneMeshSparseDiffusionMode::DiffusionStep);
    EXPECT_FLOAT_EQ(bands.tau, 0.5f);
    EXPECT_EQ(bands.output_field_asset, wz::asset::AssetKey{});

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::MeshSparseDiffusionBands), 1);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.mesh_sparse_diffusion_bands, 1u);

    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.mesh_sparse_diffusion_bands, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"mesh_sparse_diffusion_bands\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"diffusion_step\""), std::string::npos);
    EXPECT_NE(exported.find("\"field:corner_count\""), std::string::npos);
    EXPECT_EQ(
        exported.find("\"output_field_asset\""),
        std::string::npos);
}

TEST(SceneAssetModule, MeshLevelMaskSourceComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_level_mask_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_level_mask_source_scene",
  "nodes": [
    {
      "id": "mesh",
      "mesh_level_mask_source": {
        "enabled": true,
        "input_field_ref": "field:diffusion_bands",
        "output_field_id": "masks",
        "domain": "face",
        "regions": [
          {
            "input_channel_id": 8704,
            "output_channel_id": 12288,
            "lo": 0.1,
            "hi": 0.4
          },
          {
            "input_channel_id": 8705,
            "output_channel_id": 12289,
            "min_value": 0.6,
            "max_value": 1.0
          }
        ]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_level_mask_source.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_level_mask_source",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_level_mask_source.has_value());
    const auto& masks = *node.mesh_level_mask_source;
    EXPECT_TRUE(masks.enabled);
    EXPECT_EQ(masks.input_field_ref, "field:diffusion_bands");
    EXPECT_EQ(masks.output_field_id, "masks");
    EXPECT_EQ(masks.domain, MeshDerivedFieldDomain::Face);
    ASSERT_EQ(masks.regions.size(), 2u);
    EXPECT_EQ(masks.regions[0].input_channel_id, 8704u);
    EXPECT_EQ(masks.regions[0].output_channel_id, 12288u);
    EXPECT_FLOAT_EQ(masks.regions[0].min_value, 0.1f);
    EXPECT_FLOAT_EQ(masks.regions[0].max_value, 0.4f);
    EXPECT_EQ(masks.regions[1].input_channel_id, 8705u);
    EXPECT_EQ(masks.regions[1].output_channel_id, 12289u);
    EXPECT_FLOAT_EQ(masks.regions[1].min_value, 0.6f);
    EXPECT_FLOAT_EQ(masks.regions[1].max_value, 1.0f);
    EXPECT_EQ(masks.output_field_asset, wz::asset::AssetKey{});

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::MeshLevelMaskSource), 1);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.mesh_level_mask_sources, 1u);

    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.mesh_level_mask_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"mesh_level_mask_source\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"field:diffusion_bands\""), std::string::npos);
    EXPECT_NE(exported.find("\"regions\""), std::string::npos);
    EXPECT_NE(exported.find("\"face\""), std::string::npos);
    EXPECT_EQ(
        exported.find("\"output_field_asset\""),
        std::string::npos);
}

TEST(SceneAssetModule, MeshWaveletAnalysisComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_wavelet_analysis_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_wavelet_analysis_scene",
  "nodes": [
    {
      "id": "mesh",
      "mesh_source": {
        "kind": "procedural_cube"
      },
      "mesh_wavelet_analysis": {
        "enabled": true,
        "function": "builtin_detail_heat_v0",
        "scale_count": 5,
        "lambda_max_estimate": 3.5,
        "gamma": 0.75
      },
      "mesh_render_style": {
        "field_visualization_enabled": true,
        "field_visualization_channel_id": 4608
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_wavelet_analysis.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_wavelet_analysis",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.mesh_wavelet_analysis.has_value());
    EXPECT_TRUE(node.mesh_wavelet_analysis->enabled);
    EXPECT_EQ(
        node.mesh_wavelet_analysis->function,
        SceneMeshWaveletAnalysisFunction::BuiltinDetailHeatV0);
    EXPECT_EQ(node.mesh_wavelet_analysis->scale_count, 5u);
    EXPECT_FLOAT_EQ(
        node.mesh_wavelet_analysis->lambda_max_estimate,
        3.5f);
    EXPECT_FLOAT_EQ(node.mesh_wavelet_analysis->gamma, 0.75f);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.mesh_wavelet_analyses, 1u);

    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.mesh_wavelet_analyses, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"mesh_wavelet_analysis\""), std::string::npos);
    EXPECT_NE(exported.find("\"builtin_detail_heat_v0\""), std::string::npos);
    EXPECT_NE(exported.find("\"scale_count\""), std::string::npos);
    EXPECT_NE(exported.find("\"lambda_max_estimate\""), std::string::npos);
    EXPECT_NE(exported.find("\"gamma\""), std::string::npos);
    EXPECT_EQ(exported.find("\"field_asset\""), std::string::npos);
}

TEST(SceneAssetModule, TerrainMeshSourceComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_terrain_mesh_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "terrain/source_rock",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "terrain_mesh_source_scene",
  "nodes": [
    {
      "id": "source_mesh",
      "mesh_source": {
        "kind": "procedural_cube"
      },
      "mesh_processing": {
        "enabled": true,
        "operation": "cluster_hierarchy_preview",
        "preview_level_index": 0,
        "target_ratio": 0.5,
        "preserve_boundary": true
      }
    },
    {
      "id": "terrain",
      "terrain_mesh_source": {
        "mode": "scene_node",
        "source_node": "source_mesh",
        "asset": "asset://meshes/source_rock",
        "height_policy": "highest_accepted_surface",
        "min_surface_normal_y": 0.35,
        "include_backfaces": true
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "terrain_mesh_source.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "terrain_mesh_source",
            .path = rel_path,
            .mesh_asset_references = {
                SceneAssetReferenceBinding{
                    .uri = "asset://meshes/source_rock",
                    .key = mesh.output,
                },
            },
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 2u);

    ASSERT_TRUE(scene_data->nodes[0].mesh_processing.has_value());
    EXPECT_TRUE(scene_data->nodes[0].mesh_processing->enabled);
    EXPECT_EQ(
        scene_data->nodes[0].mesh_processing->operation,
        SceneMeshProcessingOperation::MeshClusterHierarchyPreview);
    EXPECT_EQ(scene_data->nodes[0].mesh_processing->preview_level_index, 0u);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].mesh_processing->target_ratio, 0.5f);
    EXPECT_TRUE(scene_data->nodes[0].mesh_processing->preserve_boundary);

    const auto& node = scene_data->nodes[1];
    ASSERT_TRUE(node.terrain_mesh_source.has_value());
    EXPECT_EQ(
        node.terrain_mesh_source->mode,
        SceneTerrainMeshSourceMode::SceneNode);
    EXPECT_EQ(node.terrain_mesh_source->source_node, "source_mesh");
    EXPECT_EQ(node.terrain_mesh_source->mesh_asset, mesh.output);
    EXPECT_EQ(
        node.terrain_mesh_source->height_policy,
        SceneTerrainMeshHeightPolicy::HighestAcceptedSurface);
    EXPECT_FLOAT_EQ(
        node.terrain_mesh_source->min_surface_normal_y,
        0.35f);
    EXPECT_TRUE(node.terrain_mesh_source->include_backfaces);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"terrain_mesh_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"mesh_processing\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"cluster_hierarchy_preview\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"preview_level_index\""), std::string::npos);
    EXPECT_NE(exported.find("\"target_ratio\""), std::string::npos);
    EXPECT_NE(exported.find("\"scene_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"source_mesh\""), std::string::npos);
    EXPECT_NE(exported.find("\"height_policy\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"highest_accepted_surface\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"min_surface_normal_y\""), std::string::npos);
    EXPECT_NE(exported.find("\"include_backfaces\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);

    const wz::fs::Path reparse_root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_terrain_mesh_source_reparse_test");
    ASSERT_EQ(
        wz::fs::create_directories(reparse_root),
        wz::fs::FileError::None);

    wz::Logger reparse_logger;
    wz::gpu::Device reparse_device{};
    wz::engine::assets::EngineAssetLibrary reparse_assets{
        reparse_device, reparse_logger, reparse_root };

    auto exported_rel_path = write_scene_json(
        reparse_root,
        "terrain_mesh_source_exported.scene.json",
        exported);
    const auto exported_scene_asset =
        reparse_assets.scenes().create_scene_from_json({
            .name = "terrain_mesh_source_exported",
            .path = exported_rel_path,
        });
    ASSERT_TRUE(exported_scene_asset.valid());
    ASSERT_TRUE(reparse_assets.commit());
    ASSERT_TRUE(reparse_assets.resolve_all().ok());

    const auto* reparsed_scene_data = reparse_assets.scenes().get_scene_data(
        reparse_assets.scenes().get_scene(exported_scene_asset));
    ASSERT_NE(reparsed_scene_data, nullptr);
    ASSERT_EQ(reparsed_scene_data->nodes.size(), 2u);
    ASSERT_TRUE(
        reparsed_scene_data->nodes[0].mesh_processing.has_value());
    EXPECT_EQ(
        reparsed_scene_data->nodes[0].mesh_processing->operation,
        SceneMeshProcessingOperation::MeshClusterHierarchyPreview);
    ASSERT_TRUE(
        reparsed_scene_data->nodes[1].terrain_mesh_source.has_value());
    EXPECT_EQ(
        reparsed_scene_data->nodes[1].terrain_mesh_source->source_node,
        "source_mesh");

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.terrains, 0u);
}

TEST(SceneAssetModule, RejectsOutOfRangeMeshProcessingPreviewLevelIndex)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_processing_preview_level_range_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_processing_preview_level_range_scene",
  "nodes": [
    {
      "id": "source_mesh",
      "mesh_source": {
        "kind": "procedural_cube"
      },
      "mesh_processing": {
        "enabled": true,
        "operation": "cluster_hierarchy_preview",
        "preview_level_index": 5000000000
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root,
        "mesh_processing_preview_level_range.scene.json",
        scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_processing_preview_level_range",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    EXPECT_FALSE(assets.resolve_all().ok());
}

TEST(SceneAssetModule, SceneImportSourceRoundTripsThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_import_source_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "scene_import_source_roundtrip";

    SceneNodeAsset anchor = make_scene_node("tank_anchor");
    anchor.scene_import_source = SceneImportSourceAsset{
        .kind = SceneImportSourceKind::GLB,
        .path = "gltf/test-mesh-a.glb",
        .import_prefix = "tank_anchor/tank1",
        .scene_index = 0u,
    };
    authored.nodes.push_back(std::move(anchor));

    SceneNodeAsset turret = make_scene_node(
        "tank_anchor/tank1/turret",
        "turret");
    turret.parent_id = "tank_anchor/tank1/body";
    turret.imported_node = SceneImportedNodeAsset{
        .anchor_node = "tank_anchor",
        .import_prefix = "tank_anchor/tank1",
        .source_node_id = "turret",
        .missing_source = false,
    };
    turret.behavior = SceneBehaviorAsset{
        .module = "tank",
        .name = "rotate_turret",
    };
    authored.nodes.push_back(std::move(turret));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"scene_import_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"imported_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"tank_anchor/tank1\""), std::string::npos);

    auto rel_path = write_scene_json(
        root,
        "scene_import_source.scene.json",
        exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "scene_import_source",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 2u);

    const auto* parsed_anchor = find_scene_node(*data, "tank_anchor");
    ASSERT_NE(parsed_anchor, nullptr);
    ASSERT_TRUE(parsed_anchor->scene_import_source.has_value());
    EXPECT_EQ(parsed_anchor->scene_import_source->kind,
        SceneImportSourceKind::GLB);
    EXPECT_EQ(parsed_anchor->scene_import_source->path, "gltf/test-mesh-a.glb");
    EXPECT_EQ(
        parsed_anchor->scene_import_source->import_prefix,
        "tank_anchor/tank1");
    ASSERT_TRUE(parsed_anchor->scene_import_source->scene_index.has_value());
    EXPECT_EQ(*parsed_anchor->scene_import_source->scene_index, 0u);

    const auto* parsed_turret =
        find_scene_node(*data, "tank_anchor/tank1/turret");
    ASSERT_NE(parsed_turret, nullptr);
    ASSERT_TRUE(parsed_turret->imported_node.has_value());
    EXPECT_EQ(parsed_turret->imported_node->anchor_node, "tank_anchor");
    EXPECT_EQ(parsed_turret->imported_node->source_node_id, "turret");
    EXPECT_FALSE(parsed_turret->imported_node->missing_source);
    ASSERT_TRUE(parsed_turret->behavior.has_value());
    EXPECT_EQ(parsed_turret->behavior->module, "tank");
}

// Issue #213 increment 1a: the renderable binding by ingredients (a geometry
// asset-graph node ref + a render-program asset-graph node ref on a scene node)
// round-trips through scene JSON. Covers the three shapes the inheritance model
// relies on: a program-only group node, a geometry-only child (program
// inherited), and a node carrying both.
TEST(SceneAssetModule, RenderBindingIngredientsRoundTripThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_render_binding_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "render_binding_roundtrip";

    // Group node: carries only a render program (no geometry). Draws nothing
    // itself; the program cascades to descendants at assembly.
    SceneNodeAsset group = make_scene_node("tank");
    attach_render_program_node(group, 10);
    authored.nodes.push_back(std::move(group));

    // Child: provides geometry, no program of its own (inherits the group's).
    SceneNodeAsset part = make_scene_node("tank/body", "body");
    part.parent_id = "tank";
    attach_geometry_asset_node(part, 24);
    authored.nodes.push_back(std::move(part));

    // Standalone node carrying both its own geometry and program.
    SceneNodeAsset solo = make_scene_node("solo");
    attach_geometry_asset_node(solo, 9);
    attach_render_program_node(solo, 19);
    authored.nodes.push_back(std::move(solo));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"geometry\""), std::string::npos);
    EXPECT_NE(exported.find("\"render_program\""), std::string::npos);

    auto rel_path = write_scene_json(root, "render_binding.scene.json", exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "render_binding",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 3u);

    const auto* parsed_group = find_scene_node(*data, "tank");
    ASSERT_NE(parsed_group, nullptr);
    EXPECT_FALSE(parsed_group->geometry_asset_node_id.has_value());
    ASSERT_TRUE(parsed_group->render_program_node_id.has_value());
    EXPECT_EQ(*parsed_group->render_program_node_id,
        wz::asset::AssetGraphDraftNodeId{ 10 });

    const auto* parsed_part = find_scene_node(*data, "tank/body");
    ASSERT_NE(parsed_part, nullptr);
    ASSERT_TRUE(parsed_part->geometry_asset_node_id.has_value());
    EXPECT_EQ(*parsed_part->geometry_asset_node_id,
        wz::asset::AssetGraphDraftNodeId{ 24 });
    EXPECT_FALSE(parsed_part->render_program_node_id.has_value());

    const auto* parsed_solo = find_scene_node(*data, "solo");
    ASSERT_NE(parsed_solo, nullptr);
    ASSERT_TRUE(parsed_solo->geometry_asset_node_id.has_value());
    EXPECT_EQ(*parsed_solo->geometry_asset_node_id,
        wz::asset::AssetGraphDraftNodeId{ 9 });
    ASSERT_TRUE(parsed_solo->render_program_node_id.has_value());
    EXPECT_EQ(*parsed_solo->render_program_node_id,
        wz::asset::AssetGraphDraftNodeId{ 19 });
}

TEST(SceneAssetModule, CollisionHeightFieldConstraintRoundTripsThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_collision_constraint_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    // Fabricated resolved keys so the collision object is emitted on export.
    wz::asset::AssetKey collision_key{};
    collision_key.content_hash = { 0x11u, 0x22u };
    collision_key.schema_hash = { 0x33u, 0x44u };
    collision_key.compiler_hash = { 0x55u, 0x66u };
    collision_key.deps_hash = { 0x77u, 0x88u };

    wz::asset::AssetKey scalar_field_key{};
    scalar_field_key.content_hash = { 0xAAu, 0xBBu };
    scalar_field_key.schema_hash = { 0xCCu, 0xDDu };
    scalar_field_key.compiler_hash = { 0xEEu, 0xFFu };
    scalar_field_key.deps_hash = { 0x99u, 0x10u };

    SceneCollisionHeightFieldSource source{};
    source.scalar_field_asset = scalar_field_key;
    source.origin[0] = -4.0f;
    source.origin[1] = 6.0f;
    source.size[0] = 32.0f;
    source.size[1] = 48.0f;
    source.vertical_scale = 5.0f;
    source.base_height = -2.0f;
    source.projection_resolution_x = 256u;
    source.projection_resolution_y = 128u;

    SceneAssetData authored{};
    authored.name = "collision_constraint_roundtrip";

    SceneNodeAsset ground = make_scene_node("ground");
    ground.collision = SceneCollisionAsset{
        .collision_asset = collision_key,
        .layer_mask = 0x3u,
        .collides_with_mask = 0x5u,
        .is_trigger = false,
        .enabled = true,
        .constrain_movement = true,
        .height_field_source = source,
    };
    authored.nodes.push_back(std::move(ground));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"constrain_movement\""), std::string::npos);
    EXPECT_NE(exported.find("\"height_field_source\""), std::string::npos);

    auto rel_path = write_scene_json(
        root, "collision_constraint.scene.json", exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "collision_constraint",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 1u);

    const auto* parsed = find_scene_node(*data, "ground");
    ASSERT_NE(parsed, nullptr);
    ASSERT_TRUE(parsed->collision.has_value());
    const auto& collision = *parsed->collision;
    EXPECT_EQ(collision.collision_asset, collision_key);
    EXPECT_EQ(collision.layer_mask, 0x3u);
    EXPECT_EQ(collision.collides_with_mask, 0x5u);
    EXPECT_FALSE(collision.is_trigger);
    EXPECT_TRUE(collision.enabled);
    EXPECT_TRUE(collision.constrain_movement);

    ASSERT_TRUE(collision.height_field_source.has_value());
    const auto& parsed_source = *collision.height_field_source;
    EXPECT_EQ(parsed_source.scalar_field_asset, scalar_field_key);
    EXPECT_FLOAT_EQ(parsed_source.origin[0], -4.0f);
    EXPECT_FLOAT_EQ(parsed_source.origin[1], 6.0f);
    EXPECT_FLOAT_EQ(parsed_source.size[0], 32.0f);
    EXPECT_FLOAT_EQ(parsed_source.size[1], 48.0f);
    EXPECT_FLOAT_EQ(parsed_source.vertical_scale, 5.0f);
    EXPECT_FLOAT_EQ(parsed_source.base_height, -2.0f);
    EXPECT_EQ(parsed_source.projection_resolution_x, 256u);
    EXPECT_EQ(parsed_source.projection_resolution_y, 128u);
}

TEST(SceneAssetModule, CollisionAssetNodeRefRoundTripsThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_collision_node_ref_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    // A collision authored by REFERENCE (issue #216/#217): the authored
    // asset-graph node id + the constrain_movement toggle. We also fabricate a
    // resolved collision_asset key so the scene asset RESOLVES (a bare collision
    // with no resolved key compiles but does not resolve to scene data); the
    // round-trip we assert is the NODE-REF + constrain_movement, which the export
    // must persist and the parse must read back. (On (re)bind against a real
    // graph the node-ref takes precedence and re-bridges the key; that precedence
    // is covered by the on-device collision/motion authoring test.)
    wz::asset::AssetKey collision_key{};
    collision_key.content_hash = { 0x12u, 0x34u };
    collision_key.schema_hash = { 0x56u, 0x78u };
    collision_key.compiler_hash = { 0x9Au, 0xBCu };
    collision_key.deps_hash = { 0xDEu, 0xF0u };

    SceneAssetData authored{};
    authored.name = "collision_node_ref_roundtrip";

    SceneNodeAsset ground = make_scene_node("ground");
    attach_collision_asset_node(ground, /*node_id=*/77u,
        /*constrain_movement=*/true);
    ground.collision->collision_asset = collision_key;
    authored.nodes.push_back(std::move(ground));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"collision_asset_node_id\""), std::string::npos);
    EXPECT_NE(exported.find("\"constrain_movement\""), std::string::npos);

    auto rel_path = write_scene_json(
        root, "collision_node_ref.scene.json", exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "collision_node_ref",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 1u);

    const auto* parsed = find_scene_node(*data, "ground");
    ASSERT_NE(parsed, nullptr);
    ASSERT_TRUE(parsed->collision.has_value());
    const auto& collision = *parsed->collision;
    ASSERT_TRUE(collision.collision_asset_node_id.has_value());
    EXPECT_EQ(*collision.collision_asset_node_id, 77u);
    EXPECT_TRUE(collision.constrain_movement);
    // The node-ref takes precedence: the parse clears the resolved key so it is
    // re-bridged from the graph on (re)bind, not read from the JSON.
    EXPECT_EQ(collision.collision_asset, wz::asset::AssetKey{});
}

TEST(SceneAssetModule, IndexedGlbPartGeometryRoundTripsThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_glb_part_geometry_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "glb_part_geometry_roundtrip";

    // Host carries the program (inherited); each part references the same
    // Scene-from-GLB node (id 21) but a different GLB part by name.
    SceneNodeAsset host = make_scene_node("tank");
    attach_render_program_node(host, 10);
    authored.nodes.push_back(std::move(host));

    SceneNodeAsset body = make_scene_node("tank/body", "body");
    body.parent_id = "tank";
    attach_geometry_glb_part(body, 21, "body");
    authored.nodes.push_back(std::move(body));

    SceneNodeAsset turret = make_scene_node("tank/turret", "turret");
    turret.parent_id = "tank";
    attach_geometry_glb_part(turret, 21, "turret");
    authored.nodes.push_back(std::move(turret));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"glb_node_id\""), std::string::npos);

    auto rel_path = write_scene_json(root, "glb_part.scene.json", exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "glb_part",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    // Both parts share the Scene-from-GLB node id but select distinct GLB parts.
    const auto* parsed_body = find_scene_node(*data, "tank/body");
    ASSERT_NE(parsed_body, nullptr);
    ASSERT_TRUE(parsed_body->geometry_asset_node_id.has_value());
    EXPECT_EQ(*parsed_body->geometry_asset_node_id,
        wz::asset::AssetGraphDraftNodeId{ 21 });
    ASSERT_TRUE(parsed_body->geometry_glb_node_id.has_value());
    EXPECT_EQ(*parsed_body->geometry_glb_node_id, "body");

    const auto* parsed_turret = find_scene_node(*data, "tank/turret");
    ASSERT_NE(parsed_turret, nullptr);
    EXPECT_EQ(*parsed_turret->geometry_asset_node_id,
        wz::asset::AssetGraphDraftNodeId{ 21 });
    ASSERT_TRUE(parsed_turret->geometry_glb_node_id.has_value());
    EXPECT_EQ(*parsed_turret->geometry_glb_node_id, "turret");
}

// A scene-source host's per-child component overrides (issue #213) persist on the
// host and round-trip through scene JSON, keyed by the child's stable sub-scene id.
// This is what carries a render program authored on a runtime-only grafted child
// across reload (save_scene excludes the grafted child itself).
TEST(SceneAssetModule, SceneSourceChildOverridesRoundTripThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_source_child_overrides_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "scene_source_child_overrides_roundtrip";

    // Host node carries a scene source (asset-graph node 21) plus two per-child
    // overrides: "body" gets a render program (node 10), "turret" carries an
    // (orphan-style) entry with no program — which must be dropped on export, not
    // emitted as an empty entry.
    SceneNodeAsset host = make_scene_node("tank");
    attach_scene_source_node(host, 21);
    host.scene_source_child_overrides.push_back(
        SceneSourceChildOverride{
            .child_id = "body",
            .render_program_node_id =
                wz::asset::AssetGraphDraftNodeId{ 10 },
        });
    authored.nodes.push_back(std::move(host));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"scene_source_child_overrides\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"child_id\""), std::string::npos);

    auto rel_path =
        write_scene_json(root, "child_overrides.scene.json", exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "child_overrides",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    const auto* parsed_host = find_scene_node(*data, "tank");
    ASSERT_NE(parsed_host, nullptr);
    ASSERT_EQ(parsed_host->scene_source_child_overrides.size(), 1u);
    const auto& ov = parsed_host->scene_source_child_overrides.front();
    EXPECT_EQ(ov.child_id, "body");
    ASSERT_TRUE(ov.render_program_node_id.has_value());
    EXPECT_EQ(*ov.render_program_node_id,
        wz::asset::AssetGraphDraftNodeId{ 10 });
}

TEST(SceneAssetModule, BehaviorApplyInEditorRoundTripsThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_behavior_apply_in_editor_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "behavior_apply_in_editor_roundtrip";

    SceneNodeAsset node = make_scene_node("listener");
    node.event_listener = SceneEventListenerAsset{
        .channels = { "collision.*" },
    };
    node.behaviors.push_back(SceneBehaviorAsset{
        .id = "listener_behavior",
        .label = "Listener behavior",
        .module = "debug",
        .name = "log_collision_events",
        .enabled = true,
        .apply_in_editor = true,
        .events = { "collision.*" },
    });
    authored.nodes.push_back(std::move(node));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"apply_in_editor\""), std::string::npos);
    EXPECT_NE(exported.find("true"), std::string::npos);

    auto rel_path = write_scene_json(
        root,
        "behavior_apply_in_editor.scene.json",
        exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "behavior_apply_in_editor",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    const auto* parsed_node = find_scene_node(*data, "listener");
    ASSERT_NE(parsed_node, nullptr);
    ASSERT_EQ(parsed_node->behaviors.size(), 1u);
    EXPECT_TRUE(parsed_node->behaviors[0].apply_in_editor);

    const std::string reparsed_export =
        wz::json::serialize_json(export_scene_to_json_document(*data));
    EXPECT_NE(
        reparsed_export.find("\"apply_in_editor\""),
        std::string::npos);
}

TEST(SceneAssetModule, ParsesExportsAndSummarizesScalarFieldSource)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_scalar_field_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "scalar_field_source_scene",
  "nodes": [
    {
      "id": "root",
      "transform": {
        "translation": [0, 0, 0]
      },
      "scalar_field_source": {
        "kind": "procedural_checkerboard",
        "width": 32,
        "height": 16,
        "depth": 1,
        "frequency": 4.0,
        "amplitude": 2.0
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "scalar_field_source.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "scalar_field_source",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.scalar_field_source.has_value());
    EXPECT_EQ(
        node.scalar_field_source->kind,
        SceneScalarFieldSourceKind::ProceduralCheckerboard);
    EXPECT_EQ(node.scalar_field_source->width, 32u);
    EXPECT_EQ(node.scalar_field_source->height, 16u);
    EXPECT_EQ(node.scalar_field_source->depth, 1u);
    EXPECT_FLOAT_EQ(node.scalar_field_source->frequency, 4.0f);
    EXPECT_FLOAT_EQ(node.scalar_field_source->amplitude, 2.0f);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.scalar_field_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"scalar_field_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"procedural_checkerboard\""), std::string::npos);
    EXPECT_NE(exported.find("\"frequency\""), std::string::npos);
    EXPECT_NE(exported.find("\"amplitude\""), std::string::npos);
}

TEST(SceneAssetModule, ParsesExportsAndSummarizesVectorFieldSource)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_vector_field_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::string scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "vector_field_source_scene",
  "nodes": [
    {
      "id": "normal",
      "transform": {
        "translation": [0, 0, 0]
      },
      "vector_field_source": {
        "kind": "raw_f32",
        "path": "normals.raw",
        "width": 32,
        "height": 16,
        "depth": 1,
        "components_per_channel": 3,
        "channels": ["normal"]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "vector_field_source.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    wz::engine::assets::EngineAssetLibrary assets{
        device,
        logger,
        root,
    };

    using namespace wz::engine::assets;

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "vector_field_source",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);

    const auto& node = scene_data->nodes[0];
    ASSERT_TRUE(node.vector_field_source.has_value());
    EXPECT_EQ(
        node.vector_field_source->kind,
        SceneVectorFieldSourceKind::RawF32);
    EXPECT_EQ(node.vector_field_source->path, "normals.raw");
    EXPECT_EQ(node.vector_field_source->width, 32u);
    EXPECT_EQ(node.vector_field_source->height, 16u);
    EXPECT_EQ(node.vector_field_source->depth, 1u);
    EXPECT_EQ(node.vector_field_source->components_per_channel, 3u);
    ASSERT_EQ(node.vector_field_source->channels.size(), 1u);
    EXPECT_EQ(node.vector_field_source->channels[0].name, "normal");

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.vector_field_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"vector_field_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"raw_f32\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"components_per_channel\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"normal\""), std::string::npos);
}

TEST(SceneAssetModule, TerrainHeightFieldSourceComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_terrain_height_field_source_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const auto field = assets.scalar_fields().create_procedural_scalar_field({
        .name = "terrain/height_field",
        .width = 8,
        .height = 8,
        .depth = 1,
        .generator = ScalarFieldGenerator::GradientY,
    });
    ASSERT_TRUE(field.valid());

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "terrain_height_field_source_scene",
  "nodes": [
    {
      "id": "height",
      "scalar_field_source": {
        "kind": "procedural_gradient_y",
        "width": 8,
        "height": 8,
        "depth": 1
      }
    },
    {
      "id": "terrain",
      "terrain_height_field_source": {
        "mode": "scene_node",
        "source_node": "height",
        "asset": "asset://scalar_fields/height",
        "origin": [-2.0, -3.0],
        "size": [10.0, 12.0],
        "vertical_scale": 4.0,
        "base_height": -1.0
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "terrain_height_field_source.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "terrain_height_field_source",
            .path = rel_path,
            .scalar_field_asset_references = {
                SceneAssetReferenceBinding{
                    .uri = "asset://scalar_fields/height",
                    .key = field.output,
                },
            },
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 2u);

    const auto& node = scene_data->nodes[1];
    ASSERT_TRUE(node.terrain_height_field_source.has_value());
    EXPECT_EQ(
        node.terrain_height_field_source->mode,
        SceneTerrainHeightFieldSourceMode::SceneNode);
    EXPECT_EQ(node.terrain_height_field_source->source_node, "height");
    EXPECT_EQ(node.terrain_height_field_source->scalar_field_asset,
        field.output);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->origin[0], -2.0f);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->origin[1], -3.0f);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->size[0], 10.0f);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->size[1], 12.0f);
    EXPECT_FLOAT_EQ(
        node.terrain_height_field_source->vertical_scale,
        4.0f);
    EXPECT_FLOAT_EQ(node.terrain_height_field_source->base_height, -1.0f);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.scalar_field_sources, 1u);
    EXPECT_EQ(summary.terrain_height_field_sources, 1u);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(
        exported.find("\"terrain_height_field_source\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"scene_node\""), std::string::npos);
    EXPECT_NE(exported.find("\"vertical_scale\""), std::string::npos);
    EXPECT_NE(exported.find("\"base_height\""), std::string::npos);
    EXPECT_NE(exported.find("asset-key:"), std::string::npos);
}

TEST(SceneAssetModule, LightComponentsRoundTripThroughSceneJSON)
{
    using namespace wz::engine::assets;

    wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(),
        "wozzits_scene_light_component_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData authored{};
    authored.name = "light_component_scene";
    SceneNodeAsset node{};
    node.id = "sun";
    node.direct_light_source = SceneDirectLightSourceAsset{
        .kind = DirectLightKind::Spot,
        .color = { 1.0f, 0.8f, 0.6f },
        .intensity = 4.0f,
        .range = 32.0f,
        .inner_cone_radians = 0.25f,
        .outer_cone_radians = 0.75f,
    };
    node.ambient_lighting = SceneAmbientLightingAsset{
        .mode = AmbientLightingMode::FieldModulated,
        .color = { 0.2f, 0.3f, 0.5f },
        .intensity = 0.45f,
        .domain_mapping = AmbientLightingDomainMapping::WorldXZ,
    };
    node.hdri_environment = SceneHDRIEnvironmentAsset{
        .path = "studio.hdr",
        .format = HDRIEnvironmentFormat::RadianceHDR,
        .exposure = 0.5f,
        .rotation_x_radians = 0.125f,
        .rotation_y_radians = 1.25f,
        .rotation_z_radians = -0.25f,
        .lighting_intensity = 0.75f,
        .reflection_intensity = 0.6f,
        .background_intensity = 0.0f,
        .lighting_sample_resolution = 512,
        .dominant_light_direction = { 0.0f, -0.5f, 0.8660254f },
        .dominant_light_color = { 1.0f, 0.92f, 0.82f },
        .dominant_light_intensity = 3.0f,
        .dominant_light_confidence = 0.8f,
    };
    node.sky_visual = SceneSkyVisualAsset{
        .kind = SceneSkyVisualKind::VectorField,
        .gradient_top_color = { 0.1f, 0.2f, 0.3f },
        .gradient_bottom_color = { 0.7f, 0.8f, 0.9f },
        .texture_asset = wz::asset::AssetKey{
            .content_hash = { 1, 2 },
            .schema_hash = { 3, 4 },
            .compiler_hash = { 5, 6 },
            .deps_hash = { 7, 8 },
        },
        .texture_path = "skies/studio.exr",
        .texture_format = HDRIEnvironmentFormat::OpenEXR,
        .vector_field_asset = wz::asset::AssetKey{
            .content_hash = { 11, 12 },
            .schema_hash = { 13, 14 },
            .compiler_hash = { 15, 16 },
            .deps_hash = { 17, 18 },
        },
        .vector_field_node = "wind_field",
        .exposure = -0.5f,
        .rotation_y_radians = 0.25f,
    };
    node.sky_surface = SceneSkySurfaceAsset{
        .projection = SceneSkyProjection::Sphere,
        .radius = 42.0f,
        .visible_to_camera = true,
    };
    authored.nodes.push_back(std::move(node));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"direct_light_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"ambient_lighting\""), std::string::npos);
    EXPECT_NE(exported.find("\"hdri_environment\""), std::string::npos);
    EXPECT_NE(exported.find("\"sky_visual\""), std::string::npos);
    EXPECT_NE(exported.find("\"sky_surface\""), std::string::npos);
    EXPECT_NE(exported.find("\"gradient_top_color\""), std::string::npos);
    EXPECT_NE(exported.find("\"field_modulated\""), std::string::npos);
    EXPECT_NE(exported.find("\"world_xz\""), std::string::npos);
    EXPECT_NE(exported.find("\"radiance_hdr\""), std::string::npos);

    auto rel_path = write_scene_json(
        root, "light_component.scene.json", exported);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "light_component",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].direct_light_source.has_value());
    ASSERT_TRUE(scene_data->nodes[0].ambient_lighting.has_value());
    ASSERT_TRUE(scene_data->nodes[0].hdri_environment.has_value());
    ASSERT_TRUE(scene_data->nodes[0].sky_visual.has_value());
    ASSERT_TRUE(scene_data->nodes[0].sky_surface.has_value());
    EXPECT_EQ(
        scene_data->nodes[0].direct_light_source->kind,
        DirectLightKind::Spot);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].direct_light_source->outer_cone_radians,
        0.75f);
    EXPECT_EQ(
        scene_data->nodes[0].ambient_lighting->mode,
        AmbientLightingMode::FieldModulated);
    EXPECT_EQ(
        scene_data->nodes[0].ambient_lighting->domain_mapping,
        AmbientLightingDomainMapping::WorldXZ);
    EXPECT_EQ(
        scene_data->nodes[0].hdri_environment->format,
        HDRIEnvironmentFormat::RadianceHDR);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].hdri_environment->rotation_x_radians,
        0.125f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].hdri_environment->rotation_y_radians,
        1.25f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].hdri_environment->rotation_z_radians,
        -0.25f);
    EXPECT_EQ(
        scene_data->nodes[0].hdri_environment->lighting_sample_resolution,
        512u);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].hdri_environment->dominant_light_confidence,
        0.8f);
    EXPECT_EQ(
        scene_data->nodes[0].sky_visual->kind,
        SceneSkyVisualKind::VectorField);
    EXPECT_FALSE(
        scene_data->nodes[0].sky_visual->texture_asset
        == wz::asset::AssetKey{});
    EXPECT_EQ(
        scene_data->nodes[0].sky_visual->texture_path,
        "skies/studio.exr");
    EXPECT_EQ(
        scene_data->nodes[0].sky_visual->texture_format,
        HDRIEnvironmentFormat::OpenEXR);
    EXPECT_FALSE(
        scene_data->nodes[0].sky_visual->vector_field_asset
        == wz::asset::AssetKey{});
    EXPECT_EQ(
        scene_data->nodes[0].sky_visual->vector_field_node,
        "wind_field");
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].sky_visual->rotation_y_radians,
        0.25f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].sky_visual->gradient_top_color[1],
        0.2f);
    EXPECT_FLOAT_EQ(
        scene_data->nodes[0].sky_visual->gradient_bottom_color[2],
        0.9f);
    EXPECT_EQ(
        scene_data->nodes[0].sky_surface->projection,
        SceneSkyProjection::Sphere);
    EXPECT_FLOAT_EQ(scene_data->nodes[0].sky_surface->radius, 42.0f);

    const auto components = authored_components_for_node(scene_data->nodes[0]);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::Light), 1);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::AmbientLighting), 1);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::HDRIEnvironment), 1);

    const auto summary = summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(summary.hdri_environments, 1u);

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.hdri_environments, 1u);
}

// Persistence guard for the runtime-authored behavior binding (steps 1-3):
// a node's singular `behavior` and plural `behaviors[]` must survive
// export -> import losslessly, including every config kind (bool/number/
// string) and the events list. WozzitsApp_v1::save_scene re-emits the nodes
// array, so this exporter<->importer round-trip is what makes a behavior
// edited via the runtime survive save -> reload.
TEST(SceneAssetModule, BehaviorBindingConfigAndEventsRoundTripThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_behavior_config_events_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "behavior_config_events_roundtrip";

    SceneNodeAsset node = make_scene_node("actor");

    // Singular `behavior` carrying all config kinds + an events list.
    node.behavior = SceneBehaviorAsset{
        .id = "spin_behavior",
        .label = "Spin behavior",
        .module = "demo",
        .name = "spin",
        .enabled = false,
        .apply_in_editor = true,
        .events = { "tick", "collision.begin" },
        .config = {
            SceneBehaviorConfigValue{
                .key = "loop",
                .kind = SceneBehaviorConfigValueKind::Bool,
                .bool_value = true,
            },
            SceneBehaviorConfigValue{
                .key = "speed",
                .kind = SceneBehaviorConfigValueKind::Number,
                .number_value = 2.5,
            },
            SceneBehaviorConfigValue{
                .key = "axis",
                .kind = SceneBehaviorConfigValueKind::String,
                .string_value = "up",
            },
        },
    };

    // Plural `behaviors[]` entry, also config + events, to prove both the
    // singular and plural exporter/importer paths persist faithfully.
    node.behaviors.push_back(SceneBehaviorAsset{
        .id = "log_behavior",
        .label = "Log behavior",
        .module = "debug",
        .name = "log_events",
        .enabled = true,
        .apply_in_editor = false,
        .events = { "input.key" },
        .config = {
            SceneBehaviorConfigValue{
                .key = "verbose",
                .kind = SceneBehaviorConfigValueKind::Bool,
                .bool_value = false,
            },
            SceneBehaviorConfigValue{
                .key = "threshold",
                .kind = SceneBehaviorConfigValueKind::Number,
                .number_value = -1.25,
            },
            SceneBehaviorConfigValue{
                .key = "prefix",
                .kind = SceneBehaviorConfigValueKind::String,
                .string_value = "dbg:",
            },
        },
    });
    authored.nodes.push_back(std::move(node));

    // Export -> the serializer must emit behavior/behaviors + config + events.
    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"behavior\""), std::string::npos);
    EXPECT_NE(exported.find("\"behaviors\""), std::string::npos);
    EXPECT_NE(exported.find("\"config\""), std::string::npos);
    EXPECT_NE(exported.find("\"events\""), std::string::npos);
    EXPECT_NE(exported.find("\"axis\""), std::string::npos);
    EXPECT_NE(exported.find("\"collision.begin\""), std::string::npos);

    auto rel_path = write_scene_json(
        root,
        "behavior_config_events.scene.json",
        exported);

    // Re-import the exported JSON through the asset pipeline.
    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "behavior_config_events",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);

    const auto* parsed = find_scene_node(*data, "actor");
    ASSERT_NE(parsed, nullptr);

    // Helper: assert a parsed binding matches the authored one field by field.
    const auto expect_config_entry =
        [](const SceneBehaviorAsset& behavior,
           const std::string& key,
           SceneBehaviorConfigValueKind kind)
        -> const SceneBehaviorConfigValue&
    {
        const auto it = std::find_if(
            behavior.config.begin(),
            behavior.config.end(),
            [&](const SceneBehaviorConfigValue& v) { return v.key == key; });
        EXPECT_NE(it, behavior.config.end())
            << "missing config key: " << key;
        EXPECT_EQ(it->kind, kind) << "config key kind mismatch: " << key;
        return *it;
    };

    // ── Singular behavior ────────────────────────────────────────────────
    ASSERT_TRUE(parsed->behavior.has_value());
    const auto& singular = *parsed->behavior;
    EXPECT_EQ(singular.id, "spin_behavior");
    EXPECT_EQ(singular.label, "Spin behavior");
    EXPECT_EQ(singular.module, "demo");
    EXPECT_EQ(singular.name, "spin");
    EXPECT_FALSE(singular.enabled);
    EXPECT_TRUE(singular.apply_in_editor);
    ASSERT_EQ(singular.events.size(), 2u);
    EXPECT_EQ(singular.events[0], "tick");
    EXPECT_EQ(singular.events[1], "collision.begin");
    ASSERT_EQ(singular.config.size(), 3u);
    EXPECT_TRUE(expect_config_entry(
        singular, "loop", SceneBehaviorConfigValueKind::Bool).bool_value);
    EXPECT_DOUBLE_EQ(
        expect_config_entry(
            singular, "speed", SceneBehaviorConfigValueKind::Number)
            .number_value,
        2.5);
    EXPECT_EQ(
        expect_config_entry(
            singular, "axis", SceneBehaviorConfigValueKind::String)
            .string_value,
        "up");

    // ── Plural behaviors[] ───────────────────────────────────────────────
    ASSERT_EQ(parsed->behaviors.size(), 1u);
    const auto& plural = parsed->behaviors[0];
    EXPECT_EQ(plural.id, "log_behavior");
    EXPECT_EQ(plural.label, "Log behavior");
    EXPECT_EQ(plural.module, "debug");
    EXPECT_EQ(plural.name, "log_events");
    EXPECT_TRUE(plural.enabled);
    EXPECT_FALSE(plural.apply_in_editor);
    ASSERT_EQ(plural.events.size(), 1u);
    EXPECT_EQ(plural.events[0], "input.key");
    ASSERT_EQ(plural.config.size(), 3u);
    EXPECT_FALSE(expect_config_entry(
        plural, "verbose", SceneBehaviorConfigValueKind::Bool).bool_value);
    EXPECT_DOUBLE_EQ(
        expect_config_entry(
            plural, "threshold", SceneBehaviorConfigValueKind::Number)
            .number_value,
        -1.25);
    EXPECT_EQ(
        expect_config_entry(
            plural, "prefix", SceneBehaviorConfigValueKind::String)
            .string_value,
        "dbg:");

    // Re-export the imported scene: a second round trip stays lossless.
    const std::string reparsed_export =
        wz::json::serialize_json(export_scene_to_json_document(*data));
    EXPECT_EQ(reparsed_export, exported);
}

// Issue #213: the GLB scene-source DESCRIPTOR (path + scene_index +
// consume_mode + per-component style mapping) round-trips through scene JSON.
// This is the authored intent only — create_scene_from_json compiles the JSON
// back to SceneAssetData; the descriptor is re-resolved into a Scene asset at
// materialization (WozzitsApp_v1::resolve_glb_scene_sources), so scene_source
// stays unset here. Mirrors the SceneImportSource round-trip above.
TEST(SceneAssetModule, GlbSceneSourceDescriptorRoundTripsThroughSceneJSON)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_glb_scene_source_descriptor_roundtrip_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    SceneAssetData authored{};
    authored.name = "glb_scene_source_descriptor_roundtrip";

    SceneNodeAsset host = make_scene_node("tank_host");

    SceneGLBSceneSource source{};
    source.path = "gltf/test-mesh-a.glb";
    source.scene_index = 0u;
    source.consume_mode = SceneSourceConsumeMode::Flatten;

    // A non-default base style so the layer/alpha/depth fields are observable.
    MeshRenderStyleData base{};
    base.wireframe.enabled = false;
    base.surface.enabled = true;
    base.surface.color[0] = 0.10f;
    base.surface.color[1] = 0.20f;
    base.surface.color[2] = 0.30f;
    base.surface.color[3] = 1.0f;
    base.surface.emissive_strength = 0.5f;
    base.alpha = 0.75f;
    base.depth_test = true;
    base.depth_write = true;
    base.double_sided = false;
    base.hidden_line_prepass = false;
    source.base_style = base;

    // One per-mesh-index override with distinct (steel-ish) values.
    MeshRenderStyleData steel{};
    steel.surface.enabled = true;
    steel.surface.color[0] = 0.6f;
    steel.surface.color[1] = 0.6f;
    steel.surface.color[2] = 0.62f;
    steel.surface.color[3] = 1.0f;
    steel.wireframe.enabled = false;
    steel.alpha = 1.0f;
    source.style_overrides.push_back(
        SceneGLBSceneSourceStyleOverride{ .mesh_index = 1u, .style = steel });

    attach_glb_scene_source(host, std::move(source));
    authored.nodes.push_back(std::move(host));

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(authored));
    EXPECT_NE(exported.find("\"glb_scene_source\""), std::string::npos);
    EXPECT_NE(exported.find("\"gltf/test-mesh-a.glb\""), std::string::npos);
    EXPECT_NE(exported.find("\"flatten\""), std::string::npos);
    EXPECT_NE(exported.find("\"style_overrides\""), std::string::npos);

    auto rel_path = write_scene_json(
        root,
        "glb_scene_source.scene.json",
        exported);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "glb_scene_source",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(data, nullptr);
    ASSERT_EQ(data->nodes.size(), 1u);

    const auto* parsed = find_scene_node(*data, "tank_host");
    ASSERT_NE(parsed, nullptr);
    ASSERT_TRUE(parsed->glb_scene_source.has_value());
    // The descriptor is authored intent; the resolved Scene key is filled in at
    // materialization, not by the JSON compiler.
    EXPECT_FALSE(parsed->scene_source.has_value());
    EXPECT_FALSE(parsed->scene_source_node_id.has_value());

    const SceneGLBSceneSource& got = *parsed->glb_scene_source;
    EXPECT_EQ(got.path, "gltf/test-mesh-a.glb");
    EXPECT_EQ(got.scene_index, 0u);
    EXPECT_EQ(got.consume_mode, SceneSourceConsumeMode::Flatten);

    ASSERT_TRUE(got.base_style.has_value());
    EXPECT_FALSE(got.base_style->wireframe.enabled);
    EXPECT_TRUE(got.base_style->surface.enabled);
    EXPECT_FLOAT_EQ(got.base_style->surface.color[0], 0.10f);
    EXPECT_FLOAT_EQ(got.base_style->surface.color[2], 0.30f);
    EXPECT_FLOAT_EQ(got.base_style->surface.emissive_strength, 0.5f);
    EXPECT_FLOAT_EQ(got.base_style->alpha, 0.75f);
    EXPECT_TRUE(got.base_style->depth_test);
    EXPECT_TRUE(got.base_style->depth_write);
    EXPECT_FALSE(got.base_style->double_sided);
    EXPECT_FALSE(got.base_style->hidden_line_prepass);

    ASSERT_EQ(got.style_overrides.size(), 1u);
    EXPECT_EQ(got.style_overrides[0].mesh_index, 1u);
    EXPECT_TRUE(got.style_overrides[0].style.surface.enabled);
    EXPECT_FLOAT_EQ(got.style_overrides[0].style.surface.color[0], 0.6f);
    EXPECT_FLOAT_EQ(got.style_overrides[0].style.surface.color[2], 0.62f);

    // A second round trip stays lossless (export of the parsed scene matches).
    const std::string reparsed_export =
        wz::json::serialize_json(export_scene_to_json_document(*data));
    EXPECT_EQ(reparsed_export, exported);
}

// Issue #287: the render-to-texture source must survive a save. A component the
// engine reads but does not WRITE is the "my component disappeared" failure --
// the editor rewrites scene.json through this exporter on every save, so a
// missing export branch silently deletes hand-authored intent.
TEST(SceneAssetModule, RenderToTextureComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_render_to_texture_component_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{ device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "render_to_texture_component_scene",
  "nodes": [
    {
      "id": "card_art",
      "render_to_texture": {
        "target_asset_node_id": 42,
        "include_descendants": false,
        "also_draw_in_scene": true,
        "enabled": false
      }
    },
    {
      "id": "defaults_only",
      "render_to_texture": { "target_asset_node_id": 7 }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "render_to_texture_component.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "render_to_texture_component",
            .path = rel_path,
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 2u);

    // Every authored dial survives, including the two that are non-default --
    // a reader that dropped them would still "work" and quietly change what is
    // drawn where.
    const auto& authored = scene_data->nodes[0];
    ASSERT_TRUE(authored.render_to_texture.has_value());
    ASSERT_TRUE(authored.render_to_texture->target_node_id.has_value());
    EXPECT_EQ(*authored.render_to_texture->target_node_id, 42u);
    EXPECT_FALSE(authored.render_to_texture->include_descendants);
    EXPECT_TRUE(authored.render_to_texture->also_draw_in_scene);
    EXPECT_FALSE(authored.render_to_texture->enabled);
    // The resolved target key is a bridge product, never read from the scene.
    EXPECT_TRUE(authored.render_to_texture->target == wz::asset::AssetKey{});

    const auto& defaults = scene_data->nodes[1];
    ASSERT_TRUE(defaults.render_to_texture.has_value());
    ASSERT_TRUE(defaults.render_to_texture->target_node_id.has_value());
    EXPECT_EQ(*defaults.render_to_texture->target_node_id, 7u);
    EXPECT_TRUE(defaults.render_to_texture->include_descendants);
    EXPECT_FALSE(defaults.render_to_texture->also_draw_in_scene);
    EXPECT_TRUE(defaults.render_to_texture->enabled);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"render_to_texture\""), std::string::npos);
    EXPECT_NE(exported.find("\"target_asset_node_id\""), std::string::npos);

    // Re-reading the export must produce the identical scene -- the round trip
    // an editor save/reload cycle actually performs.
    auto rel_path2 = write_scene_json(
        root, "render_to_texture_component_reexport.scene.json", exported);
    const auto reparsed =
        assets.scenes().create_scene_from_json({
            .name = "render_to_texture_component_reexport",
            .path = rel_path2,
        });
    ASSERT_TRUE(reparsed.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());
    const auto* data2 = assets.scenes().get_scene_data(
        assets.scenes().get_scene(reparsed));
    ASSERT_NE(data2, nullptr);
    ASSERT_EQ(data2->nodes.size(), 2u);
    ASSERT_TRUE(data2->nodes[0].render_to_texture.has_value());
    EXPECT_EQ(*data2->nodes[0].render_to_texture->target_node_id, 42u);
    EXPECT_FALSE(data2->nodes[0].render_to_texture->include_descendants);
    EXPECT_TRUE(data2->nodes[0].render_to_texture->also_draw_in_scene);
    EXPECT_FALSE(data2->nodes[0].render_to_texture->enabled);

    const std::string reparsed_export =
        wz::json::serialize_json(export_scene_to_json_document(*data2));
    EXPECT_EQ(reparsed_export, exported);
}
