#include "scene_authoring_materialize_test_support.h"

#include <window/window2.h>

namespace
{
    struct SceneComputeKernelMaterializeGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        wz::fs::Path root;

        void SetUp() override
        {
            root = wz::fs::join(
                wz::fs::temp_directory_path(),
                "wozzits_scene_materialize_compute_kernel_test");
            ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
            ASSERT_EQ(
                wz::fs::create_directories(
                    wz::fs::join(root, "shaders/compute")),
                wz::fs::FileError::None);

            const wz::fs::Path shader_path = wz::fs::join(
                root,
                "shaders/compute/debug_multiply_u32_cs.hlsl");
            ASSERT_EQ(
                wz::fs::write_file_text(
                    shader_path,
                    "StructuredBuffer<uint> Input : register(t0);\n"
                    "RWStructuredBuffer<uint> Output : register(u0);\n"
                    "cbuffer Constants : register(b0) {\n"
                    "    uint Factor;\n"
                    "    uint Count;\n"
                    "};\n"
                    "[numthreads(64, 1, 1)]\n"
                    "void main(uint3 id : SV_DispatchThreadID) {\n"
                    "    if (id.x < Count) {\n"
                    "        Output[id.x] = Input[id.x] * Factor;\n"
                    "    }\n"
                    "}\n"),
                wz::fs::FileError::None);

            wz::window::WindowDesc desc{};
            desc.title = "scene_compute_kernel_materialize_test";
            desc.width = 64;
            desc.height = 64;
            desc.resizable = false;

            window = wz::window::create_window(desc);
            ASSERT_TRUE(window.native);

            device = wz::gpu::create_device(window);
            ASSERT_TRUE(device.impl);
        }

        void TearDown() override
        {
            if (device.impl) {
                wz::gpu::destroy_device(device);
            }
            if (window.native) {
                wz::window::destroy_window(window);
            }
        }
    };
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    ComputeKernelMaterializesShaderAndPipelineAssets)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "compute_kernel_materialize";

    SceneNodeAsset node = make_scene_node("kernel");
    node.compute_kernel = SceneComputeKernelAsset{
        .kernel_id = "project/debug_multiply_u32",
        .hlsl_path = "shaders/compute/debug_multiply_u32_cs.hlsl",
        .entry = "main",
        .target = "cs_5_0",
        .thread_group_size_x = 64,
        .thread_group_size_y = 1,
        .thread_group_size_z = 1,
        .ports = {
            SceneComputeKernelPortAsset{
                .name = "input",
                .kind = SceneComputeKernelPortKind::StructuredBuffer,
                .direction = SceneComputeKernelPortDirection::Input,
                .binding_kind = SceneComputeKernelBindingKind::SRV,
                .shader_register = 0,
                .register_space = 0,
                .stride_bytes = 4,
            },
            SceneComputeKernelPortAsset{
                .name = "output",
                .kind = SceneComputeKernelPortKind::StructuredBuffer,
                .direction = SceneComputeKernelPortDirection::Output,
                .binding_kind = SceneComputeKernelBindingKind::UAV,
                .shader_register = 0,
                .register_space = 0,
                .stride_bytes = 4,
            },
            SceneComputeKernelPortAsset{
                .name = "factor",
                .kind = SceneComputeKernelPortKind::U32,
                .direction = SceneComputeKernelPortDirection::Input,
                .root_constant_offset = 0,
                .root_constant_dwords = 1,
            },
            SceneComputeKernelPortAsset{
                .name = "count",
                .kind = SceneComputeKernelPortKind::U32,
                .direction = SceneComputeKernelPortDirection::Input,
                .root_constant_offset = 1,
                .root_constant_dwords = 1,
            },
        },
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 1u);
    ASSERT_TRUE(scene.nodes[0].compute_kernel.has_value());

    const auto& kernel = *scene.nodes[0].compute_kernel;
    EXPECT_NE(kernel.compute_shader_asset, wz::asset::AssetKey{});
    EXPECT_NE(kernel.compute_pipeline_asset, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    const auto resolve_report = assets.resolve_all();
    if (!resolve_report.ok()) {
        ADD_FAILURE() << "resolve_all failed with "
                      << resolve_report.failures.size() << " failure(s)";
        for (const auto& failure : resolve_report.failures) {
            ADD_FAILURE() << "  error=" << static_cast<int>(failure.error);
        }
    }
    ASSERT_TRUE(resolve_report.ok());

    const ComputeShaderHandle shader =
        assets.shaders().get_compute_shader(
            ComputeShaderAsset{ .shader = kernel.compute_shader_asset });
    ASSERT_TRUE(shader.valid());

    const auto pipeline_handle =
        assets.compute_pipelines().get_compute_pipeline(
            ComputePipelineAsset{ .key = kernel.compute_pipeline_asset });
    ASSERT_TRUE(pipeline_handle.valid());

    const ComputePipelineData* pipeline_data =
        assets.compute_pipelines().get_compute_pipeline_data(
            pipeline_handle);
    ASSERT_NE(pipeline_data, nullptr);

    EXPECT_EQ(pipeline_data->name, "project/debug_multiply_u32");
    EXPECT_EQ(pipeline_data->compute_shader.id, shader.shader.id);
    EXPECT_EQ(pipeline_data->compute_shader.epoch, shader.shader.epoch);
    EXPECT_EQ(pipeline_data->compute_shader.type, shader.shader.type);
    EXPECT_EQ(pipeline_data->root_constant_dwords, 2u);
    EXPECT_EQ(pipeline_data->thread_group_size_x, 64u);
    EXPECT_EQ(pipeline_data->thread_group_size_y, 1u);
    EXPECT_EQ(pipeline_data->thread_group_size_z, 1u);

    ASSERT_EQ(pipeline_data->bindings.size(), 2u);
    EXPECT_EQ(
        pipeline_data->bindings[0].kind,
        ComputeBindingKind::StructuredBufferSRV);
    EXPECT_EQ(
        pipeline_data->bindings[0].semantic,
        ComputeBindingSemantic::Unknown);
    EXPECT_EQ(pipeline_data->bindings[0].shader_register, 0u);
    EXPECT_EQ(pipeline_data->bindings[0].register_space, 0u);
    EXPECT_EQ(pipeline_data->bindings[0].descriptor_count, 1u);
    EXPECT_EQ(pipeline_data->bindings[0].stride_bytes, 4u);

    EXPECT_EQ(
        pipeline_data->bindings[1].kind,
        ComputeBindingKind::StructuredBufferUAV);
    EXPECT_EQ(
        pipeline_data->bindings[1].semantic,
        ComputeBindingSemantic::Unknown);
    EXPECT_EQ(pipeline_data->bindings[1].shader_register, 0u);
    EXPECT_EQ(pipeline_data->bindings[1].register_space, 0u);
    EXPECT_EQ(pipeline_data->bindings[1].descriptor_count, 1u);
    EXPECT_EQ(pipeline_data->bindings[1].stride_bytes, 4u);
}

TEST_F(
    SceneComputeKernelMaterializeGpuFixture,
    ComputeKernelMaterializesDerivedShaderBindingsWithoutAuthoredPorts)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "compute_kernel_derived_materialize";

    SceneNodeAsset node = make_scene_node("kernel");
    node.compute_kernel = SceneComputeKernelAsset{
        .kernel_id = "project/debug_multiply_u32",
        .hlsl_path = "shaders/compute/debug_multiply_u32_cs.hlsl",
        .entry = "main",
        .target = "cs_5_0",
        .thread_group_size_x = 64,
        .thread_group_size_y = 1,
        .thread_group_size_z = 1,
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 1u);
    ASSERT_TRUE(scene.nodes[0].compute_kernel.has_value());

    const auto& kernel = *scene.nodes[0].compute_kernel;
    EXPECT_TRUE(kernel.ports.empty());
    EXPECT_NE(kernel.compute_shader_asset, wz::asset::AssetKey{});
    EXPECT_NE(kernel.compute_pipeline_asset, wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    const auto resolve_report = assets.resolve_all();
    ASSERT_TRUE(resolve_report.ok());

    const auto pipeline_handle =
        assets.compute_pipelines().get_compute_pipeline(
            ComputePipelineAsset{ .key = kernel.compute_pipeline_asset });
    ASSERT_TRUE(pipeline_handle.valid());

    const ComputePipelineData* pipeline_data =
        assets.compute_pipelines().get_compute_pipeline_data(
            pipeline_handle);
    ASSERT_NE(pipeline_data, nullptr);

    EXPECT_EQ(pipeline_data->root_constant_dwords, 2u);
    ASSERT_EQ(pipeline_data->bindings.size(), 2u);
    EXPECT_EQ(
        pipeline_data->bindings[0].kind,
        ComputeBindingKind::StructuredBufferSRV);
    EXPECT_EQ(pipeline_data->bindings[0].stride_bytes, 4u);
    EXPECT_EQ(
        pipeline_data->bindings[1].kind,
        ComputeBindingKind::StructuredBufferUAV);
    EXPECT_EQ(pipeline_data->bindings[1].stride_bytes, 4u);
}
