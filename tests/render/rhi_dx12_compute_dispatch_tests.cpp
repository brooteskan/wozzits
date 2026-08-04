#include <gtest/gtest.h>

#include <engine/rendering/rhi_context.h>
#include <engine/rendering/rhi_dx12_command_recorder.h>
#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_gpu_backend.h>

#include <gpu/compute.h>
#include <gpu/shader.h>
#include <gpu/gpu.h>
#include <window/window2.h>

#include <wozzits/rhi/compute_program.h>
#include <wozzits/rhi/shader_module.h>
#include <wozzits/rhi/shader_resource_group.h>

#define WIN32_LEAN_AND_MEAN
#include <cstring>
#include <string>

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct RhiComputeDeviceFixture : public ::testing::Test
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};

        void SetUp() override
        {
            wz::window::WindowDesc desc{};
            desc.title = "rhi_dx12_compute_dispatch_test";
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

    std::vector<uint8_t> compile_write_indices_cs()
    {
        constexpr const char* kSource =
            "RWStructuredBuffer<uint> Output : register(u0);\n"
            "[numthreads(1, 1, 1)]\n"
            "void main(uint3 id : SV_DispatchThreadID) {\n"
            "    Output[id.x] = id.x;\n"
            "}\n";

        Microsoft::WRL::ComPtr<ID3DBlob> shader_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> error_blob;
        const HRESULT hr = D3DCompile(
            kSource,
            std::strlen(kSource),
            nullptr,
            nullptr,
            nullptr,
            "main",
            "cs_5_0",
            0,
            0,
            shader_blob.GetAddressOf(),
            error_blob.GetAddressOf());
        if (FAILED(hr) || !shader_blob || shader_blob->GetBufferSize() == 0u) {
            return {};
        }

        std::vector<uint8_t> out(shader_blob->GetBufferSize());
        std::memcpy(
            out.data(),
            shader_blob->GetBufferPointer(),
            shader_blob->GetBufferSize());
        return out;
    }

    std::array<uint32_t, 8> read_u32x8(const std::vector<std::byte>& bytes)
    {
        std::array<uint32_t, 8> out{};
        std::memcpy(out.data(), bytes.data(), sizeof(out));
        return out;
    }
}

TEST_F(RhiComputeDeviceFixture, DispatchWritesUavBuffer)
{
    constexpr uint32_t kElementCount = 8;

    const std::vector<uint8_t> bytecode = compile_write_indices_cs();
    ASSERT_FALSE(bytecode.empty());

    wz::engine::rendering::EngineGpuContext gpu(device);
    wz::engine::rendering::RhiDx12PipelineCache pipeline_cache(
        device,
        gpu.programs,
        gpu.compute_programs,
        gpu.shaders);
    wz::engine::rendering::RhiDx12CommandRecorder recorder(
        device,
        pipeline_cache,
        gpu.resources,
        gpu.backend);

    const std::string shader_name = "test/write_indices_cs";
    const wz::rhi::Tag shader_tag = gpu.shaders.register_program(
        wz::rhi::ShaderModuleDesc{
            shader_name,
            wz::rhi::ShaderStage::Compute,
            bytecode });
    ASSERT_TRUE(shader_tag.valid());

    const wz::rhi::Tag output_semantic =
        gpu.descriptor_semantics.acquire("output");
    ASSERT_TRUE(output_semantic.valid());

    wz::rhi::ShaderResourceGroupLayout output_layout;
    output_layout.binding_slot = 0;
    output_layout.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::UAV,
        wz::rhi::ShaderStage::Compute,
        output_semantic,
        /*shader_register*/ 0,
        /*register_space*/ 0,
        /*descriptor_count*/ 1 });

    wz::rhi::ComputeProgramDesc program_desc;
    program_desc.name = "test/write_indices";
    program_desc.compute_shader = shader_name;
    program_desc.thread_group_size[0] = 1;
    program_desc.shader_resource_groups.push_back(output_layout);

    const wz::rhi::Tag program =
        gpu.compute_programs.register_program(program_desc);
    ASSERT_TRUE(program.valid());
    ASSERT_NE(pipeline_cache.realize(program), nullptr);

    // #317 D1-C22: a compute PSO is stored under a render-target sentinel key
    // (false, 0), but realize()'s lookup reads the live bound colour format -- so
    // before the fix a re-realize (in production, every set_pipeline of the same
    // compute program) MISSED the cache and appended a duplicate root signature +
    // PSO: an unbounded leak on a per-frame dispatch. Re-realize and assert the
    // cache stays flat. Revert-check: drop the sentinel lookup in realize()'s
    // compute branch and entry_count() grows by one here.
#ifdef WZ_ENABLE_TESTING
    const std::size_t entries_after_first = pipeline_cache.entry_count();
    ASSERT_EQ(entries_after_first, 1u);
    ASSERT_NE(pipeline_cache.realize(program), nullptr);
    EXPECT_EQ(pipeline_cache.entry_count(), entries_after_first)
        << "compute realize appended a duplicate cache entry (D1-C22)";
#endif

    const wz::rhi::GpuResourceHandle output =
        gpu.resources.acquire(wz::rhi::GpuResourceDesc::buffer(
            kElementCount * sizeof(uint32_t),
            sizeof(uint32_t),
            wz::rhi::ResourceUsage_Storage | wz::rhi::ResourceUsage_CopySrc));
    ASSERT_TRUE(output.valid());

    wz::rhi::ShaderResourceGroup output_srg(output_layout);
    ASSERT_TRUE(output_srg.set(output_semantic, output).has_value());
    ASSERT_TRUE(output_srg.satisfies(output_layout));

    ASSERT_TRUE(wz::gpu::begin_frame(device));
    recorder.barrier(
        output,
        wz::rhi::ResourceState::Undefined,
        wz::rhi::ResourceState::UnorderedAccess);
    recorder.set_pipeline(program);
    recorder.bind_resource_group(0, output_srg);
    wz::rhi::DispatchArgs dispatch;
    dispatch.group_count[0] = kElementCount;
    recorder.dispatch(dispatch);
    ASSERT_TRUE(recorder.ready());
    ASSERT_TRUE(wz::gpu::end_frame(device));
    wz::gpu::wait_idle(device);

    const wz::rhi::GpuResource* output_resource = gpu.resources.get(output);
    ASSERT_NE(output_resource, nullptr);
    const wz::gpu::GPUHandle gpu_output =
        gpu.backend.gpu_handle_for(output_resource->backend);
    ASSERT_TRUE(gpu_output.valid());
    const std::vector<std::byte> bytes =
        wz::gpu::readback_buffer(device, gpu_output);
    ASSERT_EQ(bytes.size(), kElementCount * sizeof(uint32_t));

    EXPECT_EQ(
        read_u32x8(bytes),
        (std::array<uint32_t, 8>{ 0, 1, 2, 3, 4, 5, 6, 7 }));

    gpu.resources.release(output);
    gpu.resources.collect(UINT64_MAX);
}

// ── The shader-warning channel (#316, C3-C2) ─────────────────────────────
//
// compile_hlsl read FXC's diagnostic blob on FAILURE and released it unread on
// SUCCESS, so every warning the corpus emits was discarded at the one place
// that could see it. `out_warnings` is that channel; these two tests are what
// stop it from being a no-op nobody notices.
//
// The positive case matters more than it looks: the shipping corpus is
// currently warning-FREE at the sites the render tests exercise, so a test that
// only ran real shaders would pass identically with the channel removed. This
// provokes a diagnostic on purpose.

namespace
{
    // Returning a value that was never fully written. FXC: X3578, "Output
    // value 'main' is not completely initialized". Chosen because it is a
    // WARNING -- the shader still compiles, which is the whole case under
    // test. (Asserting the exact code rather than just "non-empty" earned
    // its keep immediately: it caught that I had guessed X4000.)
    constexpr const char* kWarnsButCompiles =
        "float4 main() : SV_TARGET\n"
        "{\n"
        "    float4 c;\n"
        "    c.x = 1.0f;\n"
        "    return c;\n"
        "}\n";

    constexpr const char* kClean =
        "float4 main() : SV_TARGET\n"
        "{\n"
        "    return float4(1.0f, 0.0f, 0.0f, 1.0f);\n"
        "}\n";

    std::span<const uint8_t> as_bytes(const char* text)
    {
        return { reinterpret_cast<const uint8_t*>(text), std::strlen(text) };
    }
}

TEST_F(RhiComputeDeviceFixture, ShaderWarningsReachTheCallerOnASuccessfulCompile)
{
    wz::gpu::HLSLCompileDesc desc{};
    desc.stage  = wz::gpu::ShaderStage::Pixel;
    desc.entry  = "main";
    desc.target = "ps_5_1";

    const std::span<const uint8_t> source = as_bytes(kWarnsButCompiles);
    std::string error;
    std::string warnings;
    const wz::gpu::GPUHandle handle = wz::gpu::compile_hlsl(
        device, { &source, 1 }, desc, &error, &warnings);

    // It COMPILED -- this is not the failure path.
    EXPECT_TRUE(handle.valid()) << error;
    EXPECT_TRUE(error.empty());

    EXPECT_FALSE(warnings.empty())
        << "a shader that compiles with a diagnostic reported nothing; the "
           "success path is dropping FXC's blob again";
    EXPECT_NE(warnings.find("X3578"), std::string::npos)
        << "got a diagnostic, but not the expected one: " << warnings;
}

TEST_F(RhiComputeDeviceFixture, ACleanShaderReportsNoWarnings)
{
    wz::gpu::HLSLCompileDesc desc{};
    desc.stage  = wz::gpu::ShaderStage::Pixel;
    desc.entry  = "main";
    desc.target = "ps_5_1";

    const std::span<const uint8_t> source = as_bytes(kClean);
    std::string error;
    std::string warnings;
    const wz::gpu::GPUHandle handle = wz::gpu::compile_hlsl(
        device, { &source, 1 }, desc, &error, &warnings);

    EXPECT_TRUE(handle.valid()) << error;
    // Without this, the test above passes for a channel that reports
    // unconditionally.
    EXPECT_TRUE(warnings.empty()) << "unexpected diagnostic: " << warnings;
}
