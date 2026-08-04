// Issue #227 seam test: express binding_layout preset 2 (clipmap landscape)
// as a render-binding-layout ASSET wired into a custom render program (0x103),
// and assert the produced RenderProgramData and rhi RenderProgramDesc are
// identical to the numbered preset-2 path.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/key_factories/render_binding_layout.h>
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
#include <map>
#include <string>
#include <utility>
#include <vector>

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

    // The #281 lit-textured SRG, as a typed layout: the WorldViewProjCamera36
    // head, the three mesh-pull rows, the two SG-sky rows, a material_albedo
    // texture, and -- the part hand-authoring got wrong first time -- a
    // pixel-visible LinearClamp sampler, without which the pixel shader's
    // material_sampler at s0 is unbound.
    RenderBindingLayoutData lit_textured_layout()
    {
        RenderBindingLayoutData layout{};
        layout.constants_semantic = "lit";
        layout.constants_visibility = ShaderVisibility::All;
        layout.constants_head =
            RenderBindingConstantsHead::WorldViewProjCamera36;
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
                .semantic = "pulled_mesh_normals",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::Vertex,
            },
            {
                .semantic = "sky_gaussian",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::All,
            },
            {
                .semantic = "sky_gaussian_points",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::All,
            },
            {
                .semantic = "material_albedo",
                .kind = RenderBindingKind::TextureSrv,
                .visibility = ShaderVisibility::Pixel,
            },
        };
        layout.samplers = {
            {
                .kind = StaticSamplerKind::LinearClamp,
                .visibility = ShaderVisibility::Pixel,
            },
        };
        return layout;
    }

    // sg_lit_uv: the lit-textured SRG plus the mesh's own UV pull stream. The
    // shader (sg_lit_uv_vs.hlsl) appends uvs at t6 so t0..t5 stay identical to
    // sg_lit_textured; the layout must produce the same. Authored LAST here, but
    // the register is a property of the canonical semantic order, not row order.
    RenderBindingLayoutData lit_uv_layout()
    {
        RenderBindingLayoutData layout = lit_textured_layout();
        layout.bindings.push_back({
            .semantic = "pulled_mesh_uvs",
            .kind = RenderBindingKind::StructuredSrv,
            .visibility = ShaderVisibility::Vertex,
        });
        return layout;
    }

    // sky_gaussian: the SG-sky program's SRG -- mesh pull (positions, indices)
    // then the two sky rows, no normals and no material. The two sky rows are the
    // same semantic as sg_lit's but land at t2/t3 here (dense per program), which
    // is the point of asserting the built register rather than a fixed one.
    RenderBindingLayoutData sky_gaussian_layout()
    {
        RenderBindingLayoutData layout{};
        layout.constants_semantic = "sky";
        layout.constants_visibility = ShaderVisibility::All;
        layout.constants_head =
            RenderBindingConstantsHead::WorldViewProjCamera36;
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
                .semantic = "sky_gaussian",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::All,
            },
            {
                .semantic = "sky_gaussian_points",
                .kind = RenderBindingKind::StructuredSrv,
                .visibility = ShaderVisibility::All,
            },
        };
        return layout;
    }

    // The SAME layout as the indexed scalar params a graph node carries --
    // transcribed from the node #281 hand-wrote into test_mesh_001. Note the
    // sampler kind is offset by one in the authored enum (0 = no sampler), so
    // "Linear clamp" is 1 here and StaticSamplerKind::LinearClamp above.
    wz::asset::ParamBlock lit_textured_layout_params()
    {
        wz::asset::ParamBlock params;
        params.values["name"] = std::string("lit_textured_layout");
        params.values["constants_semantic"] = std::string("lit");
        params.values["constants_visibility"] =
            static_cast<int64_t>(ShaderVisibility::All);
        params.values["constants_head"] = static_cast<int64_t>(
            RenderBindingConstantsHead::WorldViewProjCamera36);
        params.values["constants_dwords"] = int64_t{ 0 };

        const struct { const char* semantic; int64_t kind; int64_t vis; }
        rows[] = {
            { "pulled_mesh_positions", 1, 1 },
            { "pulled_mesh_indices",   1, 1 },
            { "pulled_mesh_normals",   1, 1 },
            { "sky_gaussian",          1, 0 },
            { "sky_gaussian_points",   1, 0 },
            { "material_albedo",       0, 2 },
        };
        for (int i = 0; i < 6; ++i) {
            const std::string n = std::to_string(i);
            params.values["binding" + n + "_semantic"] =
                std::string(rows[i].semantic);
            params.values["binding" + n + "_kind"] = rows[i].kind;
            params.values["binding" + n + "_visibility"] = rows[i].vis;
        }

        params.values["sampler0_kind"] = int64_t{ 1 };        // Linear clamp
        params.values["sampler0_visibility"] =
            static_cast<int64_t>(ShaderVisibility::Pixel);
        return params;
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
        params.values["blend_mode"] = static_cast<int64_t>(wz::rhi::BlendMode::Opaque);
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
        .blend_mode = wz::rhi::BlendMode::Opaque,
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

// Issue #286: a LIT program -- one carrying a RenderBindingConstantsHead --
// built entirely through the typed API, and proven equal to the same thing
// authored as graph params. #281 wired its composite-material sphere by
// hand-writing nodes into test_mesh_001/assets.graph.json because nothing in
// code could construct this arrangement; this is that arrangement in code, and
// the test #281 had no way to write.
TEST_F(BindingLayoutProgramFixture, LitProgramBuildsTypedAndMatchesAuthoredParams)
{
    EngineAssetLibrary assets(device, logger, resources.wz_root());

    const auto shaders = assets.shaders().create_shader_pair({
        .name        = "stub/lit_textured",
        .vertex_path = "shaders/stub/stub_vs.hlsl",
        .pixel_path  = "shaders/stub/stub_ps.hlsl",
        });
    ASSERT_TRUE(shaders.valid());

    // Layout A -- the graph form: a 0x104 node whose meta is the indexed scalar
    // ParamBlock the editor writes.
    const wz::asset::AssetKey authored_layout_key{
        .content_hash  = { 0x11701, 1 },
        .schema_hash   = { 0x11701, 2 },
        .compiler_hash = { 0x11701, 3 },
        .deps_hash     = { 0, 0 },
    };
    {
        wz::asset::AssetNode node{};
        node.key = authored_layout_key;
        node.type = kAssetTypeRenderBindingLayout;
        node.schema = kRenderBindingLayoutSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = lit_textured_layout_params();
        ASSERT_TRUE(assets.system().register_asset(std::move(node)));
    }

    // Layout B -- the typed form.
    const auto typed_layout =
        assets.render_binding_layouts().create_render_binding_layout({
            .name = "layout/lit_textured",
            .layout = lit_textured_layout(),
        });
    ASSERT_TRUE(typed_layout.valid());

    const auto authored_program = assets.render_programs().create_custom({
        .name = "program/lit_textured_authored",
        .vertex_shader = shaders.vertex_shader,
        .pixel_shader = shaders.pixel_shader,
        .binding_layout = authored_layout_key,
        .binding_model = RenderBindingModel::MeshVertexPull,
        .topology = RenderPrimitiveTopology::TriangleList,
        .default_domain = RenderDomain::Opaque,
        .default_policy_flags = RenderPolicy_DepthTest | RenderPolicy_DepthWrite,
        .input_layout = InputLayoutKind::None,
        .blend_mode = wz::rhi::BlendMode::Opaque,
        .depth_mode = DepthMode::TestWrite,
        .raster_mode = RasterMode::SolidCullBack,
        });
    ASSERT_TRUE(authored_program.valid());

    const auto typed_program = assets.render_programs().create_custom({
        .name = "program/lit_textured_typed",
        .vertex_shader = shaders.vertex_shader,
        .pixel_shader = shaders.pixel_shader,
        .binding_layout = typed_layout.output,
        .binding_model = RenderBindingModel::MeshVertexPull,
        .topology = RenderPrimitiveTopology::TriangleList,
        .default_domain = RenderDomain::Opaque,
        .default_policy_flags = RenderPolicy_DepthTest | RenderPolicy_DepthWrite,
        .input_layout = InputLayoutKind::None,
        .blend_mode = wz::rhi::BlendMode::Opaque,
        .depth_mode = DepthMode::TestWrite,
        .raster_mode = RasterMode::SolidCullBack,
        });
    ASSERT_TRUE(typed_program.valid());

    ASSERT_TRUE(assets.commit());
    const auto report = assets.resolve_all();
    if (!report.ok()) {
        for (const auto& f : report.failures)
            ADD_FAILURE() << "resolve failure error="
                          << static_cast<int>(f.error) << " " << f.detail;
    }
    ASSERT_TRUE(report.ok());

    const auto authored_handle =
        assets.render_programs().get_render_program(authored_program);
    ASSERT_TRUE(authored_handle.valid());
    const RenderProgramData* authored_data =
        assets.render_programs().get_render_program_data(authored_handle);
    ASSERT_NE(authored_data, nullptr);

    const auto typed_handle =
        assets.render_programs().get_render_program(typed_program);
    ASSERT_TRUE(typed_handle.valid());
    const RenderProgramData* typed_data =
        assets.render_programs().get_render_program_data(typed_handle);
    ASSERT_NE(typed_data, nullptr);

    expect_same_program_data(*authored_data, *typed_data);

    // The absolute shape, so this locks the lit SRG and not merely A == B.
    ASSERT_EQ(typed_data->root_constants.size(), 1u);
    EXPECT_EQ(typed_data->root_constants[0].semantic, "lit");
    EXPECT_EQ(typed_data->root_constants[0].value_count, 36u)
        << "WorldViewProjCamera36 head did not reach the program";
    EXPECT_EQ(typed_data->root_constants[0].register_space, 2u);

    // The constants contract the custom-renderable compiler validates against.
    EXPECT_TRUE(typed_data->has_authored_binding_layout);
    EXPECT_EQ(
        typed_data->constants_head,
        RenderBindingConstantsHead::WorldViewProjCamera36);

    // Six SRV rows in authored order; material_albedo is the texture the
    // composite target binds to, and it must be LAST (t5) -- the rows derive
    // their registers from order, so an inserted row moves it.
    ASSERT_EQ(typed_data->descriptor_bindings.size(), 6u);
    EXPECT_EQ(
        typed_data->descriptor_bindings[5].semantic,
        DescriptorSemantic::MaterialAlbedo);
    EXPECT_EQ(typed_data->descriptor_bindings[5].shader_register, 5u);
    EXPECT_EQ(
        typed_data->descriptor_bindings[5].visibility, ShaderVisibility::Pixel);

    // The sampler #281 had to discover by hand: without it the pixel shader's
    // material_sampler at s0 is unbound and the surface samples nothing.
    ASSERT_EQ(typed_data->static_samplers.size(), 1u);
    EXPECT_EQ(
        typed_data->static_samplers[0].kind, StaticSamplerKind::LinearClamp);
    EXPECT_EQ(
        typed_data->static_samplers[0].visibility, ShaderVisibility::Pixel);
    EXPECT_EQ(typed_data->static_samplers[0].shader_register, 0u);
}

// The layout key must cover the VIEW-frequency block. Without this, two layouts
// differing only in view_head derive the same key, the second create_* is a
// no-op that returns the first, and a program compiles against an SRG its
// shader does not match.
TEST(RenderBindingLayoutKey, ViewHeadChangesTheKey)
{
    RenderBindingLayoutData layout{};
    layout.constants_semantic = "lit";
    layout.constants_head = RenderBindingConstantsHead::WorldViewProjCamera36;

    const wz::asset::AssetKey without_view =
        make_render_binding_layout_key("layout/lit", layout);

    layout.view_head = RenderBindingViewHead::CameraFog;
    const wz::asset::AssetKey with_view =
        make_render_binding_layout_key("layout/lit", layout);

    EXPECT_FALSE(without_view == with_view);

    // A layout wanting no view block keys exactly as it did before view heads
    // existed, so adding this did not repoint any existing typed layout.
    layout.view_head = RenderBindingViewHead::None;
    EXPECT_TRUE(without_view == make_render_binding_layout_key("layout/lit", layout));
}

// #317 D1-T11: the D3DReflect check (D1-C13) compares a shader's bytecode against
// its layout by register + KIND, but it cannot see SEMANTIC identity -- two
// same-kind resources (e.g. two StructuredBuffers) at swapped registers pass it.
// That is the SILENT half of the D1-C20 class: #322's canonical order repointed
// sky_gaussian / sky_gaussian_points / material_albedo / uvs, and only the two
// slots where the KIND also flipped (material_albedo <-> uvs) were caught by
// reflection; t3/t4 were silently mis-bound.
//
// This closes that gap at the source of truth: for each shipping object-SRG
// program it builds the SRG the same way the 0x103 compiler does and asserts
// every semantic lands at the register its shader hand-declares. Expected
// registers are transcribed from the .hlsl named on each case. Revert the D1-C20
// fix (PulledMeshUvs back among the mesh streams) and the sg_lit_uv case fails on
// t3..t6. Device-free -- no GPU, no compiled bytecode; it exercises the pure
// register-assignment derivation.
TEST(RenderBindingLayoutRegisters, EveryShippingLayoutMatchesItsShaderRegisters)
{
    struct Case
    {
        const char* name;
        RenderBindingLayoutData layout;
        // semantic -> the t-register the shader hand-declares in space2.
        std::vector<std::pair<std::string, uint32_t>> expected;
    };

    std::vector<Case> cases;
    // clipmap/clipmap_vs.hlsl:59-61 (positions t0, indices t1, heightTex t2).
    cases.push_back({ "clipmap", clipmap_layout(), {
        { "pulled_mesh_positions", 0u },
        { "pulled_mesh_indices", 1u },
        { "scalar_field_texture", 2u },
    } });
    // sg_lit_textured_vs.hlsl (t0..t2) + sg_lighting_common.hlsl:45-46 (sky t3/t4)
    // + sg_lit_textured_ps.hlsl:13 (material_albedo t5).
    cases.push_back({ "sg_lit_textured", lit_textured_layout(), {
        { "pulled_mesh_positions", 0u },
        { "pulled_mesh_indices", 1u },
        { "pulled_mesh_normals", 2u },
        { "sky_gaussian", 3u },
        { "sky_gaussian_points", 4u },
        { "material_albedo", 5u },
    } });
    // sg_lit_uv_ps.hlsl:15 (material_albedo t5) + sg_lit_uv_vs.hlsl:26 (uvs t6) --
    // the D1-C20 case: uvs MUST sort after material_albedo, not among the mesh
    // streams, or t3..t6 shift out from under the shader.
    cases.push_back({ "sg_lit_uv", lit_uv_layout(), {
        { "pulled_mesh_positions", 0u },
        { "pulled_mesh_indices", 1u },
        { "pulled_mesh_normals", 2u },
        { "sky_gaussian", 3u },
        { "sky_gaussian_points", 4u },
        { "material_albedo", 5u },
        { "pulled_mesh_uvs", 6u },
    } });
    // sky_gaussian_sky_vs.hlsl:25-26 (positions t0, indices t1) +
    // sky_gaussian_sky_ps.hlsl:36-37 (sky_gaussian t2, sky_gaussian_points t3).
    cases.push_back({ "sky_gaussian", sky_gaussian_layout(), {
        { "pulled_mesh_positions", 0u },
        { "pulled_mesh_indices", 1u },
        { "sky_gaussian", 2u },
        { "sky_gaussian_points", 3u },
    } });

    for (const Case& c : cases) {
        RenderBindingLayoutSrg srg;
        std::string error;
        ASSERT_TRUE(build_render_binding_layout_srg(c.layout, srg, error))
            << c.name << ": " << error;
        ASSERT_EQ(srg.descriptor_bindings.size(), c.expected.size())
            << c.name << ": binding count";

        std::map<std::string, uint32_t> got;
        for (const DescriptorBinding& b : srg.descriptor_bindings) {
            const std::string semantic{ descriptor_semantic_name(b.semantic) };
            got[semantic] = b.shader_register;
            EXPECT_EQ(b.register_space, kRenderBindingLayoutRegisterSpace)
                << c.name << ": " << semantic << " must be in space2";
        }
        for (const auto& [semantic, reg] : c.expected) {
            const auto it = got.find(semantic);
            ASSERT_NE(it, got.end())
                << c.name << ": layout is missing " << semantic;
            EXPECT_EQ(it->second, reg)
                << c.name << ": " << semantic << " landed at t" << it->second
                << " but its shader declares register(t" << reg << ", space2)";
        }
    }
}
