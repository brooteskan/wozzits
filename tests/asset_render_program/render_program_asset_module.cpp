#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/render_program.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_scene_renderer.h>
#include <math/mat4.h>
#include <math/math_types.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/builtin_render_programs.h>
#include <engine/rendering/render_program_pipeline_cache.h>
#include <engine/rendering/rhi_render_program_bridge.h>
#include <wozzits/rhi/tag_registry.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>
#include <window/window2.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Consume the single rhi blend enum directly (issue #272): the engine's
// BlendMode is wz::rhi::BlendMode, so unqualified `BlendMode` in the
// `using namespace wz::engine::assets` test bodies resolves to it.
using wz::rhi::BlendMode;

namespace
{
    namespace fs = std::filesystem;

    struct TempShaderDir
    {
        fs::path root;

        TempShaderDir()
        {
            root = fs::temp_directory_path() /
                ("wozzits_render_program_tests_" +
                 std::to_string(::GetCurrentProcessId()));

            fs::remove_all(root);
            fs::create_directories(root / "shaders" / "stub");
        }

        ~TempShaderDir()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        wz::fs::Path wz_root() const { return root.string(); }

        void write_stub_shaders() const
        {
            auto write = [](const fs::path& p, const std::string& src) {
                std::ofstream out(p, std::ios::binary);
                out << src;
            };

            write(root / "shaders" / "stub" / "stub_vs.hlsl",
                "struct PSIn { float4 pos : SV_POSITION; };\n"
                "PSIn main(uint id : SV_VertexID) {\n"
                "    PSIn o; o.pos = float4(0,0,0,1); return o;\n"
                "}\n");

            write(root / "shaders" / "stub" / "stub_ps.hlsl",
                "float4 main() : SV_TARGET {\n"
                "    return float4(1,0,0,1);\n"
                "}\n");
        }
    };

    struct RenderProgramGpuFixture : public ::testing::Test
    {
        wz::Logger               logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device          device{};
        TempShaderDir            resources;

        void SetUp() override
        {
            resources.write_stub_shaders();

            wz::window::WindowDesc desc{};
            desc.title     = "render_program_test";
            desc.width     = 64;
            desc.height    = 64;
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
}

// ── Registration-only tests (no GPU needed) ──────────────────────────────────

TEST(RenderProgramAssetModule, CreateBuiltinRejectsEmptyName)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rp_reject_tests");
    wz::fs::create_directories(root);

    wz::engine::assets::EngineAssetLibrary assets(device, logger, root);

    wz::asset::AssetKey dummy_key{
        .content_hash  = {1, 0},
        .schema_hash   = {1, 0},
        .compiler_hash = {1, 0},
        .deps_hash     = {0, 0},
    };

    const auto program = assets.render_programs().create_builtin({
        .name          = "",
        .program       = wz::engine::assets::BuiltinRenderProgram::GaussianSplatDebug,
        .vertex_shader = dummy_key,
        .pixel_shader  = dummy_key,
        });

    EXPECT_FALSE(program.valid());
}

TEST(RenderProgramAssetModule, CreateCustomRejectsEmptyName)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rp_custom_reject_tests");
    wz::fs::create_directories(root);

    wz::engine::assets::EngineAssetLibrary assets(device, logger, root);

    wz::asset::AssetKey dummy_key{
        .content_hash  = {1, 0},
        .schema_hash   = {1, 0},
        .compiler_hash = {1, 0},
        .deps_hash     = {0, 0},
    };

    const auto program = assets.render_programs().create_custom({
        .name = "",
        .vertex_shader = dummy_key,
        .pixel_shader  = dummy_key,
        .binding_model = wz::engine::assets::RenderBindingModel::MeshIA,
        .input_layout  = wz::engine::assets::InputLayoutKind::MeshPositionNormalUV,
        .blend_mode    = wz::rhi::BlendMode::Opaque,
        .depth_mode    = wz::engine::assets::DepthMode::TestWrite,
        .raster_mode   = wz::engine::assets::RasterMode::SolidCullNone,
        });

    EXPECT_FALSE(program.valid());
}

TEST(RenderProgramAssetModule, CreateBuiltinRejectsCountProgram)
{
    wz::Logger logger;
    wz::gpu::Device device{};
    const wz::fs::Path root = wz::fs::join(
        wz::fs::temp_directory_path(), "wozzits_rp_count_tests");
    wz::fs::create_directories(root);

    wz::engine::assets::EngineAssetLibrary assets(device, logger, root);

    wz::asset::AssetKey dummy_key{
        .content_hash  = {1, 0},
        .schema_hash   = {1, 0},
        .compiler_hash = {1, 0},
        .deps_hash     = {0, 0},
    };

    const auto program = assets.render_programs().create_builtin({
        .name          = "test/program",
        .program       = wz::engine::assets::BuiltinRenderProgram::Count,
        .vertex_shader = dummy_key,
        .pixel_shader  = dummy_key,
        });

    EXPECT_FALSE(program.valid());
}

TEST(RenderProgramAssetModule, CustomKeyIncludesRootConstantSemantic)
{
    using namespace wz::engine::assets;

    CustomRenderProgramDesc a;
    a.name = "custom/key_semantic";
    a.root_constants.push_back(RootConstantBinding{
        .visibility = ShaderVisibility::All,
        .shader_register = 0,
        .register_space = 2,
        .value_count = 16,
        .semantic = "world",
    });

    CustomRenderProgramDesc b = a;
    b.root_constants[0].semantic = "object_constants";

    const wz::asset::AssetKey key_a = make_custom_render_program_key(a);
    const wz::asset::AssetKey key_b = make_custom_render_program_key(b);

    EXPECT_FALSE(key_a == key_b);
}

// ── Full resolution test (GPU + real shader compilation required) ─────────────

TEST_F(RenderProgramGpuFixture, ResolvesBuiltinGaussianSplatDebug)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shaders = assets.shaders().create_shader_pair({
        .name        = "stub/splat_debug",
        .vertex_path = "shaders/stub/stub_vs.hlsl",
        .pixel_path  = "shaders/stub/stub_ps.hlsl",
        });

    ASSERT_TRUE(shaders.valid());

    const auto program = assets.render_programs().create_builtin({
        .name          = "program/gaussian_splat_debug",
        .program       = BuiltinRenderProgram::GaussianSplatDebug,
        .vertex_shader = shaders.vertex_shader,
        .pixel_shader  = shaders.pixel_shader,
        });

    ASSERT_TRUE(program.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    if (!report.ok()) {
        ADD_FAILURE() << "resolve_all failed with "
                      << report.failures.size() << " failure(s)";
        for (const auto& f : report.failures)
            ADD_FAILURE() << "  error=" << static_cast<int>(f.error);
    }
    ASSERT_TRUE(report.ok());

    const auto handle = assets.render_programs().get_render_program(program);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, kAssetTypeRenderProgram);

    const auto* data = assets.render_programs().get_render_program_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->builtin_program, BuiltinRenderProgram::GaussianSplatDebug);
    EXPECT_EQ(data->binding_model,   RenderBindingModel::SplatVertexInstanced);
    EXPECT_EQ(data->topology,        RenderPrimitiveTopology::TriangleStrip);
    EXPECT_EQ(data->default_domain,  RenderDomain::Splat);
    EXPECT_TRUE(data->vertex_shader.valid());
    EXPECT_TRUE(data->pixel_shader.valid());

    // Declarative pipeline state.
    EXPECT_EQ(data->input_layout, InputLayoutKind::GaussianSplatVertex);
    EXPECT_EQ(data->blend_mode,   BlendMode::AlphaBlend);
    EXPECT_EQ(data->depth_mode,   DepthMode::Disabled);
    EXPECT_EQ(data->raster_mode,  RasterMode::SolidCullNone);

    ASSERT_EQ(data->root_constants.size(), 1u);
    EXPECT_EQ(data->root_constants[0].visibility,      ShaderVisibility::Vertex);
    EXPECT_EQ(data->root_constants[0].shader_register, 0u);
    EXPECT_EQ(data->root_constants[0].register_space,  0u);
    EXPECT_EQ(data->root_constants[0].value_count,     36u);

    EXPECT_TRUE(data->descriptor_bindings.empty());

    wz::engine::rendering::RenderProgramPipelineCache pipeline_cache;
    EXPECT_FALSE(pipeline_cache.get(handle).valid());  // not yet realized
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));
    EXPECT_TRUE(pipeline_cache.get(handle).valid());
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));  // idempotent
}

TEST_F(RenderProgramGpuFixture, ResolvesBuiltinGaussianSplatPullDebug)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    // Stub shaders — DX12 does not enforce that shaders consume every declared
    // root parameter, so this exercises the descriptor-table root-sig path
    // without needing a real pull shader.
    const auto shaders = assets.shaders().create_shader_pair({
        .name        = "stub/splat_pull_debug",
        .vertex_path = "shaders/stub/stub_vs.hlsl",
        .pixel_path  = "shaders/stub/stub_ps.hlsl",
        });

    ASSERT_TRUE(shaders.valid());

    const auto program = assets.render_programs().create_builtin({
        .name          = "program/gaussian_splat_pull_debug",
        .program       = BuiltinRenderProgram::GaussianSplatPullDebug,
        .vertex_shader = shaders.vertex_shader,
        .pixel_shader  = shaders.pixel_shader,
        });

    ASSERT_TRUE(program.valid());
    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    ASSERT_TRUE(report.ok());

    const auto handle = assets.render_programs().get_render_program(program);
    ASSERT_TRUE(handle.valid());

    const auto* data = assets.render_programs().get_render_program_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->builtin_program, BuiltinRenderProgram::GaussianSplatPullDebug);
    EXPECT_EQ(data->binding_model,   RenderBindingModel::SplatPull);
    EXPECT_EQ(data->topology,        RenderPrimitiveTopology::TriangleStrip);
    EXPECT_EQ(data->default_domain,  RenderDomain::Splat);

    // Declarative pipeline state.
    EXPECT_EQ(data->input_layout, InputLayoutKind::None);
    EXPECT_EQ(data->blend_mode,   BlendMode::AlphaBlend);
    EXPECT_EQ(data->depth_mode,   DepthMode::Disabled);
    EXPECT_EQ(data->raster_mode,  RasterMode::SolidCullNone);

    // Root constant at b0, VS-only, 36 values.
    ASSERT_EQ(data->root_constants.size(), 1u);
    EXPECT_EQ(data->root_constants[0].visibility,      ShaderVisibility::Vertex);
    EXPECT_EQ(data->root_constants[0].shader_register, 0u);
    EXPECT_EQ(data->root_constants[0].register_space,  0u);
    EXPECT_EQ(data->root_constants[0].value_count,     36u);

    // Two SRV descriptor bindings: t0 = SplatCloud, t1 = SortedSplatIndices.
    ASSERT_EQ(data->descriptor_bindings.size(), 2u);

    EXPECT_EQ(data->descriptor_bindings[0].kind,             DescriptorKind::StructuredBufferSRV);
    EXPECT_EQ(data->descriptor_bindings[0].visibility,       ShaderVisibility::Vertex);
    EXPECT_EQ(data->descriptor_bindings[0].semantic,         DescriptorSemantic::SplatCloud);
    EXPECT_EQ(data->descriptor_bindings[0].shader_register,  0u);  // t0
    EXPECT_EQ(data->descriptor_bindings[0].register_space,   0u);
    EXPECT_EQ(data->descriptor_bindings[0].descriptor_count, 1u);

    EXPECT_EQ(data->descriptor_bindings[1].kind,             DescriptorKind::StructuredBufferSRV);
    EXPECT_EQ(data->descriptor_bindings[1].visibility,       ShaderVisibility::Vertex);
    EXPECT_EQ(data->descriptor_bindings[1].semantic,         DescriptorSemantic::SortedSplatIndices);
    EXPECT_EQ(data->descriptor_bindings[1].shader_register,  1u);  // t1
    EXPECT_EQ(data->descriptor_bindings[1].register_space,   0u);
    EXPECT_EQ(data->descriptor_bindings[1].descriptor_count, 1u);

    // Verify that the descriptor-table root-sig path and PSO creation both succeed.
    wz::engine::rendering::RenderProgramPipelineCache pipeline_cache;
    EXPECT_FALSE(pipeline_cache.get(handle).valid());
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));
    EXPECT_TRUE(pipeline_cache.get(handle).valid());
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));  // idempotent

    // Verify the realized binding layout has the expected root parameter shape:
    //   root_constants[0] → param 0, b0, 36 values
    //   desc_tables[0]    → param 1, heap slot 0, 2 descriptors (t0+t1 same table)
    const auto* layout = pipeline_cache.get_binding_layout(handle);
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->root_constants.size(), 1u);
    EXPECT_EQ(layout->root_constants[0].root_parameter_index, 0u);
    EXPECT_EQ(layout->root_constants[0].value_count,          36u);

    ASSERT_EQ(layout->desc_tables.size(), 1u);
    EXPECT_EQ(layout->desc_tables[0].root_parameter_index, 1u);
    EXPECT_EQ(layout->desc_tables[0].heap_start_slot,      0u);
    EXPECT_EQ(layout->desc_tables[0].slot_count,           2u);

    ASSERT_EQ(layout->descriptors.size(), 2u);
    EXPECT_EQ(layout->descriptors[0].semantic,                DescriptorSemantic::SplatCloud);
    EXPECT_EQ(layout->descriptors[0].root_parameter_index,    1u);
    EXPECT_EQ(layout->descriptors[0].descriptor_table_offset, 0u);
    EXPECT_EQ(layout->descriptors[1].semantic,                DescriptorSemantic::SortedSplatIndices);
    EXPECT_EQ(layout->descriptors[1].root_parameter_index,    1u);
    EXPECT_EQ(layout->descriptors[1].descriptor_table_offset, 1u);
}

TEST_F(RenderProgramGpuFixture, ResolvesCustomMeshSurface)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shaders = assets.shaders().create_shader_pair({
        .name        = "stub/custom_mesh_surface",
        .vertex_path = "shaders/stub/stub_vs.hlsl",
        .pixel_path  = "shaders/stub/stub_ps.hlsl",
        });

    ASSERT_TRUE(shaders.valid());

    const auto program = assets.render_programs().create_custom({
        .name          = "custom/mesh_surface",
        .vertex_shader = shaders.vertex_shader,
        .pixel_shader  = shaders.pixel_shader,
        .binding_model = RenderBindingModel::MeshIA,
        .topology      = RenderPrimitiveTopology::TriangleList,
        .default_domain     = RenderDomain::Opaque,
        .default_policy_flags = RenderPolicy_DepthTest | RenderPolicy_DepthWrite,
        .input_layout  = InputLayoutKind::MeshPositionNormalUV,
        .blend_mode    = BlendMode::Opaque,
        .depth_mode    = DepthMode::TestWrite,
        .raster_mode   = RasterMode::SolidCullNone,
        .root_constants = {{
            .visibility      = ShaderVisibility::All,
            .shader_register = 0,
            .register_space  = 0,
            .value_count     = 40,
        }},
        });

    ASSERT_TRUE(program.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    if (!report.ok()) {
        ADD_FAILURE() << "resolve_all failed with "
                      << report.failures.size() << " failure(s)";
        for (const auto& f : report.failures)
            ADD_FAILURE() << "  error=" << static_cast<int>(f.error);
    }
    ASSERT_TRUE(report.ok());

    const auto handle = assets.render_programs().get_render_program(program);
    ASSERT_TRUE(handle.valid());
    EXPECT_EQ(handle.type, kAssetTypeRenderProgram);

    const auto* data = assets.render_programs().get_render_program_data(handle);
    ASSERT_NE(data, nullptr);

    EXPECT_EQ(data->builtin_program, BuiltinRenderProgram{});
    EXPECT_EQ(data->binding_model,   RenderBindingModel::MeshIA);
    EXPECT_EQ(data->topology,        RenderPrimitiveTopology::TriangleList);
    EXPECT_EQ(data->default_domain,  RenderDomain::Opaque);
    EXPECT_NE(data->default_policy_flags & RenderPolicy_DepthTest, 0u);
    EXPECT_NE(data->default_policy_flags & RenderPolicy_DepthWrite, 0u);

    EXPECT_TRUE(data->vertex_shader.valid());
    EXPECT_TRUE(data->pixel_shader.valid());

    EXPECT_EQ(data->input_layout, InputLayoutKind::MeshPositionNormalUV);
    EXPECT_EQ(data->blend_mode,   BlendMode::Opaque);
    EXPECT_EQ(data->depth_mode,   DepthMode::TestWrite);
    EXPECT_EQ(data->raster_mode,  RasterMode::SolidCullNone);

    ASSERT_EQ(data->root_constants.size(), 1u);
    EXPECT_EQ(data->root_constants[0].visibility,      ShaderVisibility::All);
    EXPECT_EQ(data->root_constants[0].shader_register, 0u);
    EXPECT_EQ(data->root_constants[0].register_space,  0u);
    EXPECT_EQ(data->root_constants[0].value_count,     40u);

    EXPECT_TRUE(data->descriptor_bindings.empty());

    wz::engine::rendering::RenderProgramPipelineCache pipeline_cache;
    EXPECT_FALSE(pipeline_cache.get(handle).valid());
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));
    EXPECT_TRUE(pipeline_cache.get(handle).valid());
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));
}

// ── Every builtin must be convertible to the rhi contract ────────────────
//
// A builtin render program reaches the rhi path through the renderer's
// render-time bridge, not through compiler publication -- the compiler only
// publishes CUSTOM programs (publish_custom_rhi_render_program). So the ONE
// thing that decides whether a builtin can render on rhi at all is whether
// to_rhi_render_program_desc accepts it, and a refusal there is terminal:
// the render-time path calls the same converter over the same data and fails
// identically, one frame later, with a generic message.
//
// Measured before this test existed: ALL 14 builtins were refused, every one
// for the same reason -- a root-constant block with no semantic. That is
// #317's D1-C11 open half, and it means no builtin could render through rhi.
// This is the census that keeps it closed: a new builtin that forgets its
// semantic fails HERE, naming itself, rather than silently declining to draw.
TEST_F(RenderProgramGpuFixture, EveryBuiltinConvertsToTheRhiContract)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shaders = assets.shaders().create_shader_pair({
        .name        = "stub/builtin_census",
        .vertex_path = "shaders/stub/stub_vs.hlsl",
        .pixel_path  = "shaders/stub/stub_ps.hlsl",
        });
    ASSERT_TRUE(shaders.valid());

    struct Made { BuiltinRenderProgram which; RenderProgramAsset asset; };
    std::vector<Made> made;
    for (size_t i = 0; i < kBuiltinRenderProgramCount; ++i) {
        const auto which = static_cast<BuiltinRenderProgram>(i);
        const auto program = assets.render_programs().create_builtin({
            .name          = "census/" + std::to_string(i),
            .program       = which,
            .vertex_shader = shaders.vertex_shader,
            .pixel_shader  = shaders.pixel_shader,
            });
        if (program.valid()) {
            made.push_back({ which, program });
        }
    }
    ASSERT_FALSE(made.empty());
    ASSERT_TRUE(assets.commit());
    (void)assets.resolve_all();

    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry   constants;

    size_t convertible = 0;
    size_t considered  = 0;
    for (const auto& m : made) {
        const auto handle =
            assets.render_programs().get_render_program(m.asset);
        if (!handle.valid()) {
            continue;  // the builtin declines to define itself (fill == false)
        }
        const auto* data =
            assets.render_programs().get_render_program_data(handle);
        if (!data) {
            continue;
        }
        ++considered;

        const auto desc = wz::engine::rendering::to_rhi_render_program_desc(
            *data,
            m.asset.key,
            shaders.vertex_shader,
            shaders.pixel_shader,
            descriptors,
            constants);

        EXPECT_TRUE(desc.has_value())
            << "builtin #" << static_cast<int>(m.which)
            << " cannot render on the rhi path — "
            << wz::engine::rendering::render_program_bridge_refusal(
                   data->root_constants, data->descriptor_bindings);
        if (desc) {
            ++convertible;
        }
    }

    // Non-vacuity: a glob/enum change that stopped producing programs would
    // otherwise make this pass green having checked nothing. Pinned to the
    // enum rather than a hand-picked floor, so it cannot rot the way the old
    // `> 8` did when the nine MeshIA builtins were deleted -- 6 of 7 members
    // still cleared it silently. TerrainSurfelSurface is the one subtracted:
    // its preset deliberately returns false, so no program is ever built.
    ASSERT_EQ(
        considered,
        wz::engine::assets::kBuiltinRenderProgramCount - 1u)
        << "the census stopped seeing builtins";
    EXPECT_EQ(convertible, considered);
}

// ── The render-time fallback, end to end ─────────────────────────────────
//
// The census above proves a builtin CONVERTS. This proves the renderer
// actually takes it through the path it converts for.
//
// A builtin never gets an rhi program from the compiler: publication is the
// CUSTOM path only (publish_custom_rhi_render_program), and the builtin's
// route is RhiSceneRenderer::realize_program's find-then-fallback arm, which
// bridges at render time and registers the result. Nothing tested that arm
// with a builtin, which is how all 14 stayed unrenderable on rhi without a
// red test anywhere (#317 D1-C11).
//
// SkySurface is the builtin to drive it with: Fullscreen -> VertexSource::None,
// so the pipeline needs no vertex input and does not meet the InputAssembler
// refusal that the other nine mesh builtins would (#317 D1-Q1). What is under
// test is the BRIDGE, not the draw -- so the assertions are that the fallback
// ran exactly once and that it produced a registered program, which only
// happens after to_rhi_render_program_desc has accepted the builtin.
TEST_F(RenderProgramGpuFixture, RendererBridgesABuiltinAtRenderTime)
{
    using namespace wz::engine::assets;

    wz::engine::rendering::EngineGpuContext gpu(device);
    EngineAssetLibrary assets(gpu, logger, resources.wz_root());

    const auto shaders = assets.shaders().create_shader_pair({
        .name        = "stub/sky_surface",
        .vertex_path = "shaders/stub/stub_vs.hlsl",
        .pixel_path  = "shaders/stub/stub_ps.hlsl",
        });
    ASSERT_TRUE(shaders.valid());

    const auto program = assets.render_programs().create_builtin({
        .name          = "program/sky_surface",
        .program       = BuiltinRenderProgram::SkySurface,
        .vertex_shader = shaders.vertex_shader,
        .pixel_shader  = shaders.pixel_shader,
        });
    ASSERT_TRUE(program.valid());

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "mesh/sky_carrier",
        .kind = ProceduralMeshKind::Cube,
        });
    ASSERT_TRUE(mesh.valid());

    const auto renderable = assets.renderables().create_rhi_pull_mesh({
        .name    = "renderable/sky_surface",
        .mesh    = mesh,
        .program = program,
        });
    ASSERT_TRUE(renderable.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);

    // The compiler published nothing for a builtin -- that is the premise.
    const std::size_t before = renderer.registered_program_count();
    EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u);

    SceneNodeAsset node{};
    node.id = wz::scene::AuthoredEntityId{ "sky" };
    node.name = "sky";
    node.visible = true;
    node.renderable_asset = renderable.output;
    const std::vector<SceneNodeAsset> nodes{ node };

    ASSERT_TRUE(wz::gpu::begin_frame(device));
    wz::gpu::clear(device, 0.0f, 0.0f, 0.0f, 1.0f);
    (void)renderer.render_scene(
        nodes, assets, wz::math::Mat4::identity(), wz::math::Vec3{});
    ASSERT_TRUE(wz::gpu::end_frame(device));
    wz::gpu::present(device, /*sync_interval*/ 0);

    // The fallback ran, exactly once, because the builtin had no published
    // program to find.
    EXPECT_EQ(renderer.render_time_program_bridge_count(), 1u);

    // And it SUCCEEDED. register_program happens only after the conversion is
    // accepted, so a refused builtin leaves this flat -- which is what it did
    // for all 14 before their root-constant blocks were named.
    EXPECT_GT(renderer.registered_program_count(), before)
        << "the render-time bridge refused the builtin, so it cannot render "
           "on the rhi path at all";
}
