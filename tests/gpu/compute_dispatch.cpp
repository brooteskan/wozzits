#include <gtest/gtest.h>

#include <engine/assets/compute_pipeline_asset_module.h>
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
