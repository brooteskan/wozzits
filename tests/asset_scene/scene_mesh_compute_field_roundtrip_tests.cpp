#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, MeshComputeFieldComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_compute_field_roundtrip_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_compute_field_scene",
  "nodes": [
    {
      "id": "field_mesh",
      "mesh_source": {
        "kind": "procedural_cube"
      },
      "mesh_compute_field": {
        "enabled": true,
        "kernel_id": "project/scaled_height",
        "hlsl_path": "shaders/compute/scaled_height_cs.hlsl",
        "entry": "main",
        "target": "cs_5_0",
        "thread_group_size": [64, 1, 1],
        "inputs": ["positions", "indices"],
        "channels": [
          { "channel_id": 8192, "value_type": "float1" },
          { "channel_id": 8193, "value_type": "uint1" }
        ],
        "params": [1069547520]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_compute_field.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "mesh_compute_field",
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
    ASSERT_TRUE(node.mesh_compute_field.has_value());
    const auto& field = *node.mesh_compute_field;
    EXPECT_TRUE(field.enabled);
    EXPECT_EQ(field.kernel_id, "project/scaled_height");
    EXPECT_EQ(field.hlsl_path, "shaders/compute/scaled_height_cs.hlsl");
    EXPECT_EQ(field.entry, "main");
    EXPECT_EQ(field.target, "cs_5_0");
    EXPECT_EQ(field.thread_group_size_x, 64u);
    EXPECT_EQ(field.thread_group_size_y, 1u);
    EXPECT_EQ(field.thread_group_size_z, 1u);
    ASSERT_EQ(field.inputs.size(), 2u);
    EXPECT_EQ(field.inputs[0], MeshComputeInput::Positions);
    EXPECT_EQ(field.inputs[1], MeshComputeInput::Indices);
    ASSERT_EQ(field.channels.size(), 2u);
    EXPECT_EQ(field.channels[0].channel_id, 8192u);
    EXPECT_EQ(
        field.channels[0].value_type,
        MeshDerivedFieldValueType::Float1);
    EXPECT_EQ(field.channels[1].channel_id, 8193u);
    EXPECT_EQ(
        field.channels[1].value_type,
        MeshDerivedFieldValueType::UInt1);
    ASSERT_EQ(field.params.size(), 1u);
    EXPECT_EQ(field.params[0], 1069547520u);
    EXPECT_EQ(field.field_asset, wz::asset::AssetKey{});

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::MeshComputeField), 1);
    EXPECT_EQ(
        wz::scene::scene_component_domain(
            wz::scene::SceneAuthoredComponentKind::MeshComputeField),
        wz::scene::SceneComponentDomain::EditorAuthoring);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.mesh_compute_fields, 1u);
    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.mesh_compute_fields, 1u);

    const uint64_t original_fingerprint =
        scene_asset_fingerprint(*scene_data);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"mesh_compute_field\""), std::string::npos);
    EXPECT_NE(exported.find("\"project/scaled_height\""), std::string::npos);
    EXPECT_NE(exported.find("\"positions\""), std::string::npos);
    EXPECT_NE(exported.find("\"indices\""), std::string::npos);
    EXPECT_NE(exported.find("\"channel_id\""), std::string::npos);
    EXPECT_NE(exported.find("\"uint1\""), std::string::npos);
    EXPECT_NE(exported.find("\"params\""), std::string::npos);

    const wz::fs::Path reparse_root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_compute_field_reparse_test");
    ASSERT_EQ(
        wz::fs::create_directories(reparse_root),
        wz::fs::FileError::None);

    wz::Logger reparse_logger;
    wz::gpu::Device reparse_device{};
    EngineAssetLibrary reparse_assets{
        reparse_device, reparse_logger, reparse_root };

    auto exported_rel_path = write_scene_json(
        reparse_root,
        "mesh_compute_field_exported.scene.json",
        exported);
    const auto exported_scene_asset =
        reparse_assets.scenes().create_scene_from_json({
            .name = "mesh_compute_field_exported",
            .path = exported_rel_path,
        });
    ASSERT_TRUE(exported_scene_asset.valid());
    ASSERT_TRUE(reparse_assets.commit());
    ASSERT_TRUE(reparse_assets.resolve_all().ok());

    const auto* reparsed_scene_data = reparse_assets.scenes().get_scene_data(
        reparse_assets.scenes().get_scene(exported_scene_asset));
    ASSERT_NE(reparsed_scene_data, nullptr);
    ASSERT_EQ(reparsed_scene_data->nodes.size(), 1u);
    ASSERT_TRUE(reparsed_scene_data->nodes[0].mesh_compute_field.has_value());
    EXPECT_EQ(
        scene_asset_fingerprint(*reparsed_scene_data),
        original_fingerprint);
    EXPECT_EQ(
        reparsed_scene_data->nodes[0].mesh_compute_field->channels[1]
            .value_type,
        wz::engine::assets::MeshDerivedFieldValueType::UInt1);
}

TEST(SceneAssetModule, MeshComputeFieldFingerprintChangesWithRecipe)
{
    using namespace wz::engine::assets;

    SceneAssetData scene{};
    scene.name = "mesh_compute_field_fingerprint";
    SceneNodeAsset node = make_scene_node("field_mesh");
    attach_mesh_source(node, SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    });
    attach_mesh_compute_field(node, SceneMeshComputeFieldAsset{
        .kernel_id = "project/kernel",
        .hlsl_path = "shaders/kernel_cs.hlsl",
        .thread_group_size_x = 64,
        .inputs = { MeshComputeInput::Positions },
        .channels = {
            SceneMeshComputeFieldChannelAsset{ .channel_id = 0x2000u },
        },
        .params = { 7u },
    });
    scene.nodes.push_back(node);
    const uint64_t base = scene_asset_fingerprint(scene);

    scene.nodes[0].mesh_compute_field->params[0] = 8u;
    EXPECT_NE(scene_asset_fingerprint(scene), base);
    scene.nodes[0].mesh_compute_field->params[0] = 7u;
    EXPECT_EQ(scene_asset_fingerprint(scene), base);

    scene.nodes[0].mesh_compute_field->hlsl_path = "shaders/other_cs.hlsl";
    EXPECT_NE(scene_asset_fingerprint(scene), base);
    scene.nodes[0].mesh_compute_field->hlsl_path = "shaders/kernel_cs.hlsl";

    scene.nodes[0].mesh_compute_field->channels[0].channel_id = 0x2001u;
    EXPECT_NE(scene_asset_fingerprint(scene), base);
    scene.nodes[0].mesh_compute_field->channels[0].channel_id = 0x2000u;

    scene.nodes[0].mesh_compute_field->inputs.push_back(
        MeshComputeInput::Normals);
    EXPECT_NE(scene_asset_fingerprint(scene), base);
    scene.nodes[0].mesh_compute_field->inputs.pop_back();

    EXPECT_EQ(scene_asset_fingerprint(scene), base);
}

TEST(SceneAssetModule, MeshComputeFieldParseRejectsMissingChannels)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_mesh_compute_field_missing_channels_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "mesh_compute_field_missing_channels",
  "nodes": [
    {
      "id": "field_mesh",
      "mesh_compute_field": {
        "kernel_id": "project/kernel",
        "hlsl_path": "shaders/kernel_cs.hlsl"
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "mesh_compute_field_missing_channels.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "mesh_compute_field_missing_channels",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    EXPECT_FALSE(assets.resolve_all().ok());
}
