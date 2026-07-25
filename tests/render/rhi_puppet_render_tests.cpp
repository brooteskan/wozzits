// tests/render/rhi_puppet_render_tests.cpp
//
// On-device integration coverage for the Inochi2D puppet render path (inochi
// S2b). It builds, end to end:
//   * a Puppet asset from the real Aka.inp fixture (loads + publishes residency:
//     atlas Texture2Ds + per-Part interleaved-vertex/index StructuredBuffers),
//   * a puppet render program (MeshVertexPull; a Screen view head + the
//     PuppetVertices/PuppetIndices/PuppetAtlas object SRG + a clamp sampler),
//   * a kPuppetRhiRenderableSchema renderable binding them,
// then drives one device frame through RhiSceneRenderer and asserts the puppet
// realizes (one DrawPacket per Part) and records WITHOUT the recorder rejecting
// the draws. This structurally proves the wiring; it does NOT verify the puppet
// looks right -- there is no view of the viewport. Skipped when no Aka.inp
// fixture or no GPU device is available.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/puppet_asset_module.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_scene_renderer.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <gpu/texture.h>
#include <gpu/dx12/dx12_internal.h>
#include <math/mat4.h>
#include <math/math_types.h>
#include <window/window2.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;
    namespace fs = std::filesystem;

    // The puppet shaders, written into a self-contained temp resource root so the
    // test never depends on the staged resources tree. Byte-compatible with
    // resources/shaders/puppet/puppet_vs.hlsl + puppet_ps.hlsl: a Screen view head
    // at (t0, space0); interleaved WzPuppetVertex at (t0, space2), indices t1,
    // atlas t2, sampler s0; the PuppetPartBlock (2D affine + opacity) at (b0,
    // space2). The space2 bindings require Shader Model 5.1.
    constexpr const char* kPuppetVs = R"(
struct WzScreenConstants
{
    float4 viewport;
};
StructuredBuffer<WzScreenConstants> screen_constants : register(t0, space0);

cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;
    float4 xform_row1;
};

struct WzPuppetVertex
{
    float2 pos;
    float2 uv;
};
StructuredBuffer<WzPuppetVertex> vertices : register(t0, space2);
StructuredBuffer<uint>           indices  : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    uint           idx = indices[vid];
    WzPuppetVertex v   = vertices[idx];
    float2 px = float2(
        xform_row0.x * v.pos.x + xform_row0.y * v.pos.y + xform_row0.z,
        xform_row1.x * v.pos.x + xform_row1.y * v.pos.y + xform_row1.z);
    float2 vp  = screen_constants[0].viewport.xy;
    float2 ndc = px * (2.0f / vp) - 1.0f;
    VSOut o;
    o.pos = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
    o.uv  = v.uv;
    return o;
}
)";

    constexpr const char* kPuppetPs = R"(
cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;
    float4 xform_row1;
};

Texture2D<float4> atlas   : register(t2, space2);
SamplerState      atlas_s : register(s0, space2);

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 tex     = atlas.SampleLevel(atlas_s, input.uv, 0.0f);
    float  opacity = xform_row0.w;
    return float4(tex.rgb, tex.a * opacity);
}
)";

    void write_text_file(const fs::path& path, const char* text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "failed to open " << path.string();
        out << text;
    }

    // The puppet program's SRG, authored directly (create_custom takes an explicit
    // CustomRenderProgramDesc). MUST match puppet_vs/ps.hlsl: a "puppet_part"
    // root-constant block (8 dwords = 2 float4) at (b0, space2), the Screen view
    // head at (t0, space0), and the object SRG PuppetVertices t0 / PuppetIndices
    // t1 / PuppetAtlas t2 + a LinearClamp sampler s0, all in space 2.
    ea::CustomRenderProgramDesc puppet_program_desc(const std::string& name)
    {
        ea::CustomRenderProgramDesc desc{};
        desc.name = name;
        desc.binding_model = ea::RenderBindingModel::MeshVertexPull;
        desc.topology = ea::RenderPrimitiveTopology::TriangleList;
        desc.default_domain = ea::RenderDomain::Opaque;
        desc.default_policy_flags = ea::RenderPolicy_None;
        desc.input_layout = ea::InputLayoutKind::None;
        desc.blend_mode = wz::rhi::BlendMode::AlphaBlend;
        desc.depth_mode = ea::DepthMode::Disabled;
        desc.raster_mode = ea::RasterMode::SolidCullNone;

        desc.root_constants.push_back(ea::RootConstantBinding{
            .visibility = ea::ShaderVisibility::All,
            .shader_register = 0,
            .register_space = 2,
            .value_count = 8,
            .semantic = "puppet_part",
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::StructuredBufferSRV,
            .visibility = ea::ShaderVisibility::Vertex,
            .semantic = ea::DescriptorSemantic::ScreenConstants,
            .shader_register = 0,
            .register_space = 0,
            .descriptor_count = 1,
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::StructuredBufferSRV,
            .visibility = ea::ShaderVisibility::Vertex,
            .semantic = ea::DescriptorSemantic::PuppetVertices,
            .shader_register = 0,
            .register_space = 2,
            .descriptor_count = 1,
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::StructuredBufferSRV,
            .visibility = ea::ShaderVisibility::Vertex,
            .semantic = ea::DescriptorSemantic::PuppetIndices,
            .shader_register = 1,
            .register_space = 2,
            .descriptor_count = 1,
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::TextureSRV,
            .visibility = ea::ShaderVisibility::Pixel,
            .semantic = ea::DescriptorSemantic::PuppetAtlas,
            .shader_register = 2,
            .register_space = 2,
            .descriptor_count = 1,
        });
        desc.static_samplers.push_back(ea::StaticSamplerBinding{
            .kind = ea::StaticSamplerKind::LinearClamp,
            .visibility = ea::ShaderVisibility::Pixel,
            .shader_register = 0,
            .register_space = 2,
        });
        return desc;
    }
}

TEST(RhiPuppetRender, RealizesAndRecordsPartPackets)
{
    const fs::path fixture =
        fs::path(WZ_TEST_FIXTURE_DIR) / "inochi" / "Aka.inp";
    if (!fs::exists(fixture)) {
        GTEST_SKIP() << "no Aka.inp fixture for the on-device puppet render test";
    }

    wz::window::WindowDesc window_desc{};
    window_desc.title = "rhi_puppet_render_test";
    window_desc.width = 256;
    window_desc.height = 256;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device puppet render test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device puppet render test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;

        const fs::path root =
            fs::temp_directory_path()
            / ("wozzits_puppet_render_test_"
               + std::to_string(static_cast<unsigned long long>(
                     std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count())));
        fs::remove_all(root);
        write_text_file(root / "shaders" / "puppet" / "puppet_vs.hlsl", kPuppetVs);
        write_text_file(root / "shaders" / "puppet" / "puppet_ps.hlsl", kPuppetPs);

        ea::EngineAssetLibrary assets(gpu, logger, root.string());

        // 1) The .inp as a raw-file carrier (absolute fixture path).
        const wz::asset::AssetKey inp_file = assets.files().register_file_node(
            wz::fs::join(WZ_TEST_FIXTURE_DIR, "inochi/Aka.inp"),
            ea::kRawFileSchema,
            ea::kAssetTypeRawFile);
        ASSERT_FALSE(inp_file == wz::asset::AssetKey{});

        // 2) The Puppet asset (loads the container + publishes residency).
        const ea::PuppetAsset puppet =
            assets.puppets().create_puppet_from_file(
                ea::PuppetFromFileDesc{
                    .name = "puppet/aka",
                    .source_file = inp_file });
        ASSERT_TRUE(puppet.valid());

        // 3) The puppet render program (space2 bindings require SM 5.1).
        const ea::ShaderPairAsset shaders =
            assets.shaders().create_shader_pair({
                .name = "puppet/program",
                .vertex_path = "shaders/puppet/puppet_vs.hlsl",
                .pixel_path = "shaders/puppet/puppet_ps.hlsl",
                .vertex_target = "vs_5_1",
                .pixel_target = "ps_5_1",
            });
        ASSERT_TRUE(shaders.valid());

        ea::CustomRenderProgramDesc program_desc =
            puppet_program_desc("puppet/program");
        program_desc.vertex_shader = shaders.vertex_shader;
        program_desc.pixel_shader = shaders.pixel_shader;
        const ea::RenderProgramAsset program =
            assets.render_programs().create_custom(program_desc);
        ASSERT_TRUE(program.valid());

        // 4) The puppet renderable binding the two.
        const ea::RenderableAsset renderable =
            assets.renderables().create_puppet_rhi(
                ea::PuppetRhiRenderableDesc{
                    .name = "puppet/renderable",
                    .puppet = puppet,
                    .program = program });
        ASSERT_TRUE(renderable.valid());

        ASSERT_TRUE(assets.commit());
        const ea::ResolveReport resolve = assets.resolve_all();
        for (const ea::ResolveFailure& f : resolve.failures) {
            ADD_FAILURE() << "resolve failure: error="
                          << static_cast<int>(f.error);
        }
        ASSERT_TRUE(resolve.ok());

        // The puppet became resident: its first atlas page (Aka.inp atlases the
        // per-part textures into a few pages).
        const wz::rhi::GpuResourceHandle atlas0 = gpu.resources.find(
            wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(puppet.output, "atlas_0"), {} });
        ASSERT_TRUE(atlas0.valid())
            << "puppet atlas page 0 did not become resident";

        // Drive one device frame through the renderer.
        wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);

        ea::SceneNodeAsset node{};
        node.id = wz::scene::AuthoredEntityId{ 1 };
        node.name = "puppet";
        node.visible = true;
        node.renderable_asset = renderable.output;
        const std::vector<ea::SceneNodeAsset> nodes{ node };

        const wz::math::Mat4 view_projection = wz::math::Mat4::identity();
        const wz::math::Vec3 camera_world_pos{ 0.0f, 0.0f, 0.0f };

        ASSERT_TRUE(wz::gpu::begin_frame(device));
        wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
        const bool recorded = renderer.render_scene(
            nodes, assets, view_projection, camera_world_pos);
        EXPECT_TRUE(recorded)
            << "puppet failed to realize or the recorder rejected the Part draws";
        ASSERT_TRUE(wz::gpu::end_frame(device));

        // The puppet must render VISIBLE pixels to the backbuffer (this structural
        // test historically only checked recording, never output). Read it back and
        // count texels that differ from the ~(26,26,31) clear.
        {
            std::vector<std::uint8_t> bb;
            ASSERT_TRUE(wz::gpu::dx12::internal::read_backbuffer_rgba8_dx12(
                device, bb));
            std::size_t nonclear = 0;
            for (std::size_t i = 0; i + 3 < bb.size(); i += 4) {
                const int dr = static_cast<int>(bb[i]) - 26;
                const int dg = static_cast<int>(bb[i + 1]) - 26;
                const int db = static_cast<int>(bb[i + 2]) - 31;
                if (dr > 15 || dr < -15 || dg > 15 || dg < -15
                    || db > 15 || db < -15) {
                    ++nonclear;
                }
            }
            EXPECT_GT(nonclear, 0u)
                << "the puppet rendered no visible pixels to the backbuffer";
        }
        wz::gpu::present(device, /*sync_interval*/ 0);

        // Offscreen render-to-texture (S6): render the SAME puppet into an RGBA8
        // render target and read it back. The puppet's Parts must leave
        // non-transparent pixels -- a real multi-draw (PSO + geometry) into a
        // texture, not just a clear. The RT is a 512x512 SQUARE, deliberately a
        // different size + aspect than the backbuffer, to prove per-target placement
        // (#280): the puppet is re-fitted to the target each render, so it lands.
        wz::gpu::TextureDesc rt_desc{};
        rt_desc.width = 512;
        rt_desc.height = 512;
        rt_desc.format = wz::gpu::TextureFormat::RGBA8Unorm;
        rt_desc.render_target = true;
        const wz::gpu::GPUHandle rt = wz::gpu::create_texture(device, rt_desc);
        ASSERT_TRUE(rt.valid());

        ASSERT_TRUE(wz::gpu::begin_frame(device));
        const bool rt_recorded = renderer.render_scene(
            nodes, assets, view_projection, camera_world_pos, {}, nullptr, rt);
        EXPECT_TRUE(rt_recorded)
            << "puppet failed to render into the offscreen target";
        ASSERT_TRUE(wz::gpu::end_frame(device));

        std::vector<std::uint8_t> pixels;
        ASSERT_TRUE(
            wz::gpu::dx12::internal::read_texture_rgba8_dx12(device, rt, pixels));
        ASSERT_EQ(pixels.size(),
            static_cast<std::size_t>(rt_desc.width) * rt_desc.height * 4u);
        // The target was cleared to (0,0,0,0); count texels the puppet touched (any
        // channel non-zero -- overlay AlphaBlend leaves the visible result in RGB).
        std::size_t drawn = 0;
        std::uint8_t max_channel = 0;
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
            std::uint8_t m = pixels[i];
            if (pixels[i + 1] > m) m = pixels[i + 1];
            if (pixels[i + 2] > m) m = pixels[i + 2];
            if (pixels[i + 3] > m) m = pixels[i + 3];
            if (m > 8u) {
                ++drawn;
            }
            if (m > max_channel) max_channel = m;
        }
        EXPECT_GT(drawn, 0u)
            << "the puppet left no pixels in the offscreen render target "
               "(max channel value seen = " << static_cast<int>(max_channel) << ")";
        wz::gpu::release_texture(device, rt);

        // Structural wiring proofs:
        //  - the puppet program realized from the asset compiler (no render-time
        //    bridge), like the splat/clipmap tests assert,
        EXPECT_GT(renderer.registered_program_count(), 0u);
        EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u)
            << "puppet program was bridged at render time, not produced by the "
               "asset compiler";
        //  - the per-Part object SRGs bound descriptor tables (Aka has many
        //    Parts, so the puppet records many packets).
        EXPECT_GT(renderer.cached_descriptor_table_count(), 0u)
            << "puppet Part object SRGs did not bind descriptor tables";
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
