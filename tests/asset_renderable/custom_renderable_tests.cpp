// tests/asset_renderable/custom_renderable_tests.cpp
//
// Issue #228 seam tests: the custom renderable schema 0x70A.
//
// The device test draws a mesh through a FULLY AUTHORED program — custom
// shaders + an authored render-binding-layout (#227) defining the SRG — with
// one scalar-field texture bound by semantic through a generic Any-typed
// binding port and one authored "tint" float4 constant filling the layout's
// declared tail field. Zero per-look C++: the compiler validated the authored
// bindings against the program's layout, and RhiSceneRenderer bound the
// recipe generically (no per-recipe semantic table).
//
// The negative tests lock the compile-time validation channel: an unbound
// layout semantic and a kind-mismatched source both fail compile with the
// reason surfaced in NodeResolveState::detail (the node-inspector channel,
// #212). The draft test locks the substrate extension: an Any-typed binding
// port accepts every provider type at edge time (validation moves to
// compile), while the Mesh-typed geometry port still rejects a mismatch.

#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/render_binding_layout_asset_module.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/renderable_asset_module.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/scalar_field_asset_module.h>
#include <engine/assets/gaussian_splat_asset_module.h>
#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/shader_asset_module.h>
#include <engine/assets/type_extensions.h>
#include <engine/editor/asset_graph_schema_registry.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_scene_renderer.h>

#include <asset/draft.h>
#include <asset/system.h>

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

    // The authored SRG shape: a 20-dword root-constant block (Mvp16 head +
    // a declared float4 "tint" tail field) at b0 space2, the two mesh-pull
    // StructuredBuffers at t0/t1, the scalar-field texture at t2, and a
    // LinearClamp sampler at s0 — registers all DERIVED from row order.
    constexpr const char* kCustomVs = R"(
cbuffer CustomBlock : register(b0, space2)
{
    float4x4 mvp;
    float4   tint;
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = indices[vid];
    float3 p   = positions[idx];

    VSOut o;
    o.pos = mul(mvp, float4(p, 1.0f));
    o.uv  = p.xy * 0.5f + 0.5f;
    return o;
}
)";

    constexpr const char* kCustomPs = R"(
cbuffer CustomBlock : register(b0, space2)
{
    float4x4 mvp;
    float4   tint;
};

Texture2D<float> fieldTex     : register(t2, space2);
SamplerState     fieldSampler : register(s0, space2);

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float h = fieldTex.SampleLevel(fieldSampler, input.uv, 0.0f);
    return float4(tint.rgb * (0.25f + 0.75f * h), tint.a);
}
)";

    void write_text_file(const fs::path& path, const char* text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        ASSERT_TRUE(out.good()) << "failed to open " << path.string();
        out << text;
    }

    // The authored layout the custom program wires in: exactly the SRG the
    // shaders above declare.
    ea::RenderBindingLayoutData tint_field_layout()
    {
        ea::RenderBindingLayoutData layout{};
        layout.constants_semantic = "custom_block";
        layout.constants_visibility = ea::ShaderVisibility::All;
        layout.constants_head = ea::RenderBindingConstantsHead::Mvp16;
        layout.constant_fields = {
            {
                .name = "tint",
                .type = ea::RenderBindingConstantType::Float4,
            },
        };
        layout.bindings = {
            {
                .semantic = "pulled_mesh_positions",
                .kind = ea::RenderBindingKind::StructuredSrv,
                .visibility = ea::ShaderVisibility::Vertex,
            },
            {
                .semantic = "pulled_mesh_indices",
                .kind = ea::RenderBindingKind::StructuredSrv,
                .visibility = ea::ShaderVisibility::Vertex,
            },
            {
                .semantic = "scalar_field_texture",
                .kind = ea::RenderBindingKind::TextureSrv,
                .visibility = ea::ShaderVisibility::Pixel,
            },
        };
        layout.samplers = {
            {
                .kind = ea::StaticSamplerKind::LinearClamp,
                .visibility = ea::ShaderVisibility::Pixel,
            },
        };
        return layout;
    }

    ea::CustomRenderProgramDesc custom_program_desc(
        const std::string& name,
        const wz::asset::AssetKey& layout_key)
    {
        ea::CustomRenderProgramDesc desc{};
        desc.name = name;
        desc.binding_layout = layout_key;
        desc.binding_model = ea::RenderBindingModel::MeshVertexPull;
        desc.topology = ea::RenderPrimitiveTopology::TriangleList;
        desc.default_domain = ea::RenderDomain::Opaque;
        desc.default_policy_flags =
            ea::RenderPolicy_DepthTest | ea::RenderPolicy_DepthWrite;
        desc.input_layout = ea::InputLayoutKind::None;
        desc.blend_mode = ea::BlendMode::Opaque;
        desc.depth_mode = ea::DepthMode::TestWrite;
        desc.raster_mode = ea::RasterMode::SolidCullNone;
        return desc;
    }

    wz::asset::AssetKey test_renderable_key(uint64_t id)
    {
        return wz::asset::AssetKey{
            .content_hash = { id, 0x70A0'0000ull },
            .schema_hash = { id ^ 0x10ull, 0x70A0'0001ull },
            .compiler_hash = { id ^ 0x20ull, 0x70A0'0002ull },
            .deps_hash = { id ^ 0x30ull, 0x70A0'0003ull },
        };
    }

    // Registers a graph-param-authored 0x70A node exactly as a committed
    // draft does: ParamBlock meta + PORT-ORDERED dep keys (an empty key =
    // unwired optional port).
    wz::asset::AssetKey register_custom_renderable(
        ea::EngineAssetLibrary& assets,
        uint64_t id,
        wz::asset::ParamBlock params,
        std::vector<wz::asset::AssetKey> port_dep_keys)
    {
        const wz::asset::AssetKey key = test_renderable_key(id);
        wz::asset::AssetNode node{};
        node.key = key;
        node.type = ea::kAssetTypeRenderable;
        node.schema = ea::kCustomRenderableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = std::move(params);
        EXPECT_TRUE(assets.system().register_asset(
            std::move(node), std::move(port_dep_keys)));
        return key;
    }

    struct CustomRenderableFixture : public ::testing::Test
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        wz::Logger logger;
        fs::path root;

        void SetUp() override
        {
            wz::window::WindowDesc window_desc{};
            window_desc.title = "custom_renderable_test";
            window_desc.width = 128;
            window_desc.height = 128;
            window_desc.resizable = false;

            window = wz::window::create_window(window_desc);
            if (!window.valid()) {
                GTEST_SKIP() << "no window available for the device test";
            }
            device = wz::gpu::create_device(window);
            if (!device.valid()) {
                wz::window::destroy_window(window);
                window = {};
                GTEST_SKIP() << "no GPU device available for the device test";
            }

            root = fs::temp_directory_path()
                / ("wozzits_custom_renderable_test_"
                   + std::to_string(static_cast<unsigned long long>(
                         std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count())));
            fs::remove_all(root);
            write_text_file(
                root / "shaders" / "custom" / "custom_vs.hlsl", kCustomVs);
            write_text_file(
                root / "shaders" / "custom" / "custom_ps.hlsl", kCustomPs);
        }

        void TearDown() override
        {
            if (device.valid()) {
                wz::gpu::destroy_device(device);
            }
            if (window.valid()) {
                wz::window::destroy_window(window);
            }
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        // Builds the shared ingredients: cube mesh, resident height field,
        // shaders, and the layout-authored custom program.
        struct Ingredients
        {
            ea::MeshAsset mesh{};
            ea::ScalarFieldAsset field{};
            ea::RenderProgramAsset program{};
        };

        Ingredients build_ingredients(
            ea::EngineAssetLibrary& assets,
            const ea::RenderBindingLayoutData& layout_data)
        {
            Ingredients out{};
            out.mesh = assets.meshes().create_procedural_mesh({
                .name = "custom/cube",
                .kind = ea::ProceduralMeshKind::Cube,
            });
            EXPECT_TRUE(out.mesh.valid());

            out.field = assets.scalar_fields().create_procedural_scalar_field({
                .name = "custom/field",
                .width = 16,
                .height = 16,
                .generator = ea::ScalarFieldGenerator::RadialGradient,
            });
            EXPECT_TRUE(out.field.valid());

            const auto layout =
                assets.render_binding_layouts().create_render_binding_layout({
                    .name = "custom/layout",
                    .layout = layout_data,
                });
            EXPECT_TRUE(layout.valid());

            // space2 register bindings require SM 5.1 (vs_5_0 rejects
            // `register(b0, space2)`, error X3721).
            const ea::ShaderPairAsset shaders =
                assets.shaders().create_shader_pair({
                    .name = "custom/program",
                    .vertex_path = "shaders/custom/custom_vs.hlsl",
                    .pixel_path = "shaders/custom/custom_ps.hlsl",
                    .vertex_target = "vs_5_1",
                    .pixel_target = "ps_5_1",
                });
            EXPECT_TRUE(shaders.valid());

            ea::CustomRenderProgramDesc desc =
                custom_program_desc("custom/program", layout.output);
            desc.vertex_shader = shaders.vertex_shader;
            desc.pixel_shader = shaders.pixel_shader;
            out.program = assets.render_programs().create_custom(desc);
            EXPECT_TRUE(out.program.valid());
            return out;
        }
    };
}

// The full seam: mesh + authored layout + custom program + one semantic
// binding + one authored constant → compiled generic recipe → a recorded
// on-device draw, with zero per-look C++.
TEST_F(CustomRenderableFixture, DrawsThroughAuthoredLayoutGenerically)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    const Ingredients ingredients =
        build_ingredients(assets, tint_field_layout());

    wz::asset::ParamBlock params;
    params.values["binding0_semantic"] = std::string("scalar_field_texture");
    params.values["const0_name"] = std::string("tint");
    params.values["const0_value"] = std::array<float, 3>{ 0.2f, 0.7f, 0.3f };
    params.values["const0_w"] = 1.0;
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets,
        1,
        std::move(params),
        { ingredients.mesh.output,
          ingredients.program.key,
          ingredients.field.output });

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    for (const ea::ResolveFailure& f : resolve.failures) {
        ADD_FAILURE() << "resolve failure: error="
                      << static_cast<int>(f.error) << " " << f.detail;
    }
    ASSERT_TRUE(resolve.ok());

    // The compiled recipe is the GENERIC form: the authored binding resolved
    // to the publisher's variant, the constant to its baked offset behind the
    // 16-dword Mvp16 head.
    const ea::RhiRenderableRecipe* recipe =
        assets.renderables().get_rhi_renderable_recipe(
            ea::RenderableAsset{ .output = renderable_key });
    ASSERT_NE(recipe, nullptr);
    EXPECT_TRUE(recipe->custom);
    ASSERT_EQ(recipe->bindings.size(), 1u);
    EXPECT_EQ(recipe->bindings[0].semantic, "scalar_field_texture");
    EXPECT_EQ(recipe->bindings[0].variant, "field_texture");
    EXPECT_TRUE(recipe->bindings[0].key == ingredients.field.output);
    EXPECT_EQ(recipe->constants_head, ea::RenderBindingConstantsHead::Mvp16);
    EXPECT_EQ(recipe->constants_dwords, 20u);
    ASSERT_EQ(recipe->constants.size(), 1u);
    EXPECT_EQ(recipe->constants[0].name, "tint");
    EXPECT_EQ(recipe->constants[0].offset_dwords, 16u);
    EXPECT_EQ(recipe->constants[0].dwords, 4u);
    EXPECT_FLOAT_EQ(recipe->constants[0].value[0], 0.2f);
    EXPECT_FLOAT_EQ(recipe->constants[0].value[1], 0.7f);
    EXPECT_FLOAT_EQ(recipe->constants[0].value[2], 0.3f);
    EXPECT_FLOAT_EQ(recipe->constants[0].value[3], 1.0f);

    // The field is resident under the identity the recipe names.
    const wz::rhi::GpuResourceHandle field_texture = gpu.resources.find(
        wz::rhi::ResourceIdentity{
            ea::rhi_asset_identity(ingredients.field.output, "field_texture"),
            {} });
    ASSERT_TRUE(field_texture.valid())
        << "scalar field did not become resident as a texture";

    // Drive one device frame through the renderer.
    wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);

    ea::SceneNodeAsset node{};
    node.id = wz::scene::AuthoredEntityId{ 1 };
    node.name = "custom";
    node.visible = true;
    node.renderable_asset = renderable_key;
    const std::vector<ea::SceneNodeAsset> nodes{ node };

    const wz::math::Mat4 view_projection = wz::math::Mat4::identity();
    const wz::math::Vec3 camera_world_pos{ 0.0f, 0.0f, -3.0f };

    ASSERT_TRUE(wz::gpu::begin_frame(device));
    wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
    const bool recorded = renderer.render_scene(
        nodes, assets, view_projection, camera_world_pos);
    EXPECT_TRUE(recorded)
        << "custom renderable failed to realize or the recorder rejected the "
           "draw";
    ASSERT_TRUE(wz::gpu::end_frame(device));
    wz::gpu::present(device, /*sync_interval*/ 0);

    // Structural proofs, mirroring the clipmap device test:
    //  - the program came from the asset compiler (no render-time bridge),
    EXPECT_GT(renderer.registered_program_count(), 0u);
    EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u)
        << "custom program was bridged at render time, not produced by the "
           "asset compiler";
    //  - 2 renderer-owned pull buffers + the resident field texture,
    EXPECT_EQ(renderer.resident_gpu_resource_count(), 3u)
        << "expected 2 renderer-owned pull buffers + 1 resident field texture";
    //  - the 3-descriptor object SRG built a descriptor table (the field
    //    texture bound through the GENERIC recipe.bindings walk).
    EXPECT_GT(renderer.cached_descriptor_table_count(), 0u)
        << "object SRG (incl. the field texture) did not bind a table";
}

// A layout row nothing supplies: compile fails and the reason names the
// semantic, on the node (NodeResolveState::detail — the inspector channel).
TEST_F(CustomRenderableFixture, UnboundSemanticFailsCompileWithReason)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    const Ingredients ingredients =
        build_ingredients(assets, tint_field_layout());

    // No binding0 wired, no semantic param: the layout's
    // scalar_field_texture row goes unsatisfied.
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets,
        2,
        wz::asset::ParamBlock{},
        { ingredients.mesh.output, ingredients.program.key });

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    EXPECT_FALSE(resolve.ok());

    const auto state = assets.system().node_resolve_state(renderable_key);
    ASSERT_TRUE(state.has_value());
    EXPECT_NE(
        state->detail.find("unbound semantic 'scalar_field_texture'"),
        std::string::npos)
        << "detail was: " << state->detail;
}

// A source whose published resource kind contradicts the layout row: a
// GaussianSplatCloud (structured buffer) into a TextureSrv row.
TEST_F(CustomRenderableFixture, KindMismatchFailsCompileWithReason)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    // Same shape as the tint layout, but the third row declares the
    // splat_cloud semantic as a TEXTURE SRV — which no splat source can back.
    ea::RenderBindingLayoutData layout = tint_field_layout();
    layout.bindings[2].semantic = "splat_cloud";
    layout.bindings[2].kind = ea::RenderBindingKind::TextureSrv;

    const Ingredients ingredients = build_ingredients(assets, layout);

    const ea::GaussianSplatCloudAsset cloud =
        assets.gaussian_splats().create_procedural_cloud({
            .name = "custom/cloud",
            .count = 8,
            .radius = 1.0f,
        });
    ASSERT_TRUE(cloud.valid());

    wz::asset::ParamBlock params;
    params.values["binding0_semantic"] = std::string("splat_cloud");
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets,
        3,
        std::move(params),
        { ingredients.mesh.output,
          ingredients.program.key,
          cloud.output });

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    EXPECT_FALSE(resolve.ok());

    const auto state = assets.system().node_resolve_state(renderable_key);
    ASSERT_TRUE(state.has_value());
    EXPECT_NE(
        state->detail.find("kind mismatch"), std::string::npos)
        << "detail was: " << state->detail;
    EXPECT_NE(
        state->detail.find("splat_cloud"), std::string::npos)
        << "detail was: " << state->detail;
}

// The substrate extension (#228): an Any-typed binding port accepts every
// provider type at edge time — no TypeMismatch — while a single-typed port
// (geometry, Mesh) still rejects the same provider. Pure draft validation,
// no device.
TEST(CustomRenderableDraft, AnyBindingPortSkipsEdgeTypeCheck)
{
    using namespace wz::asset;

    const CompilerRegistry registry =
        wz::engine::editor::build_asset_graph_schema_registry();

    const auto source_node = [](
        uint64_t id,
        AssetType type,
        SchemaID schema)
    {
        AssetNode node{};
        node.key = test_renderable_key(0x100 + id);
        node.type = type;
        node.schema = schema;
        node.stage = AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        return node;
    };

    // Scalar field → binding0 (port 2, Any-typed): no TypeMismatch. Other
    // validation messages (the unwired required ports) are expected and fine.
    {
        AssetGraphDraft draft{};
        const AssetGraphDraftNodeId field_node =
            add_asset_graph_draft_node(
                draft,
                source_node(
                    1,
                    ea::kAssetTypeScalarField,
                    ea::kScalarFieldProceduralSchema),
                AssetGraphDraftNodeState::Existing);
        const AssetGraphDraftNodeId renderable_node =
            add_asset_graph_draft_node(
                draft,
                source_node(
                    2,
                    ea::kAssetTypeRenderable,
                    ea::kCustomRenderableSchema),
                AssetGraphDraftNodeState::Existing);
        ASSERT_NE(
            connect_asset_graph_draft_nodes(
                draft, field_node, renderable_node, 2),
            INVALID_ASSET_GRAPH_DRAFT_EDGE);

        (void)validate_asset_graph_draft(draft, registry);
        for (const AssetGraphDraftValidationMessage& message :
             draft.validation_messages)
        {
            EXPECT_NE(
                message.code, AssetGraphDraftValidationCode::TypeMismatch)
                << message.message;
        }
    }

    // Control: the same scalar field into the Mesh-typed geometry port
    // (port 0) still trips TypeMismatch.
    {
        AssetGraphDraft draft{};
        const AssetGraphDraftNodeId field_node =
            add_asset_graph_draft_node(
                draft,
                source_node(
                    3,
                    ea::kAssetTypeScalarField,
                    ea::kScalarFieldProceduralSchema),
                AssetGraphDraftNodeState::Existing);
        const AssetGraphDraftNodeId renderable_node =
            add_asset_graph_draft_node(
                draft,
                source_node(
                    4,
                    ea::kAssetTypeRenderable,
                    ea::kCustomRenderableSchema),
                AssetGraphDraftNodeState::Existing);
        ASSERT_NE(
            connect_asset_graph_draft_nodes(
                draft, field_node, renderable_node, 0),
            INVALID_ASSET_GRAPH_DRAFT_EDGE);

        (void)validate_asset_graph_draft(draft, registry);
        bool saw_type_mismatch = false;
        for (const AssetGraphDraftValidationMessage& message :
             draft.validation_messages)
        {
            saw_type_mismatch = saw_type_mismatch
                || message.code
                    == AssetGraphDraftValidationCode::TypeMismatch;
        }
        EXPECT_TRUE(saw_type_mismatch)
            << "the Mesh-typed geometry port accepted a ScalarField";
    }
}
