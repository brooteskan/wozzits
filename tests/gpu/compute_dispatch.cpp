#include <gtest/gtest.h>

#include <engine/assets/compute_pipeline_asset_module.h>
#include <engine/assets/scene/scene_authoring_materialize.h>
#include <engine/behavior/behavior_gpu_compute_executor.h>
#include <engine/behavior/behavior_module_api.h>
#include <engine/assets/engine_asset_library.h>
#include <gpu/compute.h>
#include <gpu/gpu.h>
#include <logging/logger.h>
#include <window/window2.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    namespace fs = std::filesystem;

    struct TempComputeDir
    {
        fs::path root;

        TempComputeDir()
        {
            root = fs::temp_directory_path() /
                ("wozzits_compute_dispatch_tests_" +
                 std::to_string(::GetCurrentProcessId()));

            fs::remove_all(root);
            fs::create_directories(root / "shaders" / "compute");
        }

        ~TempComputeDir()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        wz::fs::Path wz_root() const { return root.string(); }

        void write_multiply_shader() const
        {
            std::ofstream out(
                root / "shaders" / "compute" / "multiply_uint_cs.hlsl",
                std::ios::binary);
            out
                << "StructuredBuffer<uint> Input : register(t0);\n"
                << "RWStructuredBuffer<uint> Output : register(u0);\n"
                << "cbuffer Params : register(b0) {\n"
                << "    uint Factor;\n"
                << "    uint Count;\n"
                << "    uint2 Pad;\n"
                << "};\n"
                << "[numthreads(4, 1, 1)]\n"
                << "void main(uint3 id : SV_DispatchThreadID) {\n"
                << "    if (id.x < Count) {\n"
                << "        Output[id.x] = Input[id.x] * Factor;\n"
                << "    }\n"
                << "}\n";
        }

        void write_scene_kernel_json() const
        {
            std::ofstream out(
                root / "scene_compute_kernel.scene.json",
                std::ios::binary);
            out
                << R"({
  "schema": "wozzits.scene.v0",
  "name": "scene_compute_kernel_dispatch",
  "nodes": [
    {
      "id": "debug_multiply_kernel",
      "compute_kernel": {
        "kernel_id": "project/debug_multiply_u32",
        "hlsl_path": "shaders/compute/multiply_uint_cs.hlsl",
        "entry": "main",
        "target": "cs_5_0",
        "thread_group_size": [4, 1, 1]
      }
    }
  ]
})";
        }
    };

    struct ComputeDispatchFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        TempComputeDir resources;

        void SetUp() override
        {
            resources.write_multiply_shader();
            resources.write_scene_kernel_json();

            wz::window::WindowDesc desc{};
            desc.title = "compute_dispatch_test";
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
            if (device.impl)
                wz::gpu::destroy_device(device);
            if (window.native)
                wz::window::destroy_window(window);
        }
    };

    std::array<uint32_t, 4> read_u32x4(const std::vector<std::byte>& bytes)
    {
        std::array<uint32_t, 4> out{};
        std::memcpy(out.data(), bytes.data(), sizeof(out));
        return out;
    }

}

TEST_F(ComputeDispatchFixture, MultiplyKernelDispatchesAndReadsBack)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shader = assets.shaders().create_compute_shader({
        .name = "compute/multiply_uint",
        .path = "shaders/compute/multiply_uint_cs.hlsl",
        .entry = "main",
        .target = "cs_5_0",
    });
    ASSERT_TRUE(shader.valid());

    const auto pipeline_asset =
        assets.compute_pipelines().create_compute_pipeline({
            .name = "compute/multiply_uint_pipeline",
            .compute_shader = shader.shader,
            .bindings = {
                {
                    .kind = ComputeBindingKind::StructuredBufferSRV,
                    .semantic = ComputeBindingSemantic::Scratch,
                    .shader_register = 0,
                    .register_space = 0,
                    .descriptor_count = 1,
                    .stride_bytes = sizeof(uint32_t),
                },
                {
                    .kind = ComputeBindingKind::StructuredBufferUAV,
                    .semantic = ComputeBindingSemantic::Scratch,
                    .shader_register = 0,
                    .register_space = 0,
                    .descriptor_count = 1,
                    .stride_bytes = sizeof(uint32_t),
                },
            },
            .root_constant_dwords = 4,
            .thread_group_size_x = 4,
            .thread_group_size_y = 1,
            .thread_group_size_z = 1,
        });
    ASSERT_TRUE(pipeline_asset.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto shader_handle = assets.shaders().get_compute_shader(shader);
    ASSERT_TRUE(shader_handle.valid());

    const auto pipeline_handle =
        assets.compute_pipelines().get_compute_pipeline(pipeline_asset);
    ASSERT_TRUE(pipeline_handle.valid());

    const auto* pipeline_data =
        assets.compute_pipelines().get_compute_pipeline_data(pipeline_handle);
    ASSERT_NE(pipeline_data, nullptr);

    const wz::gpu::GPUHandle gpu_pipeline =
        wz::gpu::create_compute_pipeline(
            device,
            *pipeline_data,
            shader_handle.shader);
    ASSERT_TRUE(gpu_pipeline.valid());

    const std::array<uint32_t, 4> input{ 1, 2, 3, 4 };
    const std::array<uint32_t, 4> zeroes{ 0, 0, 0, 0 };

    const wz::gpu::GPUHandle input_buffer =
        wz::gpu::create_structured_buffer(device, {
            .element_count = static_cast<uint32_t>(input.size()),
            .stride_bytes = sizeof(uint32_t),
            .initial_data = input.data(),
            .initial_data_bytes = sizeof(input),
        });
    ASSERT_TRUE(input_buffer.valid());

    const wz::gpu::GPUHandle output_buffer =
        wz::gpu::create_rw_structured_buffer(device, {
            .element_count = static_cast<uint32_t>(zeroes.size()),
            .stride_bytes = sizeof(uint32_t),
            .initial_data = zeroes.data(),
            .initial_data_bytes = sizeof(zeroes),
        });
    ASSERT_TRUE(output_buffer.valid());

    const std::array<uint32_t, 4> root_constants{ 2, 4, 0, 0 };
    const std::array<wz::gpu::ComputeDispatchBinding, 2> bindings{{
        {
            .kind = ComputeBindingKind::StructuredBufferSRV,
            .shader_register = 0,
            .register_space = 0,
            .buffer = input_buffer,
        },
        {
            .kind = ComputeBindingKind::StructuredBufferUAV,
            .shader_register = 0,
            .register_space = 0,
            .buffer = output_buffer,
        },
    }};

    ASSERT_TRUE(wz::gpu::dispatch_compute(device, {
        .pipeline = gpu_pipeline,
        .bindings = bindings,
        .root_constants = root_constants,
        .group_count_x = 1,
        .group_count_y = 1,
        .group_count_z = 1,
    }));

    const std::vector<std::byte> output =
        wz::gpu::readback_buffer(device, output_buffer);
    ASSERT_EQ(output.size(), sizeof(std::array<uint32_t, 4>));

    EXPECT_EQ(read_u32x4(output), (std::array<uint32_t, 4>{ 2, 4, 6, 8 }));

    EXPECT_TRUE(wz::gpu::release_compute_buffer(device, input_buffer));
    EXPECT_TRUE(wz::gpu::release_compute_buffer(device, output_buffer));
    EXPECT_TRUE(wz::gpu::release_compute_pipeline(device, gpu_pipeline));
}

TEST_F(ComputeDispatchFixture, BehaviorGpuNamedPortsDispatchAdHocResource)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shader = assets.shaders().create_compute_shader({
        .name = "compute/multiply_uint",
        .path = "shaders/compute/multiply_uint_cs.hlsl",
        .entry = "main",
        .target = "cs_5_0",
    });
    ASSERT_TRUE(shader.valid());

    const auto pipeline_asset =
        assets.compute_pipelines().create_compute_pipeline({
            .name = "compute/multiply_uint_pipeline",
            .compute_shader = shader.shader,
            .bindings = {
                {
                    .kind = ComputeBindingKind::StructuredBufferSRV,
                    .semantic = ComputeBindingSemantic::Scratch,
                    .shader_register = 0,
                    .register_space = 0,
                    .descriptor_count = 1,
                    .stride_bytes = sizeof(uint32_t),
                },
                {
                    .kind = ComputeBindingKind::StructuredBufferUAV,
                    .semantic = ComputeBindingSemantic::Scratch,
                    .shader_register = 0,
                    .register_space = 0,
                    .descriptor_count = 1,
                    .stride_bytes = sizeof(uint32_t),
                },
            },
            .root_constant_dwords = 4,
            .thread_group_size_x = 4,
            .thread_group_size_y = 1,
            .thread_group_size_z = 1,
        });
    ASSERT_TRUE(pipeline_asset.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto shader_handle = assets.shaders().get_compute_shader(shader);
    ASSERT_TRUE(shader_handle.valid());

    const auto pipeline_handle =
        assets.compute_pipelines().get_compute_pipeline(pipeline_asset);
    ASSERT_TRUE(pipeline_handle.valid());

    const auto* pipeline_data =
        assets.compute_pipelines().get_compute_pipeline_data(pipeline_handle);
    ASSERT_NE(pipeline_data, nullptr);

    const wz::gpu::GPUHandle gpu_pipeline =
        wz::gpu::create_compute_pipeline(
            device,
            *pipeline_data,
            shader_handle.shader);
    ASSERT_TRUE(gpu_pipeline.valid());

    const std::array<uint32_t, 4> input{ 3, 4, 5, 6 };
    WzGpuJob job{};
    WzGpuWorkId work{};
    ASSERT_EQ(wz_gpu_begin(&job, "compute/multiply_uint"), 1u);
    ASSERT_EQ(wz_gpu_set_request_tag(&job, 777u), 1u);
    ASSERT_EQ(wz_gpu_set_groups(&job, 1u, 1u, 1u), 1u);
    ASSERT_EQ(
        wz_gpu_set_structured_input(
            &job,
            "input",
            static_cast<uint32_t>(input.size()),
            sizeof(uint32_t),
            input.data(),
            sizeof(input)),
        1u);
    ASSERT_EQ(
        wz_gpu_set_structured_output(
            &job,
            "output",
            static_cast<uint32_t>(input.size()),
            sizeof(uint32_t)),
        1u);
    ASSERT_EQ(wz_gpu_set_u32(&job, "factor", 5u), 1u);
    ASSERT_EQ(wz_gpu_set_u32(&job, "count", 4u), 1u);

    wz::engine::behavior::BehaviorGpuComputeBuffer queue{};
    ASSERT_TRUE(queue.submit(12u, job.desc, &work));
    EXPECT_EQ(work.value, 1u);

    const std::vector<wz::engine::behavior::BehaviorGpuKernelBinding>
        kernels{
            {
                .name = "compute/multiply_uint",
                .pipeline = gpu_pipeline,
                .root_constant_dwords = 4u,
                .ports = {
                    {
                        .name = "input",
                        .port_kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                        .direction = WZ_GPU_PORT_INPUT,
                        .target = wz::engine::behavior::BehaviorGpuKernelPortTarget::BufferBinding,
                        .binding_kind = ComputeBindingKind::StructuredBufferSRV,
                        .shader_register = 0u,
                    },
                    {
                        .name = "output",
                        .port_kind = WZ_GPU_PORT_STRUCTURED_BUFFER,
                        .direction = WZ_GPU_PORT_OUTPUT,
                        .target = wz::engine::behavior::BehaviorGpuKernelPortTarget::BufferBinding,
                        .binding_kind = ComputeBindingKind::StructuredBufferUAV,
                        .shader_register = 0u,
                    },
                    {
                        .name = "factor",
                        .port_kind = WZ_GPU_PORT_U32,
                        .direction = WZ_GPU_PORT_INPUT,
                        .target = wz::engine::behavior::BehaviorGpuKernelPortTarget::RootConstant,
                        .root_constant_offset = 0u,
                        .root_constant_dwords = 1u,
                    },
                    {
                        .name = "count",
                        .port_kind = WZ_GPU_PORT_U32,
                        .direction = WZ_GPU_PORT_INPUT,
                        .target = wz::engine::behavior::BehaviorGpuKernelPortTarget::RootConstant,
                        .root_constant_offset = 1u,
                        .root_constant_dwords = 1u,
                    },
                },
            },
        };

    const auto dispatch_report =
        wz::engine::behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            queue.jobs,
            kernels);
    EXPECT_EQ(dispatch_report.submitted, 1u);
    EXPECT_EQ(dispatch_report.dispatched, 1u);
    EXPECT_EQ(dispatch_report.failed, 0u);
    ASSERT_EQ(dispatch_report.readbacks.size(), 1u);
    EXPECT_EQ(dispatch_report.readbacks[0].work.value, work.value);
    EXPECT_EQ(dispatch_report.readbacks[0].port_name, "output");
    ASSERT_EQ(
        dispatch_report.readbacks[0].bytes.size(),
        sizeof(std::array<uint32_t, 4>));
    EXPECT_EQ(
        read_u32x4(dispatch_report.readbacks[0].bytes),
        (std::array<uint32_t, 4>{ 15, 20, 25, 30 }));
    ASSERT_EQ(dispatch_report.completed_work.size(), 1u);
    EXPECT_EQ(dispatch_report.completed_work[0].value, work.value);
    EXPECT_TRUE(dispatch_report.failed_work.empty());

    const uint32_t posted =
        wz::engine::behavior::post_behavior_gpu_compute_events(
            queue,
            queue.jobs,
            dispatch_report);
    EXPECT_EQ(posted, 1u);
    ASSERT_EQ(queue.events.size(), 1u);
    EXPECT_EQ(queue.events[0].kind, WZ_EVENT_GPU_COMPUTE_COMPLETED);
    EXPECT_EQ(queue.events[0].entity, 12u);
    EXPECT_EQ(queue.events[0].payload.work.value, work.value);
    EXPECT_EQ(queue.events[0].payload.status, WZ_GPU_COMPUTE_STATUS_COMPLETED);
    EXPECT_EQ(queue.events[0].payload.request_tag, 777u);
    EXPECT_EQ(queue.events[0].payload.output_count, 1u);

    EXPECT_TRUE(wz::gpu::release_compute_pipeline(device, gpu_pipeline));
}

TEST_F(ComputeDispatchFixture, SceneAuthoredKernelBuildsLibraryAndPostsEvent)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "scene_compute_kernel_dispatch",
            .path = "scene_compute_kernel.scene.json",
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const auto* parsed_scene =
        assets.scenes().get_scene_data(
            assets.scenes().get_scene(scene_asset));
    ASSERT_NE(parsed_scene, nullptr);

    SceneAssetData scene = *parsed_scene;
    const auto materialize_report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(materialize_report.ok) << materialize_report.error;
    ASSERT_TRUE(assets.commit());
    const auto resolve_report = assets.resolve_all();
    ASSERT_TRUE(resolve_report.ok());

    wz::engine::behavior::BehaviorGpuKernelLibrary library{};
    std::string library_error;
    ASSERT_TRUE(
        wz::engine::behavior::build_kernel_library_from_scene(
            device,
            scene,
            assets,
            library,
            &library_error))
        << library_error;
    ASSERT_EQ(library.kernels.size(), 1u);
    ASSERT_NE(library.find("project/debug_multiply_u32"), nullptr);

    const std::array<uint32_t, 4> input{ 7, 8, 9, 10 };
    WzGpuJob job{};
    WzGpuWorkId work{};
    ASSERT_EQ(wz_gpu_begin(&job, "project/debug_multiply_u32"), 1u);
    ASSERT_EQ(wz_gpu_set_request_tag(&job, 4242u), 1u);
    ASSERT_EQ(wz_gpu_set_groups(&job, 1u, 1u, 1u), 1u);
    ASSERT_EQ(
        wz_gpu_set_structured_input(
            &job,
            "input",
            static_cast<uint32_t>(input.size()),
            sizeof(uint32_t),
            input.data(),
            sizeof(input)),
        1u);
    ASSERT_EQ(
        wz_gpu_set_structured_output(
            &job,
            "output",
            static_cast<uint32_t>(input.size()),
            sizeof(uint32_t)),
        1u);
    ASSERT_EQ(wz_gpu_set_u32(&job, "factor", 3u), 1u);
    ASSERT_EQ(wz_gpu_set_u32(&job, "count", 4u), 1u);

    wz::engine::behavior::BehaviorGpuComputeBuffer queue{};
    ASSERT_TRUE(queue.submit(24u, job.desc, &work));

    const auto dispatch_report =
        wz::engine::behavior::dispatch_behavior_gpu_compute_jobs(
            device,
            queue.jobs,
            library);
    EXPECT_EQ(dispatch_report.submitted, 1u);
    EXPECT_EQ(dispatch_report.dispatched, 1u);
    EXPECT_EQ(dispatch_report.failed, 0u);

    const uint32_t posted =
        wz::engine::behavior::post_behavior_gpu_compute_events(
            queue,
            queue.jobs,
            dispatch_report);
    EXPECT_EQ(posted, 1u);
    ASSERT_EQ(queue.events.size(), 1u);
    EXPECT_EQ(queue.events[0].kind, WZ_EVENT_GPU_COMPUTE_COMPLETED);
    EXPECT_EQ(queue.events[0].entity, 24u);
    EXPECT_EQ(queue.events[0].payload.work.value, work.value);
    EXPECT_EQ(queue.events[0].payload.request_tag, 4242u);
    EXPECT_EQ(queue.events[0].payload.output_count, 1u);
    ASSERT_EQ(queue.events[0].outputs.size(), 1u);
    ASSERT_EQ(
        queue.events[0].outputs[0].initial_data.size(),
        sizeof(std::array<uint32_t, 4>));
    EXPECT_EQ(
        read_u32x4(queue.events[0].outputs[0].initial_data),
        (std::array<uint32_t, 4>{ 21, 24, 27, 30 }));

    EXPECT_EQ(
        wz::engine::behavior::release_behavior_gpu_kernel_library(
            device,
            library),
        1u);
}
