#include "scene_asset_module_test_support.h"

TEST(SceneAssetModule, ComputeKernelComponentRoundTripsThroughSceneJSON)
{
    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_compute_kernel_roundtrip_test");

    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};

    wz::engine::assets::EngineAssetLibrary assets{
        device, logger, root };

    using namespace wz::engine::assets;

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "compute_kernel_scene",
  "nodes": [
    {
      "id": "debug_kernel",
      "compute_kernel": {
        "kernel_id": "project/debug_multiply_u32",
        "hlsl_path": "shaders/compute/debug_multiply_u32_cs.hlsl",
        "entry": "main",
        "target": "cs_5_0",
        "thread_group_size": [64, 1, 1],
        "ports": [
          {
            "name": "input",
            "kind": "structured_buffer",
            "direction": "input",
            "binding_kind": "srv",
            "shader_register": 0,
            "register_space": 0,
            "stride_bytes": 4
          },
          {
            "name": "output",
            "kind": "structured_buffer",
            "direction": "output",
            "binding_kind": "uav",
            "shader_register": 0,
            "register_space": 0,
            "stride_bytes": 4
          },
          {
            "name": "factor",
            "kind": "u32",
            "direction": "input",
            "offset": 0,
            "dword_count": 1
          },
          {
            "name": "count",
            "kind": "u32",
            "direction": "input",
            "root_constant_offset": 1,
            "root_constant_dwords": 1
          }
        ]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "compute_kernel.scene.json", scene_json);

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "compute_kernel",
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
    ASSERT_TRUE(node.compute_kernel.has_value());
    const auto& kernel = *node.compute_kernel;
    EXPECT_EQ(kernel.kernel_id, "project/debug_multiply_u32");
    EXPECT_EQ(
        kernel.hlsl_path,
        "shaders/compute/debug_multiply_u32_cs.hlsl");
    EXPECT_EQ(kernel.entry, "main");
    EXPECT_EQ(kernel.target, "cs_5_0");
    EXPECT_EQ(kernel.thread_group_size_x, 64u);
    EXPECT_EQ(kernel.thread_group_size_y, 1u);
    EXPECT_EQ(kernel.thread_group_size_z, 1u);
    ASSERT_EQ(kernel.ports.size(), 4u);

    EXPECT_EQ(kernel.ports[0].name, "input");
    EXPECT_EQ(
        kernel.ports[0].kind,
        SceneComputeKernelPortKind::StructuredBuffer);
    EXPECT_EQ(
        kernel.ports[0].direction,
        SceneComputeKernelPortDirection::Input);
    EXPECT_EQ(
        kernel.ports[0].binding_kind,
        SceneComputeKernelBindingKind::SRV);
    EXPECT_EQ(kernel.ports[0].shader_register, 0u);
    EXPECT_EQ(kernel.ports[0].register_space, 0u);
    EXPECT_EQ(kernel.ports[0].stride_bytes, 4u);

    EXPECT_EQ(
        kernel.ports[1].binding_kind,
        SceneComputeKernelBindingKind::UAV);
    EXPECT_EQ(
        kernel.ports[1].direction,
        SceneComputeKernelPortDirection::Output);

    EXPECT_EQ(kernel.ports[2].name, "factor");
    EXPECT_EQ(kernel.ports[2].kind, SceneComputeKernelPortKind::U32);
    EXPECT_EQ(kernel.ports[2].root_constant_offset, 0u);
    EXPECT_EQ(kernel.ports[2].root_constant_dwords, 1u);
    EXPECT_EQ(kernel.ports[3].root_constant_offset, 1u);
    EXPECT_EQ(kernel.ports[3].root_constant_dwords, 1u);

    const auto components = authored_components_for_node(node);
    EXPECT_EQ(std::count(
        components.begin(),
        components.end(),
        wz::scene::SceneAuthoredComponentKind::ComputeKernel), 1);
    EXPECT_EQ(
        wz::scene::scene_component_domain(
            wz::scene::SceneAuthoredComponentKind::ComputeKernel),
        wz::scene::SceneComponentDomain::Exportable);

    const auto recipe_summary =
        summarize_scene_asset_authoring_recipes(*scene_data);
    EXPECT_EQ(recipe_summary.compute_kernels, 1u);
    const auto authored_summary =
        summarize_authored_scene_components(*scene_data);
    EXPECT_EQ(authored_summary.compute_kernels, 1u);

    auto instance_result = instantiate_scene(*scene_data);
    ASSERT_TRUE(instance_result.ok()) << instance_result.error_detail;
    const auto runtime_summary =
        summarize_scene_instance_components(instance_result.instance);
    EXPECT_EQ(runtime_summary.compute_kernels, 1u);

    const uint64_t original_fingerprint =
        scene_asset_fingerprint(*scene_data);

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"compute_kernel\""), std::string::npos);
    EXPECT_NE(
        exported.find("\"project/debug_multiply_u32\""),
        std::string::npos);
    EXPECT_NE(exported.find("\"thread_group_size\""), std::string::npos);
    EXPECT_NE(exported.find("\"structured_buffer\""), std::string::npos);
    EXPECT_NE(exported.find("\"root_constant_offset\""), std::string::npos);
    EXPECT_NE(exported.find("\"root_constant_dwords\""), std::string::npos);

    const wz::fs::Path reparse_root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_compute_kernel_reparse_test");
    ASSERT_EQ(
        wz::fs::create_directories(reparse_root),
        wz::fs::FileError::None);

    wz::Logger reparse_logger;
    wz::gpu::Device reparse_device{};
    EngineAssetLibrary reparse_assets{
        reparse_device, reparse_logger, reparse_root };

    auto exported_rel_path = write_scene_json(
        reparse_root,
        "compute_kernel_exported.scene.json",
        exported);
    const auto exported_scene_asset =
        reparse_assets.scenes().create_scene_from_json({
            .name = "compute_kernel_exported",
            .path = exported_rel_path,
        });
    ASSERT_TRUE(exported_scene_asset.valid());
    ASSERT_TRUE(reparse_assets.commit());
    ASSERT_TRUE(reparse_assets.resolve_all().ok());

    const auto* reparsed_scene_data = reparse_assets.scenes().get_scene_data(
        reparse_assets.scenes().get_scene(exported_scene_asset));
    ASSERT_NE(reparsed_scene_data, nullptr);
    ASSERT_EQ(reparsed_scene_data->nodes.size(), 1u);
    ASSERT_TRUE(reparsed_scene_data->nodes[0].compute_kernel.has_value());
    EXPECT_EQ(
        scene_asset_fingerprint(*reparsed_scene_data),
        original_fingerprint);
    EXPECT_EQ(
        reparsed_scene_data->nodes[0]
            .compute_kernel->ports[1].binding_kind,
        SceneComputeKernelBindingKind::UAV);
}

TEST(SceneAssetModule, ComputeKernelThreadGroupArrayBeatsFallbackFields)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_compute_kernel_thread_group_precedence_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "compute_kernel_thread_group_precedence",
  "nodes": [
    {
      "id": "kernel",
      "compute_kernel": {
        "kernel_id": "project/kernel",
        "hlsl_path": "shaders/kernel.hlsl",
        "thread_group_size": [8, 2, 1],
        "thread_group_size_x": 64,
        "thread_group_size_y": 64,
        "thread_group_size_z": 64
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "compute_kernel_thread_group_precedence.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "compute_kernel_thread_group_precedence",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].compute_kernel.has_value());

    const auto& kernel = *scene_data->nodes[0].compute_kernel;
    EXPECT_EQ(kernel.thread_group_size_x, 8u);
    EXPECT_EQ(kernel.thread_group_size_y, 2u);
    EXPECT_EQ(kernel.thread_group_size_z, 1u);
}

TEST(SceneAssetModule, ComputeKernelPortsAreOptionalAndExportOmitted)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_compute_kernel_optional_ports_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "compute_kernel_optional_ports",
  "nodes": [
    {
      "id": "kernel",
      "compute_kernel": {
        "kernel_id": "project/kernel",
        "hlsl_path": "shaders/compute/kernel.hlsl",
        "entry": "main",
        "target": "cs_5_0",
        "thread_group_size": [8, 1, 1]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "compute_kernel_optional_ports.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "compute_kernel_optional_ports",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* scene_data = assets.scenes().get_scene_data(
        assets.scenes().get_scene(scene_asset));
    ASSERT_NE(scene_data, nullptr);
    ASSERT_EQ(scene_data->nodes.size(), 1u);
    ASSERT_TRUE(scene_data->nodes[0].compute_kernel.has_value());
    EXPECT_TRUE(scene_data->nodes[0].compute_kernel->ports.empty());

    const std::string exported =
        wz::json::serialize_json(export_scene_to_json_document(*scene_data));
    EXPECT_NE(exported.find("\"compute_kernel\""), std::string::npos);
    EXPECT_EQ(exported.find("\"ports\""), std::string::npos);
}

TEST(SceneAssetModule, ComputeKernelRejectsWideRootConstantPorts)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_scene_compute_kernel_wide_root_constant_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const char* scene_json = R"({
  "schema": "wozzits.scene.v0",
  "name": "compute_kernel_wide_root_constant",
  "nodes": [
    {
      "id": "kernel",
      "compute_kernel": {
        "kernel_id": "project/kernel",
        "hlsl_path": "shaders/kernel.hlsl",
        "ports": [
          {
            "name": "too_wide",
            "kind": "u32",
            "direction": "input",
            "root_constant_offset": 0,
            "root_constant_dwords": 5
          }
        ]
      }
    }
  ]
})";

    auto rel_path = write_scene_json(
        root, "compute_kernel_wide_root_constant.scene.json", scene_json);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto scene_asset = assets.scenes().create_scene_from_json({
        .name = "compute_kernel_wide_root_constant",
        .path = rel_path,
    });
    ASSERT_TRUE(scene_asset.valid());
    ASSERT_TRUE(assets.commit());
    EXPECT_FALSE(assets.resolve_all().ok());
}
