// tests/render/rhi_clipmap_landscape_render_tests.cpp
//
// On-device integration coverage for #198 slice 3b: a geometry-clipmap
// landscape renderable (schema 0x708) realizes + records a draw through
// RhiSceneRenderer on the wozzits-rhi path. It builds, end to end:
//   * a clipmap lattice mesh   (kProceduralClipmapLatticeMeshSchema),
//   * a small resident R32 scalar field (the #197 height texture),
//   * a clipmap render program (MeshVertexPull binding model, the new
//     binding_layout==2 object SRG: clipmap root constants + pulled
//     positions/indices + a height TextureSRV),
//   * the 0x708 clipmap landscape renderable binding them,
// then drives one device frame and asserts the program realizes, the resident
// height texture binds, the 3-descriptor object SRG satisfies its layout, and
// the draw packet records WITHOUT the recorder rejecting it. This structurally
// proves the wiring; it does NOT (and cannot) verify the terrain looks right —
// there is no view of the viewport. Skipped when no GPU device is available.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/mesh_asset_module.h>
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

    // The clipmap shaders, written into a self-contained temp resource root so
    // the test never depends on the staged resources tree. They mirror
    // resources/shaders/clipmap/clipmap_{vs,ps}.hlsl: the object SRG is space2,
    // and the cbuffer is packed to the 32-float ClipmapDrawConstants block the
    // binding_layout==2 preset declares.
    constexpr const char* kClipmapVs = R"(
cbuffer Clipmap : register(b0, space2)
{
    float4x4 view_projection;
    float4   lattice_translation_scale;
    float4   world_to_uv;
    float4   texel_and_vertical;
    float4   texel_dims;
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);
Texture2D<float>         heightTex : register(t2, space2);

struct VSOut
{
    float4 pos       : SV_POSITION;
    float3 world_pos : TEXCOORD0;
    float3 normal    : NORMAL;
};

float sample_height_world(float2 world_xz)
{
    float2 uv = world_to_uv.xy * world_xz + world_to_uv.zw;
    uv = clamp(uv, 0.0f, 1.0f);
    float2 max_texel = max(texel_dims.xy - 1.0f, 0.0f);
    int2 texel = int2(round(uv * max_texel));
    return heightTex.Load(int3(texel, 0));
}

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = indices[vid];
    float3 g   = positions[idx];
    float3 t   = lattice_translation_scale.xyz;
    float  s   = lattice_translation_scale.w;
    float2 world_xz = t.xz + s * g.xz;

    float h = sample_height_world(world_xz);
    float world_y = texel_and_vertical.w + texel_and_vertical.z * h;
    float3 world_pos = float3(world_xz.x, world_y, world_xz.y);

    VSOut o;
    o.pos = mul(view_projection, float4(world_pos, 1.0f));
    o.world_pos = world_pos;
    o.normal = float3(0.0f, 1.0f, 0.0f);
    return o;
}
)";

    constexpr const char* kClipmapPs = R"(
struct PSInput
{
    float4 pos       : SV_POSITION;
    float3 world_pos : TEXCOORD0;
    float3 normal    : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    float  shade = 0.3f + 0.7f * saturate(n.y);
    return float4(float3(0.3f, 0.5f, 0.25f) * shade, 1.0f);
}
)";

    void write_text_file(const fs::path& path, const char* text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "failed to open " << path.string();
        out << text;
    }

    // The binding_layout==2 object SRG, authored directly (the create_custom
    // path takes an explicit CustomRenderProgramDesc rather than the
    // binding_layout enum the JSON authoring path uses). MUST match the preset
    // in render_program_compilers.cpp: a 32-float "clipmap" root-constant block
    // at (b0, space2), pulled positions (t0) / indices (t1), and the height
    // TextureSRV (t2) at the scalar_field_texture semantic.
    ea::CustomRenderProgramDesc clipmap_program_desc(const std::string& name)
    {
        ea::CustomRenderProgramDesc desc{};
        desc.name = name;
        desc.binding_model = ea::RenderBindingModel::MeshVertexPull;
        desc.topology = ea::RenderPrimitiveTopology::TriangleList;
        desc.default_domain = ea::RenderDomain::Opaque;
        desc.default_policy_flags =
            ea::RenderPolicy_DepthTest | ea::RenderPolicy_DepthWrite;
        desc.input_layout = ea::InputLayoutKind::None;
        desc.blend_mode = ea::BlendMode::Opaque;
        desc.depth_mode = ea::DepthMode::TestWrite;
        desc.raster_mode = ea::RasterMode::SolidCullNone;
        desc.root_constants.push_back(ea::RootConstantBinding{
            .visibility = ea::ShaderVisibility::All,
            .shader_register = 0,
            .register_space = 2,
            .value_count = 32,
            .semantic = "clipmap",
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::StructuredBufferSRV,
            .visibility = ea::ShaderVisibility::Vertex,
            .semantic = ea::DescriptorSemantic::PulledMeshPositions,
            .shader_register = 0,
            .register_space = 2,
            .descriptor_count = 1,
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::StructuredBufferSRV,
            .visibility = ea::ShaderVisibility::Vertex,
            .semantic = ea::DescriptorSemantic::PulledMeshIndices,
            .shader_register = 1,
            .register_space = 2,
            .descriptor_count = 1,
        });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            .kind = ea::DescriptorKind::TextureSRV,
            .visibility = ea::ShaderVisibility::Vertex,
            .semantic = ea::DescriptorSemantic::ScalarFieldTexture,
            .shader_register = 2,
            .register_space = 2,
            .descriptor_count = 1,
        });
        return desc;
    }
}

TEST(RhiClipmapLandscapeRender, RealizesAndRecordsADraw)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "rhi_clipmap_landscape_render_test";
    window_desc.width = 128;
    window_desc.height = 128;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device clipmap render test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device clipmap test";
    }

    {
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;

        // Unique-enough temp root from the high-resolution clock so repeated
        // local runs don't collide on stale cache/shader files.
        const fs::path root =
            fs::temp_directory_path()
            / ("wozzits_clipmap_render_test_"
               + std::to_string(static_cast<unsigned long long>(
                     std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count())));
        fs::remove_all(root);
        write_text_file(root / "shaders" / "clipmap" / "clipmap_vs.hlsl",
            kClipmapVs);
        write_text_file(root / "shaders" / "clipmap" / "clipmap_ps.hlsl",
            kClipmapPs);

        ea::EngineAssetLibrary assets(gpu, logger, root.string());

        // 1) Clipmap lattice mesh (rides the CPU-pull mesh path).
        const ea::MeshAsset lattice =
            assets.meshes().create_clipmap_lattice_mesh(
                ea::ClipmapLatticeMeshDesc{
                    .name = "clipmap/lattice",
                    .level_count = 3,
                    .base_resolution = 8,
                    .cell_size = 1.0f,
                });
        ASSERT_TRUE(lattice.valid());

        // 2) Resident R32 height texture (#197) from a small procedural field.
        const ea::ScalarFieldAsset height =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "clipmap/height",
                .width = 16,
                .height = 16,
                .generator = ea::ScalarFieldGenerator::RadialGradient,
            });
        ASSERT_TRUE(height.valid());

        // 3) Clipmap render program (binding_layout==2 object SRG).
        // The clipmap shaders use register-space (space2) bindings, which
        // require Shader Model 5.1 — vs_5_0/ps_5_0 reject `register(b0, space2)`
        // (error X3721). Author them at 5.1, exactly like the rebind fixture's
        // pull shaders.
        const ea::ShaderPairAsset shaders =
            assets.shaders().create_shader_pair({
                .name = "clipmap/program",
                .vertex_path = "shaders/clipmap/clipmap_vs.hlsl",
                .pixel_path = "shaders/clipmap/clipmap_ps.hlsl",
                .vertex_target = "vs_5_1",
                .pixel_target = "ps_5_1",
            });
        ASSERT_TRUE(shaders.valid());

        ea::CustomRenderProgramDesc program_desc =
            clipmap_program_desc("clipmap/program");
        program_desc.vertex_shader = shaders.vertex_shader;
        program_desc.pixel_shader = shaders.pixel_shader;
        const ea::RenderProgramAsset program =
            assets.render_programs().create_custom(program_desc);
        ASSERT_TRUE(program.valid());

        // 4) The 0x708 clipmap landscape renderable binding the three.
        ea::ClipmapLandscapeRenderSettings settings{};
        settings.world_origin[0] = -8.0f;
        settings.world_origin[1] = -8.0f;
        settings.world_size[0] = 16.0f;
        settings.world_size[1] = 16.0f;
        settings.vertical_scale = 4.0f;
        settings.base_height = 0.0f;
        settings.lattice_world_cell_size = 1.0f;

        const ea::RenderableAsset renderable =
            assets.renderables().create_clipmap_landscape(
                ea::ClipmapLandscapeRenderableDesc{
                    .name = "clipmap/landscape",
                    .lattice_mesh = lattice,
                    .height_field = height,
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

        // The compiler produced the clipmap recipe binding all three by identity.
        const ea::RhiRenderableRecipe* recipe =
            assets.renderables().get_rhi_renderable_recipe(renderable);
        ASSERT_NE(recipe, nullptr);
        EXPECT_FALSE(recipe->height_texture_key == wz::asset::AssetKey{});
        EXPECT_FALSE(recipe->mesh_key == wz::asset::AssetKey{});

        // The #197 height texture is resident (the VS samples it).
        const wz::rhi::GpuResourceHandle texture = gpu.resources.find(
            wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(height.output, "field_texture"), {} });
        ASSERT_TRUE(texture.valid())
            << "height scalar field did not become resident as a texture";

        // Drive one device frame through the renderer.
        wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);

        ea::SceneNodeAsset node{};
        node.id = wz::scene::AuthoredEntityId{ 1 };
        node.name = "clipmap";
        node.visible = true;
        node.renderable_asset = renderable.output;
        const std::vector<ea::SceneNodeAsset> nodes{ node };

        const wz::math::Mat4 view_projection = wz::math::Mat4::identity();
        const wz::math::Vec3 camera_world_pos{ 2.0f, 5.0f, -3.0f };

        ASSERT_TRUE(wz::gpu::begin_frame(device));
        wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
        const bool recorded = renderer.render_scene(
            nodes, assets, view_projection, camera_world_pos);
        EXPECT_TRUE(recorded)
            << "clipmap renderable failed to realize or the recorder rejected "
               "the draw";
        ASSERT_TRUE(wz::gpu::end_frame(device));
        wz::gpu::present(device, /*sync_interval*/ 0);

        // Structural wiring proofs:
        //  - the clipmap program realized (came from the asset compiler — zero
        //    render-time bridges, like the rebind test asserts for #192/#193),
        EXPECT_GT(renderer.registered_program_count(), 0u);
        EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u)
            << "clipmap program was bridged at render time, not produced by the "
               "asset compiler";
        //  - the renderer uploaded + bound the 2 lattice pull buffers (the
        //    height texture is asset-owned and counted separately),
        EXPECT_EQ(renderer.resident_gpu_resource_count(), 3u)
            << "expected 2 renderer-owned lattice pull buffers + 1 resident "
               "height texture";
        //  - the 3-descriptor object SRG (positions + indices + height texture)
        //    built a descriptor table — the height TextureSRV bound through the
        //    generic step-1 path.
        EXPECT_GT(renderer.cached_descriptor_table_count(), 0u)
            << "object SRG (incl. the height TextureSRV) did not bind a table";
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
