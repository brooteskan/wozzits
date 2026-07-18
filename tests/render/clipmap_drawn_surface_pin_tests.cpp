// tests/render/clipmap_drawn_surface_pin_tests.cpp
//
// Pins the CPU mirror of the clipmap height tap to the GPU.
//
// clipmap_sample_height_world() reproduces sample_height_world() from
// resources/shaders/clipmap/clipmap_vs.hlsl. They are separate texts -- HLSL is
// compiled by D3DCompile with no include path, so there is no shared source to
// keep them together -- and the repo's existing answer to that has been a
// comment saying "changes here must be reflected there". That has already
// failed once here: the CPU ray reconstruction still describes the clipmap as
// point-sampling the full-res field, which stopped being true when the mip
// pyramid landed.
//
// So this test dispatches the SHADER'S OWN math in a compute shader over a grid
// of probe points, reads the results back, and requires the CPU function to
// reproduce them. It is the instrument the drawn-surface constraint work is
// measured with, and the thing that fails loudly when the two drift.
//
// Tolerance: exact equality is not achievable and is not the bar. D3D
// guarantees only 8 bits of subtexel precision on a bilinear weight, so a
// hardware tap and a full-precision CPU bilinear differ by roughly (adjacent
// texel delta)/256 regardless of how carefully the arithmetic is mirrored. The
// bound below is set by that, not by the mirroring.

#include <gtest/gtest.h>

#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/rendering/clipmap_drawn_surface.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_context.h>
#include <engine/rendering/rhi_dx12_command_recorder.h>
#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/rhi_gpu_backend.h>

#include <gpu/compute.h>
#include <gpu/gpu.h>
#include <window/window2.h>

#include <wozzits/rhi/compute_program.h>
#include <wozzits/rhi/shader_module.h>
#include <wozzits/rhi/shader_resource_group.h>

#define WIN32_LEAN_AND_MEAN
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    namespace er = wz::engine::rendering;

    constexpr uint32_t kFieldN = 64u;        // power of two: mip dims are exact
    constexpr uint32_t kProbesPerAxis = 24u;
    constexpr uint32_t kProbeCount = kProbesPerAxis * kProbesPerAxis;

    // The footprint the renderable and the collision both take from their
    // shared Placement. Deliberately not origin-zero and not square-unit, so a
    // dropped origin or a swapped axis shows up.
    constexpr float kWorldOriginX = -120.0f;
    constexpr float kWorldOriginZ = 35.0f;
    constexpr float kWorldSizeX = 480.0f;
    constexpr float kWorldSizeZ = 260.0f;

    struct ClipmapPinDeviceFixture : public ::testing::Test
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};

        void SetUp() override
        {
            wz::window::WindowDesc desc{};
            desc.title = "clipmap_drawn_surface_pin_test";
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

    // A field with real relief at every scale, so coarse mips are not just a
    // flat average and a wrong mip index cannot pass by looking similar.
    std::vector<float> make_probe_field()
    {
        std::vector<float> values(
            static_cast<size_t>(kFieldN) * kFieldN, 0.0f);
        for (uint32_t z = 0; z < kFieldN; ++z) {
            for (uint32_t x = 0; x < kFieldN; ++x) {
                const float fx = static_cast<float>(x);
                const float fz = static_cast<float>(z);
                values[static_cast<size_t>(z) * kFieldN + x] =
                    0.55f * std::sin(fx * 0.21f) * std::cos(fz * 0.17f)
                    + 0.30f * std::sin((fx + fz) * 0.63f)
                    + 0.15f * std::cos(fx * 1.31f);
            }
        }
        return values;
    }

    // The probe grid, spread across the footprint and pushed slightly PAST it
    // on both edges so the sampler's clamp addressing is exercised too.
    std::vector<std::pair<float, float>> make_probe_positions()
    {
        std::vector<std::pair<float, float>> probes;
        probes.reserve(kProbeCount);
        for (uint32_t j = 0; j < kProbesPerAxis; ++j) {
            for (uint32_t i = 0; i < kProbesPerAxis; ++i) {
                const float tx =
                    static_cast<float>(i) / (kProbesPerAxis - 1u);
                const float tz =
                    static_cast<float>(j) / (kProbesPerAxis - 1u);
                probes.emplace_back(
                    kWorldOriginX + (tx * 1.10f - 0.05f) * kWorldSizeX,
                    kWorldOriginZ + (tz * 1.10f - 0.05f) * kWorldSizeZ);
            }
        }
        return probes;
    }

    // sample_height_world, transplanted from clipmap_vs.hlsl. The body between
    // the markers is a VERBATIM copy of the shader's -- keep it that way, so a
    // change to the shader is a visible textual change here.
    std::vector<uint8_t> compile_probe_cs()
    {
        constexpr const char* kSource =
            "Texture2D<float> heightTex : register(t0, space0);\n"
            "SamplerState linearSampler : register(s0, space0);\n"
            "StructuredBuffer<float4> Probes : register(t1, space0);\n"
            "RWStructuredBuffer<float> Output : register(u0, space0);\n"
            "\n"
            "static const float4 world_to_uv = float4(WZ_W2UV);\n"
            "static const float2 texel_dims = float2(WZ_DIMS);\n"
            "\n"
            "// ---- begin verbatim clipmap_vs.hlsl sample_height_world ----\n"
            "float sample_height_world(float2 world_xz, float mip)\n"
            "{\n"
            "    float2 uv = world_to_uv.xy * world_xz + world_to_uv.zw;\n"
            "    float2 mip_dims   = max(floor(texel_dims / exp2(mip)), 1.0f);\n"
            "    float2 sample_uv  = uv + 0.5f / mip_dims;\n"
            "    return heightTex.SampleLevel(linearSampler, sample_uv, mip);\n"
            "}\n"
            "// ---- end verbatim ----\n"
            "\n"
            "[numthreads(64, 1, 1)]\n"
            "void main(uint3 id : SV_DispatchThreadID) {\n"
            "    uint n = 0, stride = 0;\n"
            "    Probes.GetDimensions(n, stride);\n"
            "    if (id.x >= n) { return; }\n"
            "    float4 p = Probes[id.x];\n"
            "    Output[id.x] = sample_height_world(p.xy, p.z);\n"
            "}\n";

        // world_to_uv = (1/size, -origin/size), the same packing
        // make_clipmap_draw_constants emits.
        std::string defines_w2uv =
            std::to_string(1.0f / kWorldSizeX) + ","
            + std::to_string(1.0f / kWorldSizeZ) + ","
            + std::to_string(-kWorldOriginX / kWorldSizeX) + ","
            + std::to_string(-kWorldOriginZ / kWorldSizeZ);
        std::string defines_dims =
            std::to_string(kFieldN) + "," + std::to_string(kFieldN);

        const D3D_SHADER_MACRO macros[] = {
            { "WZ_W2UV", defines_w2uv.c_str() },
            { "WZ_DIMS", defines_dims.c_str() },
            { nullptr, nullptr },
        };

        Microsoft::WRL::ComPtr<ID3DBlob> shader_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> error_blob;
        const HRESULT hr = D3DCompile(
            kSource,
            std::strlen(kSource),
            nullptr,
            macros,
            nullptr,
            "main",
            "cs_5_1",
            0,
            0,
            shader_blob.GetAddressOf(),
            error_blob.GetAddressOf());
        if (FAILED(hr) || !shader_blob || shader_blob->GetBufferSize() == 0u) {
            if (error_blob) {
                ADD_FAILURE() << "probe CS compile failed: "
                    << static_cast<const char*>(error_blob->GetBufferPointer());
            }
            return {};
        }

        std::vector<uint8_t> out(shader_blob->GetBufferSize());
        std::memcpy(
            out.data(),
            shader_blob->GetBufferPointer(),
            shader_blob->GetBufferSize());
        return out;
    }
}

TEST_F(ClipmapPinDeviceFixture, CpuMirrorReproducesTheShaderHeightTap)
{
    using namespace wz::engine::assets::internal;

    const std::vector<uint8_t> bytecode = compile_probe_cs();
    ASSERT_FALSE(bytecode.empty());

    // The pyramid the resident height texture is built from -- the same builder
    // the collision keeps its CPU copy from.
    const std::vector<float> mip0 = make_probe_field();
    const std::vector<ScalarFieldMipLevel> pyramid =
        build_scalar_field_mip_pyramid(mip0, kFieldN, kFieldN);
    ASSERT_GE(pyramid.size(), 4u);

    const std::vector<std::pair<float, float>> probes = make_probe_positions();
    ASSERT_EQ(probes.size(), kProbeCount);

    er::EngineGpuContext gpu(device);
    er::RhiDx12PipelineCache pipeline_cache(
        device, gpu.programs, gpu.compute_programs, gpu.shaders);
    er::RhiDx12CommandRecorder recorder(
        device, pipeline_cache, gpu.resources, gpu.backend);

    const std::string shader_name = "test/clipmap_probe_cs";
    const wz::rhi::Tag shader_tag = gpu.shaders.register_program(
        wz::rhi::ShaderModuleDesc{
            shader_name, wz::rhi::ShaderStage::Compute, bytecode });
    ASSERT_TRUE(shader_tag.valid());

    const wz::rhi::Tag height_semantic =
        gpu.descriptor_semantics.acquire("scalar_field_texture");
    const wz::rhi::Tag probe_semantic =
        gpu.descriptor_semantics.acquire("probes");
    const wz::rhi::Tag output_semantic =
        gpu.descriptor_semantics.acquire("output");
    ASSERT_TRUE(height_semantic.valid());
    ASSERT_TRUE(probe_semantic.valid());
    ASSERT_TRUE(output_semantic.valid());

    wz::rhi::ShaderResourceGroupLayout layout;
    layout.binding_slot = 0;
    layout.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::TextureSRV,
        wz::rhi::ShaderStage::Compute,
        height_semantic, 0, 0, 1 });
    layout.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::StructuredBufferSRV,
        wz::rhi::ShaderStage::Compute,
        probe_semantic, 1, 0, 1 });
    layout.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::UAV,
        wz::rhi::ShaderStage::Compute,
        output_semantic, 0, 0, 1 });
    // The SAME static sampler the clipmap program bakes in -- a different
    // filter or address mode here would invalidate the whole comparison.
    layout.static_samplers.push_back(wz::rhi::StaticSamplerBinding{
        wz::rhi::StaticSamplerKind::LinearClamp,
        wz::rhi::ShaderStage::Compute, 0, 0 });

    wz::rhi::ComputeProgramDesc program_desc;
    program_desc.name = "test/clipmap_probe";
    program_desc.compute_shader = shader_name;
    program_desc.thread_group_size[0] = 64;
    program_desc.shader_resource_groups.push_back(layout);

    const wz::rhi::Tag program =
        gpu.compute_programs.register_program(program_desc);
    ASSERT_TRUE(program.valid());
    ASSERT_NE(pipeline_cache.realize(program), nullptr);

    // Height texture, full mip chain, uploaded per level exactly as the scalar
    // field residency path does.
    wz::rhi::GpuResourceDesc tex_desc = wz::rhi::GpuResourceDesc::texture_2d(
        kFieldN, kFieldN,
        wz::rhi::TextureFormat::R32Float,
        wz::rhi::ResourceUsage_Sampled);
    tex_desc.mip_levels = static_cast<uint32_t>(pyramid.size());
    tex_desc.cpu_access = wz::rhi::ResourceCpuAccess::WriteOnce;
    const wz::rhi::GpuResourceHandle height_tex = gpu.resources.acquire(tex_desc);
    ASSERT_TRUE(height_tex.valid());
    for (uint32_t mip = 0; mip < pyramid.size(); ++mip) {
        ASSERT_TRUE(gpu.resources.update_mip(
            height_tex,
            mip,
            pyramid[mip].values.data(),
            pyramid[mip].values.size() * sizeof(float)))
            << "mip " << mip;
    }

    // Probe buffer: xy = world XZ, z = mip.
    const uint32_t mip_count = static_cast<uint32_t>(pyramid.size());
    std::vector<float> probe_data;
    probe_data.reserve(static_cast<size_t>(kProbeCount) * 4u);
    std::vector<uint32_t> probe_mips;
    probe_mips.reserve(kProbeCount);
    for (size_t i = 0; i < probes.size(); ++i) {
        // Cycle the mips so every level in the chain is exercised.
        const uint32_t mip = static_cast<uint32_t>(i % mip_count);
        probe_mips.push_back(mip);
        probe_data.push_back(probes[i].first);
        probe_data.push_back(probes[i].second);
        probe_data.push_back(static_cast<float>(mip));
        probe_data.push_back(0.0f);
    }

    const wz::rhi::GpuResourceHandle probe_buffer =
        gpu.resources.acquire(wz::rhi::GpuResourceDesc::buffer(
            probe_data.size() * sizeof(float),
            sizeof(float) * 4u,
            wz::rhi::ResourceUsage_Sampled,
            wz::rhi::ResourceCpuAccess::WriteOnce));
    ASSERT_TRUE(probe_buffer.valid());
    ASSERT_TRUE(gpu.resources.update(
        probe_buffer, probe_data.data(), probe_data.size() * sizeof(float)));

    const wz::rhi::GpuResourceHandle output =
        gpu.resources.acquire(wz::rhi::GpuResourceDesc::buffer(
            kProbeCount * sizeof(float),
            sizeof(float),
            wz::rhi::ResourceUsage_Storage | wz::rhi::ResourceUsage_CopySrc));
    ASSERT_TRUE(output.valid());

    wz::rhi::ShaderResourceGroup srg(layout);
    ASSERT_TRUE(srg.set(height_semantic, height_tex).has_value());
    ASSERT_TRUE(srg.set(probe_semantic, probe_buffer).has_value());
    ASSERT_TRUE(srg.set(output_semantic, output).has_value());
    ASSERT_TRUE(srg.satisfies(layout));

    ASSERT_TRUE(wz::gpu::begin_frame(device));
    recorder.barrier(
        output,
        wz::rhi::ResourceState::Undefined,
        wz::rhi::ResourceState::UnorderedAccess);
    recorder.set_pipeline(program);
    recorder.bind_resource_group(0, srg);
    wz::rhi::DispatchArgs dispatch;
    dispatch.group_count[0] = (kProbeCount + 63u) / 64u;
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
    ASSERT_EQ(bytes.size(), kProbeCount * sizeof(float));

    std::vector<float> gpu_heights(kProbeCount, 0.0f);
    std::memcpy(gpu_heights.data(), bytes.data(), bytes.size());

    // The CPU side, over the same pyramid and the same footprint.
    std::vector<er::ClipmapHeightMipView> levels;
    levels.reserve(pyramid.size());
    for (const auto& level : pyramid) {
        levels.push_back(er::ClipmapHeightMipView{
            level.values.data(), level.width, level.height });
    }
    er::ClipmapHeightFieldView field{};
    field.levels = levels;
    field.world_origin[0] = kWorldOriginX;
    field.world_origin[1] = kWorldOriginZ;
    field.world_size[0] = kWorldSizeX;
    field.world_size[1] = kWorldSizeZ;
    ASSERT_TRUE(field.valid());

    // Tolerance comes from the SAMPLER, not from the arithmetic. D3D guarantees
    // only 8 bits of subtexel precision on a bilinear weight, so a hardware tap
    // and a full-precision CPU bilinear differ however carefully the math is
    // mirrored. A bilinear quantizes TWO weights, and the sensitivity to each
    // is bounded by the largest adjacent-texel step along that axis, so the
    // bound is (max_step_x + max_step_z)/256 -- both axes, not one.
    float max_step = 0.0f;
    for (const auto& level : pyramid) {
        for (uint32_t z = 0; z < level.height; ++z) {
            for (uint32_t x = 0; x < level.width; ++x) {
                const size_t i = static_cast<size_t>(z) * level.width + x;
                if (x + 1u < level.width) {
                    max_step = (std::max)(
                        max_step,
                        std::abs(level.values[i + 1] - level.values[i]));
                }
                if (z + 1u < level.height) {
                    max_step = (std::max)(
                        max_step,
                        std::abs(
                            level.values[i + level.width] - level.values[i]));
                }
            }
        }
    }
    const float tolerance = 2.0f * max_step / 256.0f + 1e-5f;

    // Collect everything before asserting: a per-mip breakdown says whether a
    // failure is uniform noise (the sampler) or concentrated at coarse levels
    // (a real mirror bug, e.g. the wrong mip or the wrong half-texel shift).
    std::vector<double> worst_by_mip(mip_count, 0.0);
    double worst = 0.0;
    for (uint32_t i = 0; i < kProbeCount; ++i) {
        const float cpu = er::clipmap_sample_height_world(
            field, probes[i].first, probes[i].second, probe_mips[i]);
        const double err =
            static_cast<double>(std::abs(cpu - gpu_heights[i]));
        worst = (std::max)(worst, err);
        worst_by_mip[probe_mips[i]] =
            (std::max)(worst_by_mip[probe_mips[i]], err);
    }

    for (uint32_t mip = 0; mip < mip_count; ++mip) {
        std::cout << "[ clipmap pin ] mip " << mip
            << " worst |cpu - gpu| = " << worst_by_mip[mip] << "\n";
    }

    EXPECT_LE(worst, static_cast<double>(tolerance))
        << "the CPU mirror no longer reproduces the shader's height tap; "
           "see the per-mip breakdown above -- error concentrated at coarse "
           "mips points at the mip index or the half-texel shift, error "
           "spread evenly points at the filter or the uv mapping";

    // Guard the guard: a field this varied must produce a spread of heights,
    // so an all-zero readback (a silently unbound texture) cannot pass.
    const auto [lo, hi] =
        std::minmax_element(gpu_heights.begin(), gpu_heights.end());
    EXPECT_GT(*hi - *lo, 0.25f)
        << "probe results are nearly constant -- is the texture bound?";

    std::cout << "[ clipmap pin ] worst |cpu - gpu| = " << worst
        << " (tolerance " << tolerance << ")\n";

    gpu.resources.release(output);
    gpu.resources.release(probe_buffer);
    gpu.resources.release(height_tex);
    gpu.resources.collect(UINT64_MAX);
}
