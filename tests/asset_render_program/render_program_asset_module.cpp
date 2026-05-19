#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/render_program_pipeline_cache.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <logging/logger.h>
#include <window/window2.h>

#include <filesystem>
#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

    ASSERT_EQ(data->bindings.size(), 1u);
    EXPECT_EQ(data->bindings[0].kind,            ShaderBindingKind::RootConstants);
    EXPECT_EQ(data->bindings[0].visibility,      ShaderVisibility::Vertex);
    EXPECT_EQ(data->bindings[0].shader_register, 0u);
    EXPECT_EQ(data->bindings[0].register_space,  0u);
    EXPECT_EQ(data->bindings[0].count,           36u);

    wz::engine::rendering::RenderProgramPipelineCache pipeline_cache;
    EXPECT_FALSE(pipeline_cache.get(handle).valid());  // not yet realized
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));
    EXPECT_TRUE(pipeline_cache.get(handle).valid());
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));  // idempotent
}

TEST_F(RenderProgramGpuFixture, ResolvesBuiltinMeshWireframeDebug)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shaders = assets.shaders().create_shader_pair({
        .name = "stub/mesh_wireframe",
        .vertex_path = "shaders/stub/stub_vs.hlsl",
        .pixel_path = "shaders/stub/stub_ps.hlsl",
        });

    ASSERT_TRUE(shaders.valid());

    const auto program = assets.render_programs().create_builtin({
        .name = "program/mesh_wireframe_debug",
        .program = BuiltinRenderProgram::MeshWireframeDebug,
        .vertex_shader = shaders.vertex_shader,
        .pixel_shader = shaders.pixel_shader,
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

    EXPECT_EQ(data->builtin_program, BuiltinRenderProgram::MeshWireframeDebug);
    EXPECT_EQ(data->binding_model, RenderBindingModel::MeshIA);
    EXPECT_EQ(data->topology, RenderPrimitiveTopology::TriangleList);
    EXPECT_EQ(data->default_domain, RenderDomain::Debug);

    EXPECT_NE(data->default_policy_flags & RenderPolicy_Wireframe, 0u);
    EXPECT_NE(data->default_policy_flags & RenderPolicy_DepthTest, 0u);
    EXPECT_NE(data->default_policy_flags & RenderPolicy_DepthWrite, 0u);

    EXPECT_TRUE(data->vertex_shader.valid());
    EXPECT_TRUE(data->pixel_shader.valid());

    ASSERT_EQ(data->bindings.size(), 1u);
    EXPECT_EQ(data->bindings[0].kind,            ShaderBindingKind::RootConstants);
    EXPECT_EQ(data->bindings[0].visibility,      ShaderVisibility::All);
    EXPECT_EQ(data->bindings[0].shader_register, 0u);
    EXPECT_EQ(data->bindings[0].register_space,  0u);
    EXPECT_EQ(data->bindings[0].count,           32u);

    wz::engine::rendering::RenderProgramPipelineCache pipeline_cache;
    EXPECT_FALSE(pipeline_cache.get(handle).valid());  // not yet realized
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));
    EXPECT_TRUE(pipeline_cache.get(handle).valid());
    EXPECT_TRUE(pipeline_cache.realize(device, assets.render_programs().table(), handle));  // idempotent
}