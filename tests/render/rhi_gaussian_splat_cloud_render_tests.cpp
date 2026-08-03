// tests/render/rhi_gaussian_splat_cloud_render_tests.cpp
//
// On-device integration coverage for #208: a scalar-field-derived gaussian
// splat cloud renderable (schema 0x709) realizes + records a draw through
// RhiSceneRenderer on the wozzits-rhi path. It builds, end to end:
//   * a small procedural scalar field,
//   * the field -> gaussian splat cloud converter (resident as a decoded splat
//     StructuredBuffer, #208),
//   * a SplatPull render program (the binding_layout==3 object SRG: a
//     splat_view root-constant block + the SplatCloud StructuredBuffer SRV),
//   * the 0x709 gaussian-splat-cloud renderable binding them,
// then drives one device frame and asserts the program realizes, the resident
// splat buffer binds, the 1-descriptor object SRG satisfies its layout, and the
// (non-indexed, instanced-quad) draw packet records WITHOUT the recorder
// rejecting it. This structurally proves the wiring; it does NOT (and cannot)
// verify the cloud looks right — there is no view of the viewport. Skipped when
// no GPU device is available.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_scene_renderer.h>

#include <gpu/gpu.h>
#include <math/mat4.h>
#include <math/math_types.h>
#include <window/window2.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    namespace ea = wz::engine::assets;
    namespace fs = std::filesystem;

    // The splat-cloud shaders, written into a self-contained temp resource root
    // so the test never depends on the staged resources tree. The VS mirrors
    // resources/shaders/gaussian_splat/gaussian_splat_field_cloud_vs.hlsl: the
    // object SRG is space2, the cbuffer is the 36-float SplatViewConstants
    // block the binding_layout==3 preset declares, and the splat cloud is a
    // StructuredBuffer at (t0, space2). vid/4 = splat, vid&3 = corner.
    constexpr const char* kSplatVs = R"(
cbuffer SplatView : register(b0, space2)
{
    float4x4 world;
    float4x4 view_proj;
    float4   viewport_and_size;
};

struct Splat
{
    float3 position;
    float  opacity;
    float3 scale;
    float  pad0;
    float4 rotation;
    float3 color;
    uint   pad1;
};

StructuredBuffer<Splat> g_splats : register(t0, space2);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float  opacity  : OPACITY;
    float2 uv       : TEXCOORD0;
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    uint  splat_index = vertex_id / 4u;
    Splat s           = g_splats[splat_index];

    float2 corners[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };
    float2 corner = corners[vertex_id & 3u];

    float size = max(viewport_and_size.z, 0.000001f);
    float3 sc  = max(s.scale, float3(0.000001f, 0.000001f, 0.000001f));

    float4 world_center = mul(world, float4(s.position, 1.0f));
    float4 clip_center  = mul(view_proj, world_center);

    float2 ndc_offset = corner * sc.xy * size * 0.01f;
    float4 clip_pos   = clip_center;
    clip_pos.xy      += ndc_offset * clip_center.w;

    VSOutput o;
    o.position = clip_pos;
    o.color    = s.color;
    o.opacity  = s.opacity;
    o.uv       = corner;
    return o;
}
)";

    constexpr const char* kSplatPs = R"(
struct PSInput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float  opacity  : OPACITY;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float r2 = dot(input.uv, input.uv);
    float gaussian = exp(-0.5f * r2 * 9.0f);
    return float4(input.color, saturate(input.opacity * gaussian));
}
)";

    void write_text_file(const fs::path& path, const char* text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "failed to open " << path.string();
        out << text;
    }

    // The binding_layout==3 object SRG, authored directly (create_custom takes
    // an explicit CustomRenderProgramDesc rather than the binding_layout enum
    // the JSON path uses). MUST match the preset in render_program_compilers.cpp:
    // a 36-float "splat_view" root-constant block at (b0, space2) and the
    // SplatCloud StructuredBuffer SRV (t0, space2).
    ea::CustomRenderProgramDesc splat_program_desc(const std::string& name)
    {
        ea::CustomRenderProgramDesc desc{};
        desc.name = name;
        desc.binding_model = ea::RenderBindingModel::SplatPull;
        desc.topology = ea::RenderPrimitiveTopology::TriangleStrip;
        desc.default_domain = ea::RenderDomain::Splat;
        desc.default_policy_flags = ea::RenderPolicy_DepthTest;
        desc.input_layout = ea::InputLayoutKind::None;
        desc.blend_mode = wz::rhi::BlendMode::AlphaBlend;
        desc.depth_mode = ea::DepthMode::TestNoWrite;
        desc.raster_mode = ea::RasterMode::SolidCullNone;
        desc.root_constants.push_back(ea::RootConstantBinding{
            .visibility = ea::ShaderVisibility::Vertex,
            .shader_register = 0,
            .register_space = 2,
            .value_count = 36,
            .semantic = "splat_view",
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::StructuredBufferSRV,
            .visibility = ea::ShaderVisibility::Vertex,
            .semantic = ea::DescriptorSemantic::SplatCloud,
            .shader_register = 0,
            .register_space = 2,
            .descriptor_count = 1,
        });
        return desc;
    }
}

TEST(RhiGaussianSplatCloudRender, RealizesAndRecordsADraw)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "rhi_gaussian_splat_cloud_render_test";
    window_desc.width = 128;
    window_desc.height = 128;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device splat render test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device splat test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;

        const fs::path root =
            fs::temp_directory_path()
            / ("wozzits_splat_cloud_render_test_"
               + std::to_string(static_cast<unsigned long long>(
                     std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count())));
        fs::remove_all(root);
        write_text_file(
            root / "shaders" / "gaussian_splat" / "field_cloud_vs.hlsl",
            kSplatVs);
        write_text_file(
            root / "shaders" / "gaussian_splat" / "field_cloud_ps.hlsl",
            kSplatPs);

        ea::EngineAssetLibrary assets(gpu, logger, root.string());

        // 1) A small procedural scalar field.
        const ea::ScalarFieldAsset field =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "splat/height",
                .width = 16,
                .height = 16,
                .generator = ea::ScalarFieldGenerator::RadialGradient,
            });
        ASSERT_TRUE(field.valid());

        // 2) The field -> gaussian splat cloud converter (resident #208).
        ea::GaussianSplatFromScalarFieldDesc cloud_desc{};
        cloud_desc.name = "splat/cloud";
        cloud_desc.scalar_field_key = field.output;
        cloud_desc.height_scale = 4.0f;
        cloud_desc.splat_scale = 0.2f;
        cloud_desc.opacity = 0.9f;
        cloud_desc.subsample_step = 2;  // 8x8 = 64 splats
        const ea::GaussianSplatCloudAsset cloud =
            assets.gaussian_splats().create_from_scalar_field(cloud_desc);
        ASSERT_TRUE(cloud.valid());

        // 3) SplatPull render program (binding_layout==3 object SRG). The
        // space2 bindings require Shader Model 5.1.
        const ea::ShaderPairAsset shaders =
            assets.shaders().create_shader_pair({
                .name = "splat/program",
                .vertex_path = "shaders/gaussian_splat/field_cloud_vs.hlsl",
                .pixel_path = "shaders/gaussian_splat/field_cloud_ps.hlsl",
                .vertex_target = "vs_5_1",
                .pixel_target = "ps_5_1",
            });
        ASSERT_TRUE(shaders.valid());

        ea::CustomRenderProgramDesc program_desc =
            splat_program_desc("splat/program");
        program_desc.vertex_shader = shaders.vertex_shader;
        program_desc.pixel_shader = shaders.pixel_shader;
        const ea::RenderProgramAsset program =
            assets.render_programs().create_custom(program_desc);
        ASSERT_TRUE(program.valid());

        // 4) The 0x709 gaussian-splat-cloud renderable binding the two.
        ea::GaussianSplatCloudRenderSettings settings{};
        settings.splat_size = 2.0f;
        const ea::RenderableAsset renderable =
            assets.renderables().create_gaussian_splat_cloud_rhi(
                ea::GaussianSplatCloudRhiRenderableDesc{
                    .name = "splat/renderable",
                    .splat_cloud = cloud,
                    .program = program,
                    .settings = settings,
                });
        ASSERT_TRUE(renderable.valid());

        ASSERT_TRUE(assets.commit());
        const ea::ResolveReport resolve = assets.resolve_all();
        for (const ea::ResolveFailure& f : resolve.failures) {
            ADD_FAILURE() << "resolve failure: error="
                          << static_cast<int>(f.error);
        }
        ASSERT_TRUE(resolve.ok());

        // The compiler produced the splat recipe binding the cloud by key.
        const ea::RhiRenderableRecipe* recipe =
            assets.renderables().get_rhi_renderable_recipe(renderable);
        ASSERT_NE(recipe, nullptr);
        EXPECT_FALSE(
            recipe->gaussian_splat_cloud_key == wz::asset::AssetKey{});

        // The decoded splat StructuredBuffer is resident (the VS pulls it).
        const wz::rhi::GpuResourceHandle splat_buffer = gpu.resources.find(
            wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(cloud.output, "splat_cloud"), {} });
        ASSERT_TRUE(splat_buffer.valid())
            << "splat cloud did not become resident as a structured buffer";

        // Drive one device frame through the renderer.
        wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);

        ea::SceneNodeAsset node{};
        node.id = wz::scene::AuthoredEntityId{ 1 };
        node.name = "splat";
        node.visible = true;
        node.renderable_asset = renderable.output;
        const std::vector<ea::SceneNodeAsset> nodes{ node };

        const wz::math::Mat4 view_projection = wz::math::Mat4::identity();
        const wz::math::Vec3 camera_world_pos{ 0.0f, 0.0f, 0.0f };

        // #327 frame capture: watch what the whole cloud actually costs.
        std::vector<
            wz::engine::rendering::RhiDx12CommandRecorder::CapturedDraw>
            captured;
        renderer.begin_frame_capture(&captured);

        ASSERT_TRUE(wz::gpu::begin_frame(device));
        wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
        const bool recorded = renderer.render_scene(
            nodes, assets, view_projection, camera_world_pos);
        EXPECT_TRUE(recorded)
            << "splat renderable failed to realize or the recorder rejected "
               "the draw";
        ASSERT_TRUE(wz::gpu::end_frame(device));
        wz::gpu::present(device, /*sync_interval*/ 0);
        renderer.end_frame_capture();

        // The batching claim, measured. A whole cloud is ONE draw -- the
        // Draw-submission ladder's R2 batching, reached here and nowhere else
        // in the engine. But it reaches it by VERTEX AMPLIFICATION, not by
        // hardware instancing: DrawInstanced is D3D12's name for a non-indexed
        // draw, and the instance count is 1 (nothing in the engine ever sets
        // DrawArgs::instance_count -- rhi's make_draw_args leaves the default).
        // The #327 register said "a single instanced draw", which read the API
        // name as the mechanism. Six vertices per splat: two self-contained
        // triangles, because the program is a TriangleList and a 4-vertex quad
        // would span adjacent splats.
        ASSERT_EQ(captured.size(), static_cast<std::size_t>(1))
            << "the cloud did not collapse to one draw";
        std::printf(
            "[ CAPTURE  ] splat cloud: DrawInstanced(vertices=%u, "
            "instances=%u) = %u splats x 6 verts\n",
            captured[0].vertex_count_per_instance,
            captured[0].instance_count,
            captured[0].vertex_count_per_instance / 6u);
        EXPECT_EQ(captured[0].instance_count, 1u);
        EXPECT_FALSE(captured[0].indexed);
        EXPECT_EQ(captured[0].vertex_count_per_instance % 6u, 0u)
            << "vertex count is not a whole number of 6-vertex splat quads";

        // Structural wiring proofs:
        //  - the splat program realized from the asset compiler (zero render-
        //    time bridges, like the clipmap/rebind tests assert),
        EXPECT_GT(renderer.registered_program_count(), 0u);
        EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u)
            << "splat program was bridged at render time, not produced by the "
               "asset compiler";
        //  - the renderer uploaded NO pull buffers of its own for the splat
        //    path: the two resident resources are both asset-owned — the splat
        //    StructuredBuffer (#208) and the source field's R32 texture (#197,
        //    auto-published when the procedural field resolved). If the renderer
        //    had CPU-uploaded a mesh, this would be 3.
        EXPECT_EQ(renderer.resident_gpu_resource_count(), 2u)
            << "expected the resident splat StructuredBuffer + the source "
               "field's auto-published R32 texture (both asset-owned)";
        //  - the 1-descriptor object SRG (the SplatCloud SRV) built a descriptor
        //    table.
        EXPECT_GT(renderer.cached_descriptor_table_count(), 0u)
            << "object SRG (the SplatCloud SRV) did not bind a table";
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
