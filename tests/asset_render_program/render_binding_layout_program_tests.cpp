// Issue #227 seam test: express binding_layout preset 2 (clipmap landscape)
// as a render-binding-layout ASSET wired into a custom render program (0x103),
// and assert the produced RenderProgramData and rhi RenderProgramDesc are
// identical to the numbered preset-2 path.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/render_program.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/rhi_render_program_bridge.h>

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
    using namespace wz::engine::assets;

    struct TempShaderDir
    {
        fs::path root;

        TempShaderDir()
        {
            root = fs::temp_directory_path() /
                ("wozzits_binding_layout_program_tests_" +
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

    struct BindingLayoutProgramFixture : public ::testing::Test
    {
        wz::Logger               logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device          device{};
        TempShaderDir            resources;

        void SetUp() override
        {
            resources.write_stub_shaders();

            wz::window::WindowDesc desc{};
            desc.title     = "binding_layout_program_test";
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

    // The clipmap SRG (preset 2) expressed as an authored layout.
    RenderBindingLayoutData clipmap_layout()
    {
        RenderBindingLayoutData layout{};
        layout.constants_semantic = "clipmap";
        layout.constants_visibility = ShaderVisibility::All;
        layout.constants_head = RenderBindingConstantsHead::CameraSnappedTerrain;
        layout.bindings = {
            {
                .semantic = "pulled_mesh_positions",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::Vertex,
            },
            {
                .semantic = "pulled_mesh_indices",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::Vertex,
            },
            {
                .semantic = "scalar_field_texture",
                .kind = RenderBindingKind::TextureSrv,
                .visibility = ShaderVisibility::Vertex,
            },
        };
        layout.samplers = {
            {
                .kind = StaticSamplerKind::LinearClamp,
                .visibility = ShaderVisibility::Vertex,
            },
        };
        return layout;
    }

    void expect_same_program_data(
        const RenderProgramData& a,
        const RenderProgramData& b)
    {
        EXPECT_EQ(a.binding_model, b.binding_model);
        EXPECT_EQ(a.topology, b.topology);
        EXPECT_EQ(a.default_domain, b.default_domain);
        EXPECT_EQ(a.default_policy_flags, b.default_policy_flags);
        EXPECT_EQ(a.input_layout, b.input_layout);
        EXPECT_EQ(a.blend_mode, b.blend_mode);
        EXPECT_EQ(a.depth_mode, b.depth_mode);
        EXPECT_EQ(a.raster_mode, b.raster_mode);

        ASSERT_EQ(a.root_constants.size(), b.root_constants.size());
        for (size_t i = 0; i < a.root_constants.size(); ++i) {
            EXPECT_EQ(
                a.root_constants[i].visibility,
                b.root_constants[i].visibility);
            EXPECT_EQ(
                a.root_constants[i].shader_register,
                b.root_constants[i].shader_register);
            EXPECT_EQ(
                a.root_constants[i].register_space,
                b.root_constants[i].register_space);
            EXPECT_EQ(
                a.root_constants[i].value_count,
                b.root_constants[i].value_count);
            EXPECT_EQ(
                a.root_constants[i].semantic,
                b.root_constants[i].semantic);
        }

        ASSERT_EQ(a.descriptor_bindings.size(), b.descriptor_bindings.size());
        for (size_t i = 0; i < a.descriptor_bindings.size(); ++i) {
            EXPECT_EQ(
                a.descriptor_bindings[i].kind,
                b.descriptor_bindings[i].kind);
            EXPECT_EQ(
                a.descriptor_bindings[i].visibility,
                b.descriptor_bindings[i].visibility);
            EXPECT_EQ(
                a.descriptor_bindings[i].semantic,
                b.descriptor_bindings[i].semantic);
            EXPECT_EQ(
                a.descriptor_bindings[i].shader_register,
                b.descriptor_bindings[i].shader_register);
            EXPECT_EQ(
                a.descriptor_bindings[i].register_space,
                b.descriptor_bindings[i].register_space);
            EXPECT_EQ(
                a.descriptor_bindings[i].descriptor_count,
                b.descriptor_bindings[i].descriptor_count);
        }

        ASSERT_EQ(a.static_samplers.size(), b.static_samplers.size());
        for (size_t i = 0; i < a.static_samplers.size(); ++i) {
            EXPECT_EQ(a.static_samplers[i].kind, b.static_samplers[i].kind);
            EXPECT_EQ(
                a.static_samplers[i].visibility,
                b.static_samplers[i].visibility);
            EXPECT_EQ(
                a.static_samplers[i].shader_register,
                b.static_samplers[i].shader_register);
            EXPECT_EQ(
                a.static_samplers[i].register_space,
                b.static_samplers[i].register_space);
        }
    }
}

// Key folding (0x706 pattern): the optional layout dep changes deps_hash;
// an empty layout key leaves the base 2-dep key untouched.
TEST(RenderBindingLayoutProgramKey, LayoutDepFoldsIntoCustomProgramKey)
{
    CustomRenderProgramDesc desc;
    desc.name = "custom/layout_key";
    desc.vertex_shader = wz::asset::AssetKey{
        .content_hash = { 1, 0 },
        .schema_hash = { 1, 0 },
        .compiler_hash = { 1, 0 },
        .deps_hash = { 0, 0 },
    };
    desc.pixel_shader = wz::asset::AssetKey{
        .content_hash = { 2, 0 },
        .schema_hash = { 2, 0 },
        .compiler_hash = { 2, 0 },
        .deps_hash = { 0, 0 },
    };

    const wz::asset::AssetKey without_layout =
        make_custom_render_program_key(desc);

    desc.binding_layout = wz::asset::AssetKey{
        .content_hash = { 3, 0 },
        .schema_hash = { 3, 0 },
        .compiler_hash = { 3, 0 },
        .deps_hash = { 0, 0 },
    };
    const wz::asset::AssetKey with_layout =
        make_custom_render_program_key(desc);

    EXPECT_FALSE(without_layout == with_layout);
    EXPECT_TRUE(without_layout.content_hash == with_layout.content_hash);

    desc.binding_layout = {};
    EXPECT_TRUE(without_layout == make_custom_render_program_key(desc));
}

TEST_F(BindingLayoutProgramFixture, LayoutProgramMatchesPreset2)
{
    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shaders = assets.shaders().create_shader_pair({
        .name        = "stub/binding_layout_parity",
        .vertex_path = "shaders/stub/stub_vs.hlsl",
        .pixel_path  = "shaders/stub/stub_ps.hlsl",
        });
    ASSERT_TRUE(shaders.valid());

    // Program A — the numbered preset-2 path, compiled from a ParamBlock
    // exactly as a graph-authored 0x103 node is (the params branch builds the
    // clipmap SRG in custom_render_program_desc_from_params).
    wz::asset::AssetKey preset_key{
        .content_hash  = { 0xC0FFEE, 1 },
        .schema_hash   = { 0xC0FFEE, 2 },
        .compiler_hash = { 0xC0FFEE, 3 },
        .deps_hash     = { 0, 0 },
    };
    {
        wz::asset::ParamBlock params;
        params.values["name"] = std::string("program/preset2");
        params.values["binding_model"] = static_cast<int64_t>(
            RenderBindingModel::MeshVertexPull);
        params.values["topology"] = static_cast<int64_t>(
            RenderPrimitiveTopology::TriangleList);
        params.values["default_domain"] = static_cast<int64_t>(
            RenderDomain::Opaque);
        params.values["default_policy_flags"] = static_cast<int64_t>(
            RenderPolicy_DepthTest | RenderPolicy_DepthWrite);
        params.values["input_layout"] = static_cast<int64_t>(
            InputLayoutKind::None);
        params.values["blend_mode"] = static_cast<int64_t>(BlendMode::Opaque);
        params.values["depth_mode"] = static_cast<int64_t>(
            DepthMode::TestWrite);
        params.values["raster_mode"] = static_cast<int64_t>(
            RasterMode::SolidCullBack);
        params.values["binding_layout"] = int64_t{ 2 };  // clipmap preset

        wz::asset::AssetNode node{};
        node.key = preset_key;
        node.type = kAssetTypeRenderProgram;
        node.schema = kCustomRenderProgramSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = std::move(params);

        ASSERT_TRUE(assets.system().register_asset(
            std::move(node),
            { shaders.vertex_shader, shaders.pixel_shader }));
    }

    // Program B — the same SRG authored as a render-binding-layout asset
    // wired into the program through the optional binding_layout port.
    const auto layout =
        assets.render_binding_layouts().create_render_binding_layout({
            .name = "layout/clipmap",
            .layout = clipmap_layout(),
        });
    ASSERT_TRUE(layout.valid());

    const auto layout_program = assets.render_programs().create_custom({
        .name = "program/clipmap_via_layout",
        .vertex_shader = shaders.vertex_shader,
        .pixel_shader = shaders.pixel_shader,
        .binding_layout = layout.output,
        .binding_model = RenderBindingModel::MeshVertexPull,
        .topology = RenderPrimitiveTopology::TriangleList,
        .default_domain = RenderDomain::Opaque,
        .default_policy_flags = RenderPolicy_DepthTest | RenderPolicy_DepthWrite,
        .input_layout = InputLayoutKind::None,
        .blend_mode = BlendMode::Opaque,
        .depth_mode = DepthMode::TestWrite,
        .raster_mode = RasterMode::SolidCullBack,
        });
    ASSERT_TRUE(layout_program.valid());

    ASSERT_TRUE(assets.commit());

    const auto report = assets.resolve_all();
    if (!report.ok()) {
        for (const auto& f : report.failures)
            ADD_FAILURE() << "resolve failure error="
                          << static_cast<int>(f.error) << " " << f.detail;
    }
    ASSERT_TRUE(report.ok());

    const auto* preset_compiled = assets.system().find_compiled(preset_key);
    ASSERT_NE(preset_compiled, nullptr);
    const RenderProgramData* preset_data =
        assets.render_programs().get_render_program_data(
            preset_compiled->handle);
    ASSERT_NE(preset_data, nullptr);

    const auto layout_handle =
        assets.render_programs().get_render_program(layout_program);
    ASSERT_TRUE(layout_handle.valid());
    const RenderProgramData* layout_data =
        assets.render_programs().get_render_program_data(layout_handle);
    ASSERT_NE(layout_data, nullptr);

    // The layout-built program is byte-for-byte the preset-2 program.
    expect_same_program_data(*preset_data, *layout_data);

    // Lock the absolute preset-2 shape too (not just A == B): one 32-dword
    // "clipmap" block at b0 space2, t0/t1/t2, and the LinearClamp s0 — the
    // sampler that the pre-#227 compiler DROPPED from RenderProgramData.
    ASSERT_EQ(layout_data->root_constants.size(), 1u);
    EXPECT_EQ(layout_data->root_constants[0].value_count, 32u);
    EXPECT_EQ(layout_data->root_constants[0].semantic, "clipmap");
    EXPECT_EQ(layout_data->root_constants[0].register_space, 2u);
    ASSERT_EQ(layout_data->descriptor_bindings.size(), 3u);
    EXPECT_EQ(
        layout_data->descriptor_bindings[2].semantic,
        DescriptorSemantic::ScalarFieldTexture);
    ASSERT_EQ(layout_data->static_samplers.size(), 1u);
    EXPECT_EQ(
        layout_data->static_samplers[0].kind,
        StaticSamplerKind::LinearClamp);
    EXPECT_EQ(
        layout_data->static_samplers[0].visibility,
        ShaderVisibility::Vertex);

    // rhi registration parity: converting both table entries through the
    // bridge (same keys, same registries) must produce identical
    // RenderProgramDescs — compared structurally via the SRG layout hashes.
    wz::rhi::DescriptorSemanticRegistry descriptor_semantics;
    wz::rhi::ConstantSemanticRegistry constant_semantics;

    const auto rhi_preset =
        wz::engine::rendering::to_rhi_render_program_desc(
            *preset_data,
            preset_key,
            shaders.vertex_shader,
            shaders.pixel_shader,
            descriptor_semantics,
            constant_semantics);
    ASSERT_TRUE(rhi_preset.has_value());

    const auto rhi_layout =
        wz::engine::rendering::to_rhi_render_program_desc(
            *layout_data,
            preset_key,
            shaders.vertex_shader,
            shaders.pixel_shader,
            descriptor_semantics,
            constant_semantics);
    ASSERT_TRUE(rhi_layout.has_value());

    EXPECT_EQ(rhi_preset->vertex_source, rhi_layout->vertex_source);
    EXPECT_EQ(rhi_preset->topology, rhi_layout->topology);
    EXPECT_EQ(rhi_preset->blend_mode, rhi_layout->blend_mode);
    EXPECT_EQ(rhi_preset->depth_mode, rhi_layout->depth_mode);
    EXPECT_EQ(rhi_preset->raster_mode, rhi_layout->raster_mode);
    ASSERT_EQ(
        rhi_preset->shader_resource_groups.size(),
        rhi_layout->shader_resource_groups.size());
    for (size_t i = 0; i < rhi_preset->shader_resource_groups.size(); ++i) {
        EXPECT_EQ(
            rhi_preset->shader_resource_groups[i].hash(),
            rhi_layout->shader_resource_groups[i].hash());
    }
}
