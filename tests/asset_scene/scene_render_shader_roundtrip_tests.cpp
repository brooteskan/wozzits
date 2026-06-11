#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, RenderShaderComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_render_shader_roundtrip_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "render_shader_scene",
  "nodes": [
    {
      "id": "custom_mesh",
      "render_shader": {
        "program_id": "mesh/custom_surface",
        "vertex_hlsl_path": "shaders/render/custom_mesh_vs.hlsl",
        "pixel_hlsl_path": "shaders/render/custom_mesh_ps.hlsl",
        "vertex_entry": "vs_main",
        "pixel_entry": "ps_main",
        "vertex_target": "vs_6_0",
        "pixel_target": "ps_6_0",
        "binding_model": "mesh_ia",
        "input_layout": "mesh_position_normal_uv",
        "blend": "opaque",
        "depth": "test_write",
        "raster": "solid_cull_none",
        "descriptor_bindings": [
          {
            "kind": "structured_buffer_srv",
            "visibility": "pixel",
            "semantic": "mesh_field_visualization",
            "shader_register": 0,
            "register_space": 0,
            "descriptor_count": 1
          }
        ]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "render_shader.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "render_shader",
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
    ASSERT_TRUE(node.render_shader.has_value());
    const auto& shader = *node.render_shader;
    EXPECT_EQ(shader.program_id, "mesh/custom_surface");
    EXPECT_EQ(shader.vertex_hlsl_path,
        "shaders/render/custom_mesh_vs.hlsl");
    EXPECT_EQ(shader.pixel_hlsl_path,
        "shaders/render/custom_mesh_ps.hlsl");
    EXPECT_EQ(shader.vertex_entry, "vs_main");
    EXPECT_EQ(shader.pixel_entry, "ps_main");
    EXPECT_EQ(shader.vertex_target, "vs_6_0");
    EXPECT_EQ(shader.pixel_target, "ps_6_0");
    EXPECT_EQ(shader.binding_model, "mesh_ia");
    EXPECT_EQ(shader.input_layout, "mesh_position_normal_uv");
    EXPECT_EQ(shader.blend, "opaque");
    EXPECT_EQ(shader.depth, "test_write");
    EXPECT_EQ(shader.raster, "solid_cull_none");
    ASSERT_EQ(shader.descriptor_bindings.size(), 1u);
    EXPECT_EQ(shader.descriptor_bindings[0].kind, "structured_buffer_srv");
    EXPECT_EQ(shader.descriptor_bindings[0].visibility, "pixel");
    EXPECT_EQ(
        shader.descriptor_bindings[0].semantic,
        "mesh_field_visualization");
    EXPECT_EQ(shader.descriptor_bindings[0].shader_register, 0u);
    EXPECT_EQ(shader.descriptor_bindings[0].register_space, 0u);
    EXPECT_EQ(shader.descriptor_bindings[0].descriptor_count, 1u);

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::RenderShader), 1);
    EXPECT_EQ(
        wz::scene::scene_component_domain(
            wz::scene::SceneAuthoredComponentKind::RenderShader),
        wz::scene::SceneComponentDomain::Exportable);
    EXPECT_TRUE(has_runtime_relevant_components(node));

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.render_shaders, 1u);
    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.render_shaders, 1u);

    auto instance_result = instantiate_scene(*scene_data);
    ASSERT_TRUE(instance_result.ok()) << instance_result.error_detail;
    const auto runtime_summary =
        summarize_scene_instance_components(instance_result.instance);
    EXPECT_EQ(runtime_summary.render_shaders, 1u);

    const uint64_t original_fingerprint =
        scene_asset_fingerprint(*scene_data);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"render_shader\""), std::string::npos);
    EXPECT_NE(exported.find("\"mesh/custom_surface\""), std::string::npos);
    EXPECT_NE(exported.find("\"vertex_hlsl_path\""), std::string::npos);
    EXPECT_NE(exported.find("\"pixel_hlsl_path\""), std::string::npos);
    EXPECT_NE(exported.find("\"binding_model\""), std::string::npos);
    EXPECT_NE(exported.find("\"solid_cull_none\""), std::string::npos);
    EXPECT_NE(exported.find("\"descriptor_bindings\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"mesh_field_visualization\""),
        std::string::npos);

    const wz::fs::Path reparse_root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_render_shader_reparse_test");
    ASSERT_EQ(
        wz::fs::create_directories(reparse_root),
        wz::fs::FileError::None);

    wz::Logger reparse_logger;
    wz::gpu::Device reparse_device{};
    EngineAssetLibrary reparse_assets{
        reparse_device, reparse_logger, reparse_root };

    auto exported_rel_path = write_scene_json(
        reparse_root,
        "render_shader_exported.scene.json",
        exported);
    const auto exported_scene_asset =
        reparse_assets.scenes().create_scene_from_json({
            .name = "render_shader_exported",
            .path = exported_rel_path,
        });
    ASSERT_TRUE(exported_scene_asset.valid());
    ASSERT_TRUE(reparse_assets.commit());
    ASSERT_TRUE(reparse_assets.resolve_all().ok());

    const auto* reparsed_scene_data = reparse_assets.scenes().get_scene_data(
        reparse_assets.scenes().get_scene(exported_scene_asset));
    ASSERT_NE(reparsed_scene_data, nullptr);
    ASSERT_EQ(reparsed_scene_data->nodes.size(), 1u);
    ASSERT_TRUE(reparsed_scene_data->nodes[0].render_shader.has_value());
    EXPECT_EQ(
        scene_asset_fingerprint(*reparsed_scene_data),
        original_fingerprint);
    EXPECT_EQ(
        reparsed_scene_data->nodes[0].render_shader->program_id,
        "mesh/custom_surface");
    EXPECT_EQ(
        reparsed_scene_data->nodes[0].render_shader->vertex_entry,
        "vs_main");
    EXPECT_EQ(
        reparsed_scene_data->nodes[0].render_shader->raster,
        "solid_cull_none");
    ASSERT_EQ(
        reparsed_scene_data->nodes[0].render_shader->descriptor_bindings
            .size(),
        1u);
    EXPECT_EQ(
        reparsed_scene_data->nodes[0].render_shader->descriptor_bindings[0]
            .semantic,
        "mesh_field_visualization");
}

TEST(SceneAssetModule, RenderShaderDefaultsApplyWhenFieldsOmitted)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_render_shader_defaults_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "render_shader_defaults",
  "nodes": [
    {
      "id": "minimal",
      "render_shader": {
        "program_id": "mesh/minimal",
        "vertex_hlsl_path": "shaders/render/vs.hlsl",
        "pixel_hlsl_path": "shaders/render/ps.hlsl"
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "render_shader_defaults.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "render_shader_defaults",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].render_shader.has_value());

    const auto& shader = *scene_data->nodes[0].render_shader;
    EXPECT_EQ(shader.vertex_entry, "main");
    EXPECT_EQ(shader.pixel_entry, "main");
    EXPECT_EQ(shader.vertex_target, "vs_5_0");
    EXPECT_EQ(shader.pixel_target, "ps_5_0");
    EXPECT_EQ(shader.binding_model, "mesh_ia");
    EXPECT_EQ(shader.input_layout, "mesh_position_normal_uv");
    EXPECT_EQ(shader.blend, "opaque");
    EXPECT_EQ(shader.depth, "test_write");
    EXPECT_EQ(shader.raster, "solid_cull_none");
}

TEST(SceneAssetModule, RenderShaderRejectsMissingProgramId)
{
    using namespace wz::engine::assets;

    EXPECT_FALSE(scene_json_compiles(
        "render_shader_no_program_id",
        R"({
  "schema": "wozzits.scene.v0",
  "name": "test",
  "nodes": [{
    "id": "n",
    "render_shader": {
      "vertex_hlsl_path": "v.hlsl",
      "pixel_hlsl_path": "p.hlsl"
    }
  }]
})"));
}

TEST(SceneAssetModule, RenderShaderRejectsMissingVertexPath)
{
    using namespace wz::engine::assets;

    EXPECT_FALSE(scene_json_compiles(
        "render_shader_no_vertex",
        R"({
  "schema": "wozzits.scene.v0",
  "name": "test",
  "nodes": [{
    "id": "n",
    "render_shader": {
      "program_id": "test/prog",
      "pixel_hlsl_path": "p.hlsl"
    }
  }]
})"));
}

TEST(SceneAssetModule, RenderShaderRejectsMissingPixelPath)
{
    using namespace wz::engine::assets;

    EXPECT_FALSE(scene_json_compiles(
        "render_shader_no_pixel",
        R"({
  "schema": "wozzits.scene.v0",
  "name": "test",
  "nodes": [{
    "id": "n",
    "render_shader": {
      "program_id": "test/prog",
      "vertex_hlsl_path": "v.hlsl"
    }
  }]
})"));
}
