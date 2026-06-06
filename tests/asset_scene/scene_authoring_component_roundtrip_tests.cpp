#include "scene_asset_module_test_support.h"

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
    ASSERT_TRUE(
        reparsed_scene_data->nodes[1].terrain_mesh_source.has_value());
    EXPECT_EQ(
        reparsed_scene_data->nodes[1].terrain_mesh_source->source_node,
        "source_mesh");

    auto result = instantiate_scene(*scene_data);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    const auto runtime_summary =
        summarize_scene_instance_components(result.instance);
    EXPECT_EQ(runtime_summary.terrain_mesh_sources, 0u);
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
        .path = "gltf/tank1.glb",
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
    EXPECT_EQ(parsed_anchor->scene_import_source->path, "gltf/tank1.glb");
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
}

