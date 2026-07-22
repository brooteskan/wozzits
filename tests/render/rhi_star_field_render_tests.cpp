// tests/render/rhi_star_field_render_tests.cpp
//
// On-device integration coverage for #266: a star-field renderable (schema
// 0x70B) realizes + records a draw through RhiSceneRenderer on the wozzits-rhi
// path. It builds, end to end:
//   * a baked .star_catalog.json -> StarCatalog asset (resident as a decoded
//     point StructuredBuffer under "star_catalog"),
//   * a SplatPull render program (the binding_layout==5 object SRG: a star_view
//     root-constant block + the StarCatalog StructuredBuffer SRV), additive
//     blend, TriangleList,
//   * the 0x70B star-field renderable binding them,
// then drives one device frame and asserts the program realizes, the resident
// star buffer binds, the 1-descriptor object SRG satisfies its layout, and the
// non-indexed billboard draw (vertex_count = 6 * star_count) records WITHOUT the
// recorder rejecting it. Mirror of the gaussian-splat-cloud render test; proves
// the wiring, not the pixels. Skipped when no GPU device is available.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/star_catalog_asset_module.h>
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

    // Baked star catalog: three real bright stars, all inside the default
    // magnitude window -> 3 stars -> 3 * 6 = 18 vertices in the draw.
    constexpr const char* kStarJson = R"({
  "version": 1,
  "source_name": "render_test_stars",
  "stars": [
    { "ra_hours": 6.7525, "dec_deg": -16.7161, "vmag": -1.46, "bv": 0.00 },
    { "ra_hours": 18.6156, "dec_deg": 38.7837, "vmag": 0.03, "bv": 0.00 },
    { "ra_hours": 5.9195, "dec_deg": 7.4071, "vmag": 0.42, "bv": 1.85 }
  ]
})";

    // Star VS shim: the binding_layout==5 object SRG (a 36-float star_view root
    // constant + the StarCatalog StructuredBuffer SRV at t0/space2). vid/6 =
    // star, vid%6 = corner (two triangles). Mirrors
    // resources/shaders/star_field/star_field_vs.hlsl.
    constexpr const char* kStarVs = R"(
cbuffer StarView : register(b0, space2)
{
    float4x4 world;
    float4x4 view_proj;
    float4   camera_and_size;
    float4   star_params;
};

struct Star
{
    float3 direction;
    float  solid_angle;
    float3 radiance;
    float  magnitude;
};

StructuredBuffer<Star> g_stars : register(t0, space2);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    uint star_index = vertex_id / 6u;
    Star s          = g_stars[star_index];

    float2 quad[6] =
    {
        float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2( 1.0f, -1.0f),
        float2( 1.0f, -1.0f), float2(-1.0f, 1.0f), float2( 1.0f,  1.0f)
    };
    float2 corner = quad[vertex_id % 6u];

    float3 dir = normalize(mul((float3x3)world, s.direction));
    float3 center = camera_and_size.xyz + dir;
    float3 up0 = (abs(dir.y) < 0.99f) ? float3(0,1,0) : float3(1,0,0);
    float3 right = normalize(cross(up0, dir));
    float3 up    = cross(dir, right);
    float h = max(camera_and_size.w, 0.0001f) * 0.01f;
    float3 corner_ws = center + corner.x * h * right + corner.y * h * up;

    float4 clip = mul(view_proj, float4(corner_ws, 1.0f));
    clip.z = clip.w;

    VSOutput o;
    o.position = clip;
    o.color    = s.radiance * star_params.x;
    o.uv       = corner;
    return o;
}
)";

    constexpr const char* kStarPs = R"(
struct PSInput
{
    float4 position : SV_POSITION;
    float3 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float d2 = dot(input.uv, input.uv);
    if (d2 > 1.0f) discard;
    float falloff = saturate(1.0f - d2);
    falloff *= falloff;
    return float4(input.color * falloff, falloff);
}
)";

    void write_text_file(const fs::path& path, const char* text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "failed to open " << path.string();
        out << text;
    }

    // The binding_layout==5 object SRG, authored directly. MUST match the preset
    // in render_program_compilers.cpp: a 36-float "star_view" root-constant block
    // at (b0, space2) and the StarCatalog StructuredBuffer SRV (t0, space2).
    ea::CustomRenderProgramDesc star_program_desc(const std::string& name)
    {
        ea::CustomRenderProgramDesc desc{};
        desc.name = name;
        desc.binding_model = ea::RenderBindingModel::SplatPull;
        desc.topology = ea::RenderPrimitiveTopology::TriangleList;
        desc.default_domain = ea::RenderDomain::Splat;
        desc.input_layout = ea::InputLayoutKind::None;
        desc.blend_mode = wz::rhi::BlendMode::Additive;
        desc.depth_mode = ea::DepthMode::Disabled;
        desc.raster_mode = ea::RasterMode::SolidCullNone;
        desc.root_constants.push_back(ea::RootConstantBinding{
            .visibility = ea::ShaderVisibility::Vertex,
            .shader_register = 0,
            .register_space = 2,
            .value_count = 40,
            .semantic = "star_view",
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::StructuredBufferSRV,
            .visibility = ea::ShaderVisibility::Vertex,
            .semantic = ea::DescriptorSemantic::StarCatalog,
            .shader_register = 0,
            .register_space = 2,
            .descriptor_count = 1,
        });
        return desc;
    }
}

TEST(RhiStarFieldRender, RealizesAndRecordsADraw)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "rhi_star_field_render_test";
    window_desc.width = 128;
    window_desc.height = 128;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device star render test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device star test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;

        const fs::path root =
            fs::temp_directory_path()
            / ("wozzits_star_field_render_test_"
               + std::to_string(static_cast<unsigned long long>(
                     std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count())));
        fs::remove_all(root);
        write_text_file(root / "shaders" / "star_field" / "star_vs.hlsl", kStarVs);
        write_text_file(root / "shaders" / "star_field" / "star_ps.hlsl", kStarPs);
        write_text_file(root / "stars.star_catalog.json", kStarJson);

        ea::EngineAssetLibrary assets(gpu, logger, root.string());

        // 1) Baked JSON -> star catalog (resident #266).
        const ea::JSONAsset json = assets.json().create_json({
            .name = "stars/json",
            .path = "stars.star_catalog.json",
        });
        ASSERT_TRUE(json.valid());
        const ea::StarCatalogAsset catalog =
            assets.star_catalogs().create_from_json({
                .name = "stars/catalog",
                .json_key = json.output,
            });
        ASSERT_TRUE(catalog.valid());

        // 2) SplatPull star program (binding_layout==5 object SRG). space2
        // bindings require Shader Model 5.1.
        const ea::ShaderPairAsset shaders =
            assets.shaders().create_shader_pair({
                .name = "stars/program",
                .vertex_path = "shaders/star_field/star_vs.hlsl",
                .pixel_path = "shaders/star_field/star_ps.hlsl",
                .vertex_target = "vs_5_1",
                .pixel_target = "ps_5_1",
            });
        ASSERT_TRUE(shaders.valid());

        ea::CustomRenderProgramDesc program_desc =
            star_program_desc("stars/program");
        program_desc.vertex_shader = shaders.vertex_shader;
        program_desc.pixel_shader = shaders.pixel_shader;
        const ea::RenderProgramAsset program =
            assets.render_programs().create_custom(program_desc);
        ASSERT_TRUE(program.valid());

        // 3) The 0x70B star-field renderable binding the two.
        ea::StarFieldRenderSettings settings{};
        settings.star_size = 2.0f;
        settings.intensity = 1.5f;
        const ea::RenderableAsset renderable =
            assets.renderables().create_star_field_rhi(
                ea::StarFieldRhiRenderableDesc{
                    .name = "stars/renderable",
                    .star_catalog = catalog,
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

        // The compiler produced the star recipe binding the catalog by key.
        const ea::RhiRenderableRecipe* recipe =
            assets.renderables().get_rhi_renderable_recipe(renderable);
        ASSERT_NE(recipe, nullptr);
        EXPECT_FALSE(recipe->star_catalog_key == wz::asset::AssetKey{});

        // The decoded star StructuredBuffer is resident (the VS pulls it).
        const wz::rhi::GpuResourceHandle star_buffer = gpu.resources.find(
            wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(catalog.output, "star_catalog"), {} });
        ASSERT_TRUE(star_buffer.valid())
            << "star catalog did not become resident as a structured buffer";

        // Drive one device frame through the renderer.
        wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);

        ea::SceneNodeAsset node{};
        node.id = wz::scene::AuthoredEntityId{ 1 };
        node.name = "stars";
        node.visible = true;
        node.renderable_asset = renderable.output;
        const std::vector<ea::SceneNodeAsset> nodes{ node };

        const wz::math::Mat4 view_projection = wz::math::Mat4::identity();
        const wz::math::Vec3 camera_world_pos{ 0.0f, 0.0f, 0.0f };

        ASSERT_TRUE(wz::gpu::begin_frame(device));
        wz::gpu::clear(device, 0.02f, 0.02f, 0.05f, 1.0f);
        const bool recorded = renderer.render_scene(
            nodes, assets, view_projection, camera_world_pos);
        EXPECT_TRUE(recorded)
            << "star renderable failed to realize or the recorder rejected "
               "the draw";
        ASSERT_TRUE(wz::gpu::end_frame(device));
        wz::gpu::present(device, /*sync_interval*/ 0);

        // Structural wiring proofs:
        //  - the star program realized from the asset compiler (no render-time
        //    bridge),
        EXPECT_GT(renderer.registered_program_count(), 0u);
        EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u)
            << "star program was bridged at render time, not produced by the "
               "asset compiler";
        //  - exactly one resident, asset-owned resource: the star buffer (no
        //    source field / mesh upload).
        EXPECT_EQ(renderer.resident_gpu_resource_count(), 1u)
            << "expected only the resident star StructuredBuffer (asset-owned)";
        //  - the 1-descriptor object SRG (the StarCatalog SRV) built a table.
        EXPECT_GT(renderer.cached_descriptor_table_count(), 0u)
            << "object SRG (the StarCatalog SRV) did not bind a table";
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
