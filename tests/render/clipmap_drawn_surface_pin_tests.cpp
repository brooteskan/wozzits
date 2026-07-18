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

    // ── Lattice used by the vertex-height pin ────────────────────────────────
    // Small m keeps every ring's half-extent inside the footprint; the observer
    // is deliberately off both the lattice grid and the texel grid so a dropped
    // snap shows up.
    constexpr uint32_t kLatticeM = 8u;
    constexpr uint32_t kLatticeLevels = 5u;
    constexpr float kLatticeC0 = kWorldSizeX / static_cast<float>(kFieldN);
    constexpr float kObserverX = kWorldOriginX + 137.3f;
    constexpr float kObserverZ = kWorldOriginZ + 84.9f;

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

    // The VS's per-vertex height: per-level snap, mip-L tap, geomorph toward
    // the coarser ring's triangulated surface. Both marked bodies are VERBATIM
    // from clipmap_vs.hlsl.
    //
    // Emits TWO values per probe: the height with this ring's own snap, and the
    // height with the interior position trim applied. The CPU mirror implements
    // the first; the second exists so the test can report what the trim -- the
    // one term deliberately left out of the mirror -- is actually worth.
    std::vector<uint8_t> compile_vertex_probe_cs()
    {
        constexpr const char* kSource =
            "Texture2D<float> heightTex : register(t0, space0);\n"
            "SamplerState linearSampler : register(s0, space0);\n"
            "StructuredBuffer<float4> Probes : register(t1, space0);\n"
            "RWStructuredBuffer<float> Output : register(u0, space0);\n"
            "\n"
            "static const float4 world_to_uv = float4(WZ_W2UV);\n"
            "static const float2 texel_dims  = float2(WZ_DIMS);\n"
            "static const float2 camera_xz   = float2(WZ_CAMERA);\n"
            "static const float  c0          = WZ_C0;\n"
            "static const float  lattice_m   = WZ_M;\n"
            "static const float  max_mip     = WZ_MAXMIP;\n"
            // snap_params.w in the real cbuffer: the ring count, which is what
            // lets the VS recognise its outermost ring.
            "static const float  level_count = WZ_LEVELS;\n"
            "#define CLIPMAP_MORPH_START 0.80f\n"
            "#define CLIPMAP_MORPH_END   0.98f\n"
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
            "// ---- begin verbatim clipmap_vs.hlsl sample_height_coarse ----\n"
            "float sample_height_coarse(float2 world_xz, float two_cL, float coarse_mip)\n"
            "{\n"
            "    float2 cg = world_xz / two_cL;\n"
            "    float2 cf = floor(cg);\n"
            "    float2 fr = cg - cf;\n"
            "    float h00 = sample_height_world((cf + float2(0.0f, 0.0f)) * two_cL, coarse_mip);\n"
            "    float h10 = sample_height_world((cf + float2(1.0f, 0.0f)) * two_cL, coarse_mip);\n"
            "    float h01 = sample_height_world((cf + float2(0.0f, 1.0f)) * two_cL, coarse_mip);\n"
            "    float h11 = sample_height_world((cf + float2(1.0f, 1.0f)) * two_cL, coarse_mip);\n"
            "    return lerp(lerp(h00, h10, fr.x), lerp(h01, h11, fr.x), fr.y);\n"
            "}\n"
            "// ---- end verbatim ----\n"
            "\n"
            "[numthreads(64, 1, 1)]\n"
            "void main(uint3 id : SV_DispatchThreadID) {\n"
            "    uint n = 0, stride = 0;\n"
            "    Probes.GetDimensions(n, stride);\n"
            "    if (id.x >= n) { return; }\n"
            "    float3 g = Probes[id.x].xyz;   // g.y carries the LOD level\n"
            "\n"
            "    // ---- begin verbatim clipmap_vs.hlsl height block ----\n"
            "    float  level  = max(g.y, 0.0f);\n"
            "    float  cL     = exp2(level) * c0;\n"
            "    float  twoCL  = 2.0f * cL;\n"
            "    float  fourCL = 2.0f * twoCL;\n"
            "    float2 T_fine = floor(camera_xz / twoCL) * twoCL;\n"
            "    float  m          = max(lattice_m, 1.0f);\n"
            "    float  half_world = 0.5f * m * cL;\n"
            "    float  dist       = max(abs(g.x), abs(g.z));\n"
            "    float morph_start = CLIPMAP_MORPH_START * half_world;\n"
            "    float morph_end   = CLIPMAP_MORPH_END * half_world;\n"
            "    float a = saturate(\n"
            "        (dist - morph_start) / max(morph_end - morph_start, 1e-6f));\n"
            "    bool is_outermost = (level + 1.5f) >= level_count;\n"
            "    if (is_outermost) {\n"
            "        a = 0.0f;\n"
            "    }\n"
            "    float  a_trim   = is_outermost\n"
            "        ? 0.0f\n"
            "        : saturate((dist - (half_world - twoCL)) / twoCL);\n"
            "    float2 T_coarse = floor(camera_xz / fourCL) * fourCL;\n"
            "    float2 T        = lerp(T_fine, T_coarse, a_trim);\n"
            "    float mip_fine   = min(level, max_mip);\n"
            "    float mip_coarse = min(level + 1.0f, max_mip);\n"
            "    // ---- end verbatim ----\n"
            "\n"
            "    float2 untrimmed = T_fine + g.xz;\n"
            "    float2 trimmed   = T + g.xz;\n"
            "    Output[id.x * 2 + 0] = lerp(\n"
            "        sample_height_world(untrimmed, mip_fine),\n"
            "        sample_height_coarse(untrimmed, twoCL, mip_coarse), a);\n"
            "    Output[id.x * 2 + 1] = lerp(\n"
            "        sample_height_world(trimmed, mip_fine),\n"
            "        sample_height_coarse(trimmed, twoCL, mip_coarse), a);\n"
            "}\n";

        std::string w2uv =
            std::to_string(1.0f / kWorldSizeX) + ","
            + std::to_string(1.0f / kWorldSizeZ) + ","
            + std::to_string(-kWorldOriginX / kWorldSizeX) + ","
            + std::to_string(-kWorldOriginZ / kWorldSizeZ);
        std::string dims =
            std::to_string(kFieldN) + "," + std::to_string(kFieldN);
        std::string camera =
            std::to_string(kObserverX) + "," + std::to_string(kObserverZ);
        std::string c0s = std::to_string(kLatticeC0) + "f";
        std::string ms = std::to_string(kLatticeM) + ".0f";
        // floor(log2(64)) + 1 == 7 levels, so the coarsest mip index is 6.
        std::string maxmip = "6.0f";
        std::string levels_s = std::to_string(kLatticeLevels) + ".0f";

        const D3D_SHADER_MACRO macros[] = {
            { "WZ_W2UV", w2uv.c_str() },
            { "WZ_DIMS", dims.c_str() },
            { "WZ_CAMERA", camera.c_str() },
            { "WZ_C0", c0s.c_str() },
            { "WZ_M", ms.c_str() },
            { "WZ_MAXMIP", maxmip.c_str() },
            { "WZ_LEVELS", levels_s.c_str() },
            { nullptr, nullptr },
        };

        Microsoft::WRL::ComPtr<ID3DBlob> shader_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> error_blob;
        const HRESULT hr = D3DCompile(
            kSource, std::strlen(kSource), nullptr, macros, nullptr,
            "main", "cs_5_1", 0, 0,
            shader_blob.GetAddressOf(), error_blob.GetAddressOf());
        if (FAILED(hr) || !shader_blob || shader_blob->GetBufferSize() == 0u) {
            if (error_blob) {
                ADD_FAILURE() << "vertex probe CS compile failed: "
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

    // Upload the height pyramid + a probe buffer, dispatch, read the floats
    // back. Returns empty on any failure (the caller asserts on that), so this
    // stays usable from a non-void helper.
    std::vector<float> run_probe_dispatch(
        wz::gpu::Device& device,
        const std::vector<uint8_t>& bytecode,
        const std::vector<wz::engine::assets::internal::ScalarFieldMipLevel>&
            pyramid,
        const std::vector<float>& probe_data,
        uint32_t outputs_per_probe)
    {
        const uint32_t probe_count =
            static_cast<uint32_t>(probe_data.size() / 4u);
        const uint32_t output_count = probe_count * outputs_per_probe;

        er::EngineGpuContext gpu(device);
        er::RhiDx12PipelineCache pipeline_cache(
            device, gpu.programs, gpu.compute_programs, gpu.shaders);
        er::RhiDx12CommandRecorder recorder(
            device, pipeline_cache, gpu.resources, gpu.backend);

        static int unique = 0;
        const std::string shader_name =
            "test/clipmap_probe_cs_" + std::to_string(unique++);
        const wz::rhi::Tag shader_tag = gpu.shaders.register_program(
            wz::rhi::ShaderModuleDesc{
                shader_name, wz::rhi::ShaderStage::Compute, bytecode });
        EXPECT_TRUE(shader_tag.valid());

        const wz::rhi::Tag height_semantic =
            gpu.descriptor_semantics.acquire("scalar_field_texture");
        const wz::rhi::Tag probe_semantic =
            gpu.descriptor_semantics.acquire("probes");
        const wz::rhi::Tag output_semantic =
            gpu.descriptor_semantics.acquire("output");

        wz::rhi::ShaderResourceGroupLayout layout;
        layout.binding_slot = 0;
        layout.descriptors.push_back(wz::rhi::DescriptorBinding{
            wz::rhi::DescriptorKind::TextureSRV,
            wz::rhi::ShaderStage::Compute, height_semantic, 0, 0, 1 });
        layout.descriptors.push_back(wz::rhi::DescriptorBinding{
            wz::rhi::DescriptorKind::StructuredBufferSRV,
            wz::rhi::ShaderStage::Compute, probe_semantic, 1, 0, 1 });
        layout.descriptors.push_back(wz::rhi::DescriptorBinding{
            wz::rhi::DescriptorKind::UAV,
            wz::rhi::ShaderStage::Compute, output_semantic, 0, 0, 1 });
        // The SAME static sampler the clipmap program bakes in -- a different
        // filter or address mode would invalidate the whole comparison.
        layout.static_samplers.push_back(wz::rhi::StaticSamplerBinding{
            wz::rhi::StaticSamplerKind::LinearClamp,
            wz::rhi::ShaderStage::Compute, 0, 0 });

        wz::rhi::ComputeProgramDesc program_desc;
        program_desc.name = shader_name;
        program_desc.compute_shader = shader_name;
        program_desc.thread_group_size[0] = 64;
        program_desc.shader_resource_groups.push_back(layout);

        const wz::rhi::Tag program =
            gpu.compute_programs.register_program(program_desc);
        EXPECT_TRUE(program.valid());
        if (!program.valid() || pipeline_cache.realize(program) == nullptr) {
            return {};
        }

        wz::rhi::GpuResourceDesc tex_desc =
            wz::rhi::GpuResourceDesc::texture_2d(
                kFieldN, kFieldN,
                wz::rhi::TextureFormat::R32Float,
                wz::rhi::ResourceUsage_Sampled);
        tex_desc.mip_levels = static_cast<uint32_t>(pyramid.size());
        tex_desc.cpu_access = wz::rhi::ResourceCpuAccess::WriteOnce;
        const wz::rhi::GpuResourceHandle height_tex =
            gpu.resources.acquire(tex_desc);
        EXPECT_TRUE(height_tex.valid());
        for (uint32_t mip = 0; mip < pyramid.size(); ++mip) {
            EXPECT_TRUE(gpu.resources.update_mip(
                height_tex, mip,
                pyramid[mip].values.data(),
                pyramid[mip].values.size() * sizeof(float))) << "mip " << mip;
        }

        const wz::rhi::GpuResourceHandle probe_buffer =
            gpu.resources.acquire(wz::rhi::GpuResourceDesc::buffer(
                probe_data.size() * sizeof(float),
                sizeof(float) * 4u,
                wz::rhi::ResourceUsage_Sampled,
                wz::rhi::ResourceCpuAccess::WriteOnce));
        EXPECT_TRUE(probe_buffer.valid());
        EXPECT_TRUE(gpu.resources.update(
            probe_buffer,
            probe_data.data(),
            probe_data.size() * sizeof(float)));

        const wz::rhi::GpuResourceHandle output =
            gpu.resources.acquire(wz::rhi::GpuResourceDesc::buffer(
                output_count * sizeof(float),
                sizeof(float),
                wz::rhi::ResourceUsage_Storage
                    | wz::rhi::ResourceUsage_CopySrc));
        EXPECT_TRUE(output.valid());

        wz::rhi::ShaderResourceGroup srg(layout);
        EXPECT_TRUE(srg.set(height_semantic, height_tex).has_value());
        EXPECT_TRUE(srg.set(probe_semantic, probe_buffer).has_value());
        EXPECT_TRUE(srg.set(output_semantic, output).has_value());
        if (!srg.satisfies(layout)) {
            return {};
        }

        if (!wz::gpu::begin_frame(device)) {
            return {};
        }
        recorder.barrier(
            output,
            wz::rhi::ResourceState::Undefined,
            wz::rhi::ResourceState::UnorderedAccess);
        recorder.set_pipeline(program);
        recorder.bind_resource_group(0, srg);
        wz::rhi::DispatchArgs dispatch;
        dispatch.group_count[0] = (probe_count + 63u) / 64u;
        recorder.dispatch(dispatch);
        EXPECT_TRUE(recorder.ready());
        EXPECT_TRUE(wz::gpu::end_frame(device));
        wz::gpu::wait_idle(device);

        const wz::rhi::GpuResource* output_resource = gpu.resources.get(output);
        if (!output_resource) {
            return {};
        }
        const wz::gpu::GPUHandle gpu_output =
            gpu.backend.gpu_handle_for(output_resource->backend);
        EXPECT_TRUE(gpu_output.valid());
        const std::vector<std::byte> bytes =
            wz::gpu::readback_buffer(device, gpu_output);
        EXPECT_EQ(bytes.size(), output_count * sizeof(float));

        std::vector<float> out(output_count, 0.0f);
        if (bytes.size() == output_count * sizeof(float)) {
            std::memcpy(out.data(), bytes.data(), bytes.size());
        }

        gpu.resources.release(output);
        gpu.resources.release(probe_buffer);
        gpu.resources.release(height_tex);
        gpu.resources.collect(UINT64_MAX);
        return out;
    }

    // Tolerance from the SAMPLER, not from the arithmetic. D3D guarantees only
    // 8 bits of subtexel precision on a bilinear weight, so a hardware tap and
    // a full-precision CPU bilinear differ however carefully the math is
    // mirrored. A bilinear quantizes TWO weights and the sensitivity to each is
    // bounded by the largest adjacent-texel step along that axis, so the bound
    // is (max_step_x + max_step_z)/256 -- both axes, not one.
    float sampler_tolerance(
        const std::vector<wz::engine::assets::internal::ScalarFieldMipLevel>&
            pyramid,
        float taps_per_result)
    {
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
                                level.values[i + level.width]
                                    - level.values[i]));
                    }
                }
            }
        }
        return taps_per_result * 2.0f * max_step / 256.0f + 1e-5f;
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
    const uint32_t mip_count = static_cast<uint32_t>(pyramid.size());

    const std::vector<std::pair<float, float>> probes = make_probe_positions();
    ASSERT_EQ(probes.size(), kProbeCount);

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

    const std::vector<float> gpu_heights =
        run_probe_dispatch(device, bytecode, pyramid, probe_data, 1u);
    ASSERT_EQ(gpu_heights.size(), kProbeCount);

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

    // A single tap: the bound is one sampler quantisation.
    const float tolerance = sampler_tolerance(pyramid, 1.0f);

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
        std::cout << "[ clipmap pin ] tap mip " << mip
            << " worst |cpu - gpu| = " << worst_by_mip[mip] << "\n";
    }
    std::cout << "[ clipmap pin ] tap worst = " << worst
        << " (tolerance " << tolerance << ")\n";

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
}

// The layer above the tap: per-level snap, mip-L read, and the geomorph blend
// toward the coarser ring's TRIANGULATED surface. This is where the shader
// stops being one texture read and starts being clipmap-specific, so it is the
// part most likely to drift and the part worth pinning hardest.
//
// Also reports what the interior position trim is worth. The CPU mirror leaves
// the trim out deliberately -- its endpoints coincide with the untrimmed grid
// and it only bends a one-cell band at each ring boundary -- but that was an
// argument, not a measurement. The probe shader emits the trimmed height too,
// so the number is on the record and can be revisited if it is ever large.
TEST_F(ClipmapPinDeviceFixture, CpuMirrorReproducesTheShaderVertexHeight)
{
    using namespace wz::engine::assets::internal;

    const std::vector<uint8_t> bytecode = compile_vertex_probe_cs();
    ASSERT_FALSE(bytecode.empty());

    const std::vector<float> mip0 = make_probe_field();
    const std::vector<ScalarFieldMipLevel> pyramid =
        build_scalar_field_mip_pyramid(mip0, kFieldN, kFieldN);
    ASSERT_GE(pyramid.size(), 7u);

    // Per ring, a grid of lattice offsets reaching 5% PAST the ring's own
    // half-extent, so every probe set covers the rigid interior (morph a == 0),
    // the whole [0.80, 0.98] morph band, and the fully-morphed edge.
    constexpr uint32_t kPerAxis = 12u;
    std::vector<float> probe_data;
    std::vector<uint32_t> probe_levels;
    std::vector<std::pair<float, float>> probe_g;
    for (uint32_t level = 0; level < kLatticeLevels; ++level) {
        const float c_l = std::exp2(static_cast<float>(level)) * kLatticeC0;
        const float half_world =
            0.5f * static_cast<float>(kLatticeM) * c_l;
        for (uint32_t j = 0; j < kPerAxis; ++j) {
            for (uint32_t i = 0; i < kPerAxis; ++i) {
                const float tx =
                    static_cast<float>(i) / (kPerAxis - 1u) * 2.0f - 1.0f;
                const float tz =
                    static_cast<float>(j) / (kPerAxis - 1u) * 2.0f - 1.0f;
                const float gx = tx * 1.05f * half_world;
                const float gz = tz * 1.05f * half_world;
                probe_levels.push_back(level);
                probe_g.emplace_back(gx, gz);
                // The shader reads Probes[i].xyz as g, with g.y the LOD level.
                probe_data.push_back(gx);
                probe_data.push_back(static_cast<float>(level));
                probe_data.push_back(gz);
                probe_data.push_back(0.0f);
            }
        }
    }
    const uint32_t probe_count = static_cast<uint32_t>(probe_levels.size());

    const std::vector<float> gpu_out =
        run_probe_dispatch(device, bytecode, pyramid, probe_data, 2u);
    ASSERT_EQ(gpu_out.size(), probe_count * 2u);

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

    er::ClipmapDrawnSurfaceParams params{};
    params.observer_xz[0] = kObserverX;
    params.observer_xz[1] = kObserverZ;
    params.c0 = kLatticeC0;
    params.base_resolution = kLatticeM;
    params.level_count = kLatticeLevels;
    ASSERT_TRUE(params.valid());

    // The result is a convex combination of taps -- the coarse target is a
    // bilinear blend of four, then lerped against the fine one, all with
    // weights summing to one -- so the bound stays a single quantisation
    // rather than accumulating per tap.
    const float tolerance = sampler_tolerance(pyramid, 1.0f);

    std::vector<double> worst_by_level(kLatticeLevels, 0.0);
    double worst = 0.0;
    double worst_trim = 0.0;
    bool saw_interior = false;
    bool saw_band = false;
    bool saw_full = false;
    for (uint32_t i = 0; i < probe_count; ++i) {
        const uint32_t level = probe_levels[i];
        const float cpu = er::clipmap_drawn_vertex_height(
            field, params, level, probe_g[i].first, probe_g[i].second);
        const double err = static_cast<double>(
            std::abs(cpu - gpu_out[i * 2u + 1u]));
        worst = (std::max)(worst, err);
        worst_by_level[level] = (std::max)(worst_by_level[level], err);
        worst_trim = (std::max)(
            worst_trim,
            static_cast<double>(
                std::abs(gpu_out[i * 2u + 1u] - gpu_out[i * 2u])));

        // Confirm the probe set really spans the morph, so a mirror that
        // ignored the blend entirely could not pass by never being tested in
        // the band.
        const float c_l = std::exp2(static_cast<float>(level)) * kLatticeC0;
        const float half_world =
            0.5f * static_cast<float>(kLatticeM) * c_l;
        const float dist = (std::max)(
            std::abs(probe_g[i].first), std::abs(probe_g[i].second));
        const float a = std::clamp(
            (dist - er::kClipmapMorphStart * half_world)
                / ((er::kClipmapMorphEnd - er::kClipmapMorphStart)
                    * half_world),
            0.0f, 1.0f);
        saw_interior = saw_interior || a <= 0.0f;
        saw_band = saw_band || (a > 0.05f && a < 0.95f);
        saw_full = saw_full || a >= 1.0f;
    }

    for (uint32_t level = 0; level < kLatticeLevels; ++level) {
        std::cout << "[ clipmap pin ] vertex ring " << level
            << " worst |cpu - gpu| = " << worst_by_level[level] << "\n";
    }
    std::cout << "[ clipmap pin ] vertex worst = " << worst
        << " (tolerance " << tolerance << ")\n";
    // Diagnostic, and easy to misread, so state it precisely: this is how far
    // a VERTEX's height moves when the trim relocates it. clipmap_drawn_vertex_
    // height reproduces that (it is what the assertion above compares against).
    // clipmap_drawn_surface_height deliberately does not -- it needs its cell
    // topology and its vertex positions to agree, and reconstructing the warped
    // grid is not worth it. The number below OVERSTATES what that costs, since
    // a relocated vertex is being compared at two different world positions,
    // while a surface query asks about one fixed XZ.
    std::cout << "[ clipmap pin ] interior position trim moves a vertex's "
        << "height by up to " << worst_trim << "\n";

    EXPECT_TRUE(saw_interior);
    EXPECT_TRUE(saw_band) << "probes never landed inside the morph band";
    EXPECT_TRUE(saw_full);

    EXPECT_LE(worst, static_cast<double>(tolerance))
        << "the CPU mirror no longer reproduces the shader's vertex height; "
           "error confined to one ring points at that ring's mip or snap, "
           "error only at ring edges points at the geomorph band";
}

// ── CPU-only: what the lattice MESH means ────────────────────────────────────
// Ring membership and cell topology come from the mesh on the GPU, not from
// shader code, so there is nothing to pin a readback against. These state the
// invariants the mesh gives instead.

namespace
{
    // A smooth field, so a continuity check measures the reconstruction rather
    // than the terrain's own roughness.
    std::vector<float> make_smooth_field()
    {
        std::vector<float> values(
            static_cast<size_t>(kFieldN) * kFieldN, 0.0f);
        for (uint32_t z = 0; z < kFieldN; ++z) {
            for (uint32_t x = 0; x < kFieldN; ++x) {
                values[static_cast<size_t>(z) * kFieldN + x] =
                    std::sin(static_cast<float>(x) * 0.09f)
                    + std::cos(static_cast<float>(z) * 0.07f);
            }
        }
        return values;
    }

    struct CpuField
    {
        std::vector<wz::engine::assets::internal::ScalarFieldMipLevel> pyramid;
        std::vector<er::ClipmapHeightMipView> views;
        er::ClipmapHeightFieldView field{};

        explicit CpuField(std::vector<float> mip0)
        {
            pyramid =
                wz::engine::assets::internal::build_scalar_field_mip_pyramid(
                    mip0, kFieldN, kFieldN);
            for (const auto& level : pyramid) {
                views.push_back(er::ClipmapHeightMipView{
                    level.values.data(), level.width, level.height });
            }
            field.levels = views;
            field.world_origin[0] = kWorldOriginX;
            field.world_origin[1] = kWorldOriginZ;
            field.world_size[0] = kWorldSizeX;
            field.world_size[1] = kWorldSizeZ;
        }
    };

    er::ClipmapDrawnSurfaceParams pin_params()
    {
        er::ClipmapDrawnSurfaceParams p{};
        p.observer_xz[0] = kObserverX;
        p.observer_xz[1] = kObserverZ;
        p.c0 = kLatticeC0;
        p.base_resolution = kLatticeM;
        p.level_count = kLatticeLevels;
        return p;
    }
}

// Near the observer the drawn surface must BE the fine field: ring 0 reads mip
// 0 at cell spacing c0, so the reconstruction and the true surface agree there.
// This is the property that makes the whole approach viable -- an actor near
// the camera, where the player is looking closely, stands on the real ground.
TEST(ClipmapDrawnSurface, ConvergesToTheFineFieldAtTheObserver)
{
    const CpuField cpu(make_smooth_field());
    const er::ClipmapDrawnSurfaceParams params = pin_params();
    ASSERT_TRUE(cpu.field.valid());
    ASSERT_TRUE(params.valid());

    // Ring 0 is centred on its own snap, up to 2*c0 from the observer, and its
    // rigid interior ends at 0.80 * half_world. Probe inside what remains, or
    // the test is really asserting something about ring 1.
    const float half_world_0 =
        0.5f * static_cast<float>(kLatticeM) * kLatticeC0;
    const float rigid = er::kClipmapMorphStart * half_world_0 - 2.0f * kLatticeC0;
    ASSERT_GT(rigid, kLatticeC0);

    double worst = 0.0;
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            const float x = kObserverX + static_cast<float>(i) * rigid * 0.9f;
            const float z = kObserverZ + static_cast<float>(j) * rigid * 0.9f;
            const float drawn =
                er::clipmap_drawn_surface_height(cpu.field, params, x, z);
            const float truth =
                er::clipmap_sample_height_world(cpu.field, x, z, 0u);
            worst = (std::max)(
                worst, static_cast<double>(std::abs(drawn - truth)));
        }
    }
    EXPECT_LT(worst, 0.02)
        << "ring 0 should reproduce the fine field under the observer";
}

// Detail must COARSEN outward: out in the coarsest ring the reconstruction is
// reading a heavily box-filtered mip at wide cell spacing, so it should differ
// from the fine field far more than it does under the observer. If it did not,
// the reconstruction would be answering with detail the renderer never draws
// out there -- which is the whole failure this work exists to fix, in mirror
// image.
TEST(ClipmapDrawnSurface, CoarsensWithDistanceFromTheObserver)
{
    const CpuField cpu(make_smooth_field());
    const er::ClipmapDrawnSurfaceParams params = pin_params();

    const auto deviation = [&](float x, float z) {
        return std::abs(
            er::clipmap_drawn_surface_height(cpu.field, params, x, z)
            - er::clipmap_sample_height_world(cpu.field, x, z, 0u));
    };

    const float half_world_0 =
        0.5f * static_cast<float>(kLatticeM) * kLatticeC0;
    const float rigid =
        er::kClipmapMorphStart * half_world_0 - 2.0f * kLatticeC0;

    float near_dev = 0.0f;
    for (int k = -1; k <= 1; ++k) {
        near_dev = (std::max)(
            near_dev,
            deviation(
                kObserverX + static_cast<float>(k) * rigid * 0.9f,
                kObserverZ));
    }

    // Well inside the coarsest ring, past every finer ring's reach.
    const float coarsest_c =
        std::exp2(static_cast<float>(kLatticeLevels - 1u)) * kLatticeC0;
    const float far_reach =
        0.5f * static_cast<float>(kLatticeM) * coarsest_c * 0.6f;
    float far_dev = 0.0f;
    for (int k = 0; k < 16; ++k) {
        const float t = static_cast<float>(k) / 15.0f;
        far_dev = (std::max)(
            far_dev,
            deviation(
                kObserverX + far_reach * (0.55f + 0.4f * t),
                kObserverZ + far_reach * (0.5f + 0.45f * t)));
    }

    EXPECT_GT(far_dev, 4.0f * near_dev)
        << "near " << near_dev << " far " << far_dev
        << " -- the far field should be visibly decimated relative to the "
           "ground under the observer";
}

// The geomorph exists so a finer ring's outer edge lands on the coarser ring's
// triangulated surface. If that holds, the reconstruction has no step at a ring
// boundary -- which is exactly the crack-free property the renderer relies on,
// and a step here would put an actor through a visible seam.
TEST(ClipmapDrawnSurface, HasNoStepAcrossARingBoundary)
{
    const CpuField cpu(make_smooth_field());
    const er::ClipmapDrawnSurfaceParams params = pin_params();

    for (uint32_t level = 0; level + 1u < kLatticeLevels; ++level) {
        const float c_l = std::exp2(static_cast<float>(level)) * kLatticeC0;
        const float two_cl = 2.0f * c_l;
        const float half_world =
            0.5f * static_cast<float>(kLatticeM) * c_l;
        const float t_x =
            std::floor(params.observer_xz[0] / two_cl) * two_cl;
        const float boundary = t_x + half_world;

        const float eps = c_l * 1e-3f;
        for (int k = 0; k < 16; ++k) {
            const float z = kObserverZ + static_cast<float>(k) * c_l * 0.37f;
            const float inside = er::clipmap_drawn_surface_height(
                cpu.field, params, boundary - eps, z);
            const float outside = er::clipmap_drawn_surface_height(
                cpu.field, params, boundary + eps, z);
            EXPECT_NEAR(inside, outside, 0.02f)
                << "step at the level " << level << "/" << (level + 1)
                << " boundary, z = " << z;
        }
    }
}
