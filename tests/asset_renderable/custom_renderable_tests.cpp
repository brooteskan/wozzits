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

#include <engine/assets/atmosphere_asset_module.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/placement_asset_module.h>
#include <engine/assets/placed_field_asset_module.h>
#include <engine/assets/render_binding_layout/render_binding_layout.h>
#include <engine/assets/render_binding_layout_asset_module.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/render_program/render_program_asset_module.h>
#include <engine/assets/engine_asset_key_factory.h>
#include <engine/assets/key_factories/hlsl_shader.h>
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
#include <gpu/shader_types.h>
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

    // Camera-snapped terrain fixture shaders (issue #233): the SRG is the
    // clipmap shape — a 32-dword CameraSnappedTerrain cbuffer (view_projection +
    // per-level snap / world→uv / vertical / texel-extent) at b0 space2, the two
    // mesh-pull StructuredBuffers, a height texture sampled in the VS, and a
    // LinearClamp sampler. Minimal (displace + project); the recipe fields, not
    // the pixels, are what the terrain-head test asserts.
    constexpr const char* kTerrainVs = R"(
cbuffer Terrain : register(b0, space2)
{
    float4x4 view_projection;
    float4   snap_params;
    float4   world_to_uv;
    float4   texel_and_vertical;
    float4   texel_dims_extent;
};

StructuredBuffer<float3> positions : register(t0, space2);
StructuredBuffer<uint>   indices   : register(t1, space2);
Texture2D<float>         heightTex : register(t2, space2);
SamplerState             linearSampler : register(s0, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
};

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = indices[vid];
    float3 p   = positions[idx];
    float2 uv  = p.xz * world_to_uv.xy + world_to_uv.zw;
    float  h   = heightTex.SampleLevel(linearSampler, uv, 0.0f);
    p.y += h * texel_and_vertical.z + texel_and_vertical.w;

    VSOut o;
    o.pos = mul(view_projection, float4(p, 1.0f));
    return o;
}
)";

    constexpr const char* kTerrainPs = R"(
struct PSIn
{
    float4 pos : SV_POSITION;
};

float4 main(PSIn input) : SV_TARGET
{
    return float4(0.3f, 0.5f, 0.2f, 1.0f);
}
)";

    // View-frequency fog first light. These are BODIES, not whole shaders:
    // neither declares a cbuffer, a register, or a sampler. Every declaration
    // they use — the terrain constant block, the two pull buffers, the height
    // texture, the static sampler, and the space-0 view-constants buffer —
    // arrives from the generated binding prelude (#231), which the 0x105 node
    // prepends before the HLSL compiler ever sees them. If the layout stopped
    // declaring a row, these would fail to compile rather than read a stale
    // register.
    //
    // Separate strings from kTerrainVs/kTerrainPs, which hand-declare their
    // bindings and are shared by the tests above.
    constexpr const char* kFogTerrainVsBody = R"(
struct VSOut
{
    float4 pos       : SV_POSITION;
    float3 world_pos : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    uint   idx = pulled_mesh_indices[vid];
    float3 p   = pulled_mesh_positions[idx];
    float2 uv  = p.xz * world_to_uv.xy + world_to_uv.zw;
    float  h   = scalar_field_texture.SampleLevel(sampler0, uv, 0.0f).x;
    p.y += h * texel_and_vertical.z + texel_and_vertical.w;

    VSOut o;
    o.pos       = mul(view_projection, float4(p, 1.0f));
    o.world_pos = p;
    return o;
}
)";

    // The one-liner the whole sequence exists to make writable: a shader fogs
    // by naming the world position and nothing else. wz_fog_from_view reads
    // view_constants[0] — a binding this body never mentions.
    constexpr const char* kFogTerrainPsBody = R"(
struct PSIn
{
    float4 pos       : SV_POSITION;
    float3 world_pos : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float3 colour = float3(0.3f, 0.5f, 0.2f);
    return float4(wz_fog_from_view(colour, input.world_pos), 1.0f);
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

    // The camera-snapped terrain SRG (issue #233): a 32-dword
    // CameraSnappedTerrain constant block (no authored tail — the head fills all
    // 32 dwords), the two mesh-pull buffers, and a VS-sampled height texture with
    // a LinearClamp sampler. The clipmap shape, expressed as an authored layout.
    ea::RenderBindingLayoutData terrain_layout()
    {
        ea::RenderBindingLayoutData layout{};
        layout.constants_semantic = "terrain";
        layout.constants_visibility = ea::ShaderVisibility::Vertex;
        layout.constants_head =
            ea::RenderBindingConstantsHead::CameraSnappedTerrain;
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
                .visibility = ea::ShaderVisibility::Vertex,
            },
        };
        layout.samplers = {
            {
                .kind = ea::StaticSamplerKind::LinearClamp,
                .visibility = ea::ShaderVisibility::Vertex,
            },
        };
        return layout;
    }

    // The same camera-snapped terrain SRG plus a VIEW-frequency block: one
    // extra StructuredBuffer row at register space 0, which the renderer fills
    // once per frame with the observer + the frame's atmosphere (a8d3450 /
    // da6c952). The object rows are untouched, so the recipe, the height
    // binding and the footprint derivation are identical to terrain_layout().
    ea::RenderBindingLayoutData fog_terrain_layout()
    {
        ea::RenderBindingLayoutData layout = terrain_layout();
        layout.view_head = ea::RenderBindingViewHead::CameraFog;
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
            write_text_file(
                root / "shaders" / "terrain" / "terrain_vs.hlsl", kTerrainVs);
            write_text_file(
                root / "shaders" / "terrain" / "terrain_ps.hlsl", kTerrainPs);
            write_text_file(
                root / "shaders" / "terrain" / "fog_vs_body.hlsl",
                kFogTerrainVsBody);
            write_text_file(
                root / "shaders" / "terrain" / "fog_ps_body.hlsl",
                kFogTerrainPsBody);
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
            const ea::RenderBindingLayoutData& layout_data,
            const char* vertex_path = "shaders/custom/custom_vs.hlsl",
            const char* pixel_path = "shaders/custom/custom_ps.hlsl")
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
                    .vertex_path = vertex_path,
                    .pixel_path = pixel_path,
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

        // Register one shader THROUGH the binding prelude: a body file that
        // declares no bindings → a 0x105 prelude node wiring that body to the
        // authored layout → an HLSL shader node compiling the combined source.
        // This is the DAG route (#231), so a layout edit re-keys the prelude and
        // the shader; the alternative (an HLSL #include) would let generated
        // declarations go stale against the root signature.
        wz::asset::AssetKey register_prelude_shader(
            ea::EngineAssetLibrary& assets,
            const char* body_path,
            const wz::asset::AssetKey& layout_key,
            uint64_t prelude_id,
            wz::gpu::ShaderStage stage,
            const char* target)
        {
            const wz::asset::AssetKey body =
                assets.files().register_file_node(
                    body_path,
                    ea::kHLSLFileSchema,
                    wz::asset::AssetType::ShaderSource);
            EXPECT_FALSE(body == wz::asset::AssetKey{});

            const wz::asset::AssetKey prelude_key =
                test_renderable_key(prelude_id);
            wz::asset::AssetNode prelude{};
            prelude.key = prelude_key;
            prelude.type = wz::asset::AssetType::ShaderSource;
            prelude.schema = ea::kHLSLBindingPreludeSchema;
            prelude.stage = wz::asset::AssetStage::Source;
            prelude.residency = wz::asset::ResidencyIntent::CompileOnly;
            prelude.payload = std::vector<uint8_t>{};
            prelude.meta = wz::asset::ParamBlock{};
            // Port order: source(0), binding_layout(1).
            EXPECT_TRUE(assets.system().register_asset(
                std::move(prelude),
                std::vector<wz::asset::AssetKey>{ body, layout_key }));

            // Same key derivation ShaderAssetModule uses, with the PRELUDE
            // output standing in for the source file.
            const wz::asset::AssetKey shader_key = ea::make_hlsl_shader_key(
                prelude_key, static_cast<uint8_t>(stage), "main", target);
            wz::asset::AssetNode shader{};
            shader.key = shader_key;
            shader.type = wz::asset::AssetType::Shader;
            shader.schema = ea::kHLSLShaderSchema;
            shader.stage = wz::asset::AssetStage::Source;
            shader.payload = std::vector<uint8_t>{};
            shader.meta = wz::gpu::HLSLCompileDesc{
                .stage = stage,
                .entry = "main",
                .target = target,
                .primary_source_index = 0,
            };
            EXPECT_TRUE(assets.system().register_asset(
                std::move(shader),
                std::vector<wz::asset::AssetKey>{ prelude_key }));
            return shader_key;
        }

        // build_ingredients' sibling for the prelude route: identical mesh,
        // height field and authored layout, but both shaders come from
        // binding-free bodies combined with the layout's generated
        // declarations. space2 registers still require SM 5.1.
        Ingredients build_prelude_ingredients(
            ea::EngineAssetLibrary& assets,
            const ea::RenderBindingLayoutData& layout_data,
            uint64_t prelude_id_base)
        {
            Ingredients out{};
            out.mesh = assets.meshes().create_procedural_mesh({
                .name = "prelude/cube",
                .kind = ea::ProceduralMeshKind::Cube,
            });
            EXPECT_TRUE(out.mesh.valid());

            out.field = assets.scalar_fields().create_procedural_scalar_field({
                .name = "prelude/field",
                .width = 16,
                .height = 16,
                .generator = ea::ScalarFieldGenerator::RadialGradient,
            });
            EXPECT_TRUE(out.field.valid());

            const auto layout =
                assets.render_binding_layouts().create_render_binding_layout({
                    .name = "prelude/layout",
                    .layout = layout_data,
                });
            EXPECT_TRUE(layout.valid());

            ea::CustomRenderProgramDesc desc =
                custom_program_desc("prelude/program", layout.output);
            desc.vertex_shader = register_prelude_shader(
                assets,
                "shaders/terrain/fog_vs_body.hlsl",
                layout.output,
                prelude_id_base,
                wz::gpu::ShaderStage::Vertex,
                "vs_5_1");
            desc.pixel_shader = register_prelude_shader(
                assets,
                "shaders/terrain/fog_ps_body.hlsl",
                layout.output,
                prelude_id_base + 1,
                wz::gpu::ShaderStage::Pixel,
                "ps_5_1");
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

// The overlay draw layer (seam 3): an authored draw_layer="overlay" reaches the
// compiled recipe, so the renderer records it in the overlay pass (on top).
TEST_F(CustomRenderableFixture, AuthoredDrawLayerReachesTheRecipe)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());
    const Ingredients ingredients =
        build_ingredients(assets, tint_field_layout());

    wz::asset::ParamBlock params;
    params.values["binding0_semantic"] = std::string("scalar_field_texture");
    params.values["draw_layer"] = int64_t{ 1 };   // 1 == overlay
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets, 1, std::move(params),
        { ingredients.mesh.output,
          ingredients.program.key,
          ingredients.field.output });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const ea::RhiRenderableRecipe* recipe =
        assets.renderables().get_rhi_renderable_recipe(
            ea::RenderableAsset{ .output = renderable_key });
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->draw_layer, ea::DrawLayer::Overlay);
}

// Unauthored defaults to World, so every existing renderable is unchanged.
TEST_F(CustomRenderableFixture, DrawLayerDefaultsToWorld)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());
    const Ingredients ingredients =
        build_ingredients(assets, tint_field_layout());

    wz::asset::ParamBlock params;
    params.values["binding0_semantic"] = std::string("scalar_field_texture");
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets, 1, std::move(params),
        { ingredients.mesh.output,
          ingredients.program.key,
          ingredients.field.output });

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const ea::RhiRenderableRecipe* recipe =
        assets.renderables().get_rhi_renderable_recipe(
            ea::RenderableAsset{ .output = renderable_key });
    ASSERT_NE(recipe, nullptr);
    EXPECT_EQ(recipe->draw_layer, ea::DrawLayer::World);
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

// #233/#234: a CameraSnappedTerrain-head custom renderable compiles into a
// recipe that names the height field (from the scalar_field_texture binding) and
// carries the world footprint from a Placement wired at the placement port — the
// same data the 0x708 clipmap recipe carries, so the generic per-frame terrain
// packer fills the block — then records a device draw. This is the exact pattern
// test_mesh_001's clipmap now uses (graph-path 0x70A + placement port).
TEST_F(CustomRenderableFixture, CameraSnappedTerrainWithPlacementCarriesFootprintAndDraws)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    const Ingredients ingredients = build_ingredients(
        assets, terrain_layout(),
        "shaders/terrain/terrain_vs.hlsl", "shaders/terrain/terrain_ps.hlsl");

    // The world footprint the terrain shares with a collision on the same
    // Placement (#218): extent.xz = world size, extent.y = vertical scale.
    const ea::PlacementAsset placement =
        assets.placements().create_placement(ea::PlacementDesc{
            .name = "terrain/placement",
            .origin = { 0.0f, 0.0f, 0.0f },
            .extent = { 16.0f, 4.0f, 16.0f },
            .base_height = 0.0f,
        });
    ASSERT_TRUE(placement.valid());

    // Port-ordered deps: geometry(0), program(1), field at binding0(2), and the
    // Placement at the placement port (2 + kMaxRenderBindingLayoutBindings = 10).
    std::vector<wz::asset::AssetKey> ports(11);
    ports[0] = ingredients.mesh.output;
    ports[1] = ingredients.program.key;
    ports[2] = ingredients.field.output;
    ports[10] = placement.output;

    wz::asset::ParamBlock params;
    params.values["binding0_semantic"] =
        std::string("scalar_field_texture");
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets, 20, std::move(params), std::move(ports));

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    ASSERT_TRUE(resolve.ok());

    const ea::RhiRenderableRecipe* recipe =
        assets.renderables().get_rhi_renderable_recipe(
            ea::RenderableAsset{ .output = renderable_key });
    ASSERT_NE(recipe, nullptr);
    EXPECT_TRUE(recipe->custom);
    EXPECT_EQ(
        recipe->constants_head,
        ea::RenderBindingConstantsHead::CameraSnappedTerrain);
    // The height field the renderer samples + derives texel dims from.
    EXPECT_TRUE(recipe->height_texture_key == ingredients.field.output);
    // The Placement is authoritative for the footprint (extent → world size /
    // vertical scale), exactly the 0x708 #218 derivation.
    EXPECT_TRUE(recipe->clipmap.view_snapped);
    EXPECT_TRUE(recipe->clipmap.placement_authoritative);
    EXPECT_FLOAT_EQ(recipe->clipmap.world_size[0], 16.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.world_size[1], 16.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.vertical_scale, 4.0f);

    // Drive one device frame: the CameraSnappedTerrain head packs through the
    // custom head switch (pack_camera_snapped_terrain_constants) and the height
    // texture binds at scalar_field_texture — the recorder must accept the draw.
    wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);
    ea::SceneNodeAsset node{};
    node.id = wz::scene::AuthoredEntityId{ 1 };
    node.name = "terrain";
    node.visible = true;
    node.renderable_asset = renderable_key;
    const std::vector<ea::SceneNodeAsset> nodes{ node };

    ASSERT_TRUE(wz::gpu::begin_frame(device));
    wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
    const bool recorded = renderer.render_scene(
        nodes, assets, wz::math::Mat4::identity(),
        wz::math::Vec3{ 2.0f, 5.0f, -3.0f });
    EXPECT_TRUE(recorded)
        << "camera-snapped terrain renderable failed to realize or the recorder "
           "rejected the draw";
    ASSERT_TRUE(wz::gpu::end_frame(device));
    wz::gpu::present(device, /*sync_interval*/ 0);
}

// #223 Seam 2: a CameraSnappedTerrain-head custom renderable can take its height
// field AND world footprint from a single PlacedField upstream instead of a
// separately-wired scalar_field_texture binding + placement port. The PlacedField
// SUPERSEDES both — its field is injected as the height binding and its placement
// drives the footprint — so the terrain and a collision on the same PlacedField
// cannot drift apart. The resolved recipe is identical to the directly-wired
// clipmap, so it draws the same way.
TEST_F(CustomRenderableFixture, CameraSnappedTerrainFromPlacedFieldCarriesFootprintAndDraws)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    const Ingredients ingredients = build_ingredients(
        assets, terrain_layout(),
        "shaders/terrain/terrain_vs.hlsl", "shaders/terrain/terrain_ps.hlsl");

    const ea::PlacementAsset placement =
        assets.placements().create_placement(ea::PlacementDesc{
            .name = "terrain/placed_field_frame",
            .origin = { 0.0f, 0.0f, 0.0f },
            .extent = { 16.0f, 4.0f, 16.0f },
            .base_height = 0.0f,
        });
    ASSERT_TRUE(placement.valid());

    // One upstream binding the height field to its world frame (issue #223).
    const ea::PlacedFieldAsset placed =
        assets.placed_fields().create_placed_field(ea::PlacedFieldDesc{
            .field_key = ingredients.field.output,
            .placement_key = placement.output,
            .field_type = ea::kAssetTypeScalarField,
        });
    ASSERT_TRUE(placed.valid());

    // Port-ordered deps: geometry(0), program(1), and the PlacedField at the
    // placed_field port (3 + kMaxRenderBindingLayoutBindings = 11). NO separate
    // field binding, NO placement port — the PlacedField provides both.
    std::vector<wz::asset::AssetKey> ports(12);
    ports[0] = ingredients.mesh.output;
    ports[1] = ingredients.program.key;
    ports[11] = placed.output;

    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets, 21, wz::asset::ParamBlock{}, std::move(ports));

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    ASSERT_TRUE(resolve.ok());

    const ea::RhiRenderableRecipe* recipe =
        assets.renderables().get_rhi_renderable_recipe(
            ea::RenderableAsset{ .output = renderable_key });
    ASSERT_NE(recipe, nullptr);
    EXPECT_TRUE(recipe->custom);
    EXPECT_EQ(
        recipe->constants_head,
        ea::RenderBindingConstantsHead::CameraSnappedTerrain);
    // The PlacedField's field is injected as the scalar_field_texture binding —
    // the same height source a directly-wired clipmap would carry.
    EXPECT_TRUE(recipe->height_texture_key == ingredients.field.output);
    // The PlacedField's placement is authoritative for the footprint (extent →
    // world size / vertical scale), exactly the placement-port derivation.
    EXPECT_TRUE(recipe->clipmap.view_snapped);
    EXPECT_TRUE(recipe->clipmap.placement_authoritative);
    EXPECT_FLOAT_EQ(recipe->clipmap.world_size[0], 16.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.world_size[1], 16.0f);
    EXPECT_FLOAT_EQ(recipe->clipmap.vertical_scale, 4.0f);

    // Same recipe shape as the directly-wired clipmap → the renderer draws it the
    // same way. Drive one device frame to confirm the recorder accepts the draw.
    wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);
    ea::SceneNodeAsset node{};
    node.id = wz::scene::AuthoredEntityId{ 1 };
    node.name = "terrain";
    node.visible = true;
    node.renderable_asset = renderable_key;
    const std::vector<ea::SceneNodeAsset> nodes{ node };

    ASSERT_TRUE(wz::gpu::begin_frame(device));
    wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
    const bool recorded = renderer.render_scene(
        nodes, assets, wz::math::Mat4::identity(),
        wz::math::Vec3{ 2.0f, 5.0f, -3.0f });
    EXPECT_TRUE(recorded)
        << "placed-field camera-snapped terrain failed to realize or the "
           "recorder rejected the draw";
    ASSERT_TRUE(wz::gpu::end_frame(device));
    wz::gpu::present(device, /*sync_interval*/ 0);
}

// #233: the terrain head needs the height field. With the layout's
// scalar_field_texture row unbound, the renderable fails to compile with the
// reason naming that semantic (so a terrain look can't render heightless).
TEST_F(CustomRenderableFixture, CameraSnappedTerrainWithoutHeightBindingFails)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    const Ingredients ingredients = build_ingredients(
        assets, terrain_layout(),
        "shaders/terrain/terrain_vs.hlsl", "shaders/terrain/terrain_ps.hlsl");

    // No scalar_field_texture binding wired.
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets,
        21,
        wz::asset::ParamBlock{},
        { ingredients.mesh.output, ingredients.program.key });

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    EXPECT_FALSE(resolve.ok());

    const auto state = assets.system().node_resolve_state(renderable_key);
    ASSERT_TRUE(state.has_value());
    EXPECT_NE(
        state->detail.find("scalar_field_texture"), std::string::npos)
        << "detail was: " << state->detail;
}

// ── View-frequency fog: first light ─────────────────────────────────────────
//
// The whole chain in one draw, running end to end for the first time: an
// authored layout declares view_head = CameraFog → the SRG derivation puts a
// view_constants StructuredBuffer row at register space 0 (3e25b63) → the
// binding prelude emits WzViewConstants, that buffer, and wz_fog_from_view
// (1bd882f) → a pixel body that names no binding at all fogs with one line →
// RhiSceneRenderer packs the observer + the resolved Atmosphere into the buffer
// and binds it as the packet's slot-0 group (da6c952) → the frame draws.
//
// WHAT THIS PROVES
//   * The pixel shader compiled against the GENERATED declarations. Its body
//     declares no cbuffer and no register, so everything it names came from the
//     layout by way of the 0x105 prelude node.
//   * The fog term is genuinely in the drawn shader. The body's only colour
//     expression is wz_fog_from_view, which the prelude emits ONLY for a layout
//     with a view head — the sibling test below shows the same body failing to
//     compile without one.
//   * The program realized, and from the asset compiler rather than a
//     render-time bridge. Its root signature and the declarations the shader
//     compiled against are ONE derivation of ONE layout
//     (build_render_binding_layout_srg, re-emitted as text by the prelude), so
//     a realized program means the space-0 row is present on both sides of it.
//   * The renderer reached bind_view_constants and acquired the view buffer —
//     which happens only after the semantic gate finds view_constants in the
//     program's slot-0 group.
//   * The bytes uploaded into that buffer are the atmosphere authored here.
//
// WHAT THIS DOES NOT PROVE
//   That the PIXELS came out fogged. Render-target readback is impractical in
//   this harness (see tests/gpu/texture_mip.cpp), so nothing here compares a
//   fogged frame against an unfogged one, and the fog arithmetic itself is
//   unverified on the GPU. What is verified is that the term is compiled in,
//   the buffer is bound, and the values in it are the authored ones.
TEST_F(CustomRenderableFixture, ViewFogPreludeBindsViewConstantsAndDraws)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    const Ingredients ingredients =
        build_prelude_ingredients(assets, fog_terrain_layout(), 40);

    const ea::PlacementAsset placement =
        assets.placements().create_placement(ea::PlacementDesc{
            .name = "terrain/fog_placement",
            .origin = { 0.0f, 0.0f, 0.0f },
            .extent = { 16.0f, 4.0f, 16.0f },
            .base_height = 0.0f,
        });
    ASSERT_TRUE(placement.valid());

    // A fog nobody could mistake for the default: magenta, thick, and starting
    // close enough that the cube sits well inside it. Every dial distinct, so a
    // transposed pair shows up in the pack assertions below.
    const ea::AtmosphereAsset atmosphere_asset =
        assets.atmospheres().create_atmosphere(ea::AtmosphereDesc{
            .name = "terrain/fog",
            .fog_color = { 0.85f, 0.20f, 0.55f },
            .fog_density = 0.25f,
            .fog_start_distance = 1.0f,
            .fog_height_falloff = 0.125f,
            .fog_enabled = true,
        });
    ASSERT_TRUE(atmosphere_asset.valid());

    // Same port-ordered wiring as the terrain test above: geometry(0),
    // program(1), field at binding0(2), Placement at the placement port(10).
    std::vector<wz::asset::AssetKey> ports(11);
    ports[0] = ingredients.mesh.output;
    ports[1] = ingredients.program.key;
    ports[2] = ingredients.field.output;
    ports[10] = placement.output;

    wz::asset::ParamBlock params;
    params.values["binding0_semantic"] = std::string("scalar_field_texture");
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets, 30, std::move(params), std::move(ports));

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    for (const ea::ResolveFailure& f : resolve.failures) {
        ADD_FAILURE() << "resolve failure: error="
                      << static_cast<int>(f.error) << " " << f.detail;
    }
    // The pixel body calls wz_fog_from_view and declares nothing. Resolving at
    // all means the prelude supplied the helper AND every register the vertex
    // body uses.
    ASSERT_TRUE(resolve.ok());

    const ea::RhiRenderableRecipe* recipe =
        assets.renderables().get_rhi_renderable_recipe(
            ea::RenderableAsset{ .output = renderable_key });
    ASSERT_NE(recipe, nullptr);
    EXPECT_TRUE(recipe->custom);
    // The view block is additive: the object side of the recipe is exactly what
    // the no-view-head terrain test asserts.
    EXPECT_EQ(
        recipe->constants_head,
        ea::RenderBindingConstantsHead::CameraSnappedTerrain);
    EXPECT_TRUE(recipe->height_texture_key == ingredients.field.output);

    const ea::AtmosphereHandle atmosphere_handle =
        assets.atmospheres().get_atmosphere(atmosphere_asset);
    ASSERT_TRUE(atmosphere_handle.valid());
    const ea::AtmosphereData* atmosphere =
        assets.atmospheres().get_atmosphere_data(atmosphere_handle);
    ASSERT_NE(atmosphere, nullptr);
    ASSERT_TRUE(atmosphere->fog_enabled);

    wz::engine::rendering::RhiSceneRenderer renderer(gpu, logger);
    ea::SceneNodeAsset node{};
    node.id = wz::scene::AuthoredEntityId{ 1 };
    node.name = "fog_terrain";
    node.visible = true;
    node.renderable_asset = renderable_key;
    const std::vector<ea::SceneNodeAsset> nodes{ node };

    const wz::math::Vec3 camera_world_pos{ 2.0f, 5.0f, -3.0f };

    // The height field went resident during resolve; what the RENDER acquires
    // on top of that is the observable below.
    const std::size_t resident_before = renderer.resident_gpu_resource_count();

    ASSERT_TRUE(wz::gpu::begin_frame(device));
    wz::gpu::clear(device, 0.1f, 0.1f, 0.12f, 1.0f);
    const bool recorded = renderer.render_scene(
        nodes, assets, wz::math::Mat4::identity(), camera_world_pos,
        /*world_transforms*/ {}, atmosphere);
    EXPECT_TRUE(recorded)
        << "the view-fog renderable failed to realize or the recorder rejected "
           "the draw";
    ASSERT_TRUE(wz::gpu::end_frame(device));
    wz::gpu::present(device, /*sync_interval*/ 0);

    // The program came from the asset compiler, so the root signature it
    // realized against is the one derived from the authored layout — including
    // the space-0 row the pixel shader references.
    EXPECT_GT(renderer.registered_program_count(), 0u);
    EXPECT_EQ(renderer.render_time_program_bridge_count(), 0u)
        << "the fog program was bridged at render time, not produced by the "
           "asset compiler";

    // THREE resources acquired by the render, not two: the mesh-pull positions
    // and indices every custom renderable needs, PLUS the view-constants
    // buffer. That third one is the proof the view SRG was built —
    // ensure_view_constants_buffer() is reachable only from
    // bind_view_constants(), and only past the gate that looks the
    // view_constants semantic up in the program's slot-0 group.
    //
    // This assertion carries the binding half of the test on its own, and it
    // was mutation-verified: stubbing bind_view_constants to return without
    // binding drops this delta to 2 — while `recorded` above stays TRUE. The
    // draw succeeding does NOT detect an unbound view group, so do not delete
    // this in the belief the frame would have caught it. (Measured directly:
    // the same terrain scene through a no-view-head layout acquires 2.)
    EXPECT_EQ(renderer.resident_gpu_resource_count() - resident_before, 3u)
        << "the render did not acquire the view-constants buffer";

    // The two SRGs the packet carries — the object group and the view group —
    // each resolve to a descriptor table.
    EXPECT_GT(renderer.cached_descriptor_table_count(), 0u);

    // And the bytes inside that buffer. make_view_constants is the function
    // render_scene called, so re-running it on the same observer and the same
    // resolved atmosphere reproduces exactly what was uploaded.
    const wz::engine::rendering::ViewConstants uploaded =
        wz::engine::rendering::make_view_constants(
            camera_world_pos, atmosphere);
    EXPECT_FLOAT_EQ(uploaded.camera[0], 2.0f);
    EXPECT_FLOAT_EQ(uploaded.camera[1], 5.0f);
    EXPECT_FLOAT_EQ(uploaded.camera[2], -3.0f);
    EXPECT_FLOAT_EQ(uploaded.fog_color[0], 0.85f);
    EXPECT_FLOAT_EQ(uploaded.fog_color[1], 0.20f);
    EXPECT_FLOAT_EQ(uploaded.fog_color[2], 0.55f);
    EXPECT_FLOAT_EQ(uploaded.fog_color[3], 0.25f);    // density in .w
    EXPECT_FLOAT_EQ(uploaded.fog_params[0], 1.0f);    // start distance
    EXPECT_FLOAT_EQ(uploaded.fog_params[1], 0.125f);  // height falloff
    // The flag wz_fog_from_view tests before it does anything. On — so the
    // helper the shader calls takes its fog branch rather than returning early.
    EXPECT_FLOAT_EQ(uploaded.fog_params[2], 1.0f);
}

// The fog term in that pixel body is load-bearing, not decoration.
// wz_fog_from_view is emitted only for a layout that declares a view head
// (1bd882f), so the SAME body against the plain terrain layout does not
// compile. That is what makes the shader the test above draws with provably a
// fogging one, rather than one that merely could have been.
TEST_F(CustomRenderableFixture, FogPixelBodyDoesNotCompileWithoutAViewHead)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    // terrain_layout(), NOT fog_terrain_layout(): identical object rows, no
    // view head. Same bodies, same prelude node, same shader nodes.
    const Ingredients ingredients =
        build_prelude_ingredients(assets, terrain_layout(), 50);
    ASSERT_TRUE(ingredients.program.valid());

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    EXPECT_FALSE(resolve.ok())
        << "a body calling wz_fog_from_view compiled against a layout that "
           "declares no view block";

    bool named_the_helper = false;
    std::string details;
    for (const ea::ResolveFailure& f : resolve.failures) {
        named_the_helper = named_the_helper
            || f.detail.find("wz_fog_from_view") != std::string::npos;
        details += f.detail + "\n";
    }
    EXPECT_TRUE(named_the_helper)
        << "the compile failed for some other reason; details were:\n"
        << details;
}

// The other half of the ownership rule the view row needed. A declared
// view_constants row is satisfied WITHOUT a port (that is what lets the test
// above compile at all), so the matching restriction has to hold too: a
// renderable may not author a binding for it, because the value is per-FRAME
// state the renderer packs and no renderable has a copy of. Untested, that
// rejection could be deleted and only the permissive half would remain.
TEST_F(CustomRenderableFixture, ViewConstantsCannotBeBoundFromAPort)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    ea::EngineAssetLibrary assets(gpu, logger, root.string());

    // The hand-declared terrain shaders are fine here: nothing has to READ the
    // view block for the layout to declare the row.
    const Ingredients ingredients = build_ingredients(
        assets, fog_terrain_layout(),
        "shaders/terrain/terrain_vs.hlsl", "shaders/terrain/terrain_ps.hlsl");

    wz::asset::ParamBlock params;
    params.values["binding0_semantic"] = std::string("view_constants");
    const wz::asset::AssetKey renderable_key = register_custom_renderable(
        assets,
        31,
        std::move(params),
        { ingredients.mesh.output,
          ingredients.program.key,
          ingredients.field.output });

    ASSERT_TRUE(assets.commit());
    const ea::ResolveReport resolve = assets.resolve_all();
    EXPECT_FALSE(resolve.ok());

    // Named for what is actually wrong. Falling through to the source-type
    // lookup would report "a Scalar field cannot back binding 'view_constants'",
    // which reads as "wire a different asset" — but nothing can back this row.
    const auto state = assets.system().node_resolve_state(renderable_key);
    ASSERT_TRUE(state.has_value());
    EXPECT_NE(
        state->detail.find("the renderer fills each"), std::string::npos)
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

// #231 staleness cascade, device-free: routing the binding declarations
// THROUGH the DAG (a 0x105 prelude node wiring a shader-source body + the
// authored layout) is the whole point — an edit to the layout must re-key the
// prelude so the generated declarations can never go stale against the root
// signature via a cached shader. Editing a layout param changes the prelude
// node's computed key; leaving it unchanged keeps the key stable.
TEST(HlslBindingPreludeCascade, LayoutEditRekeysPreludeNode)
{
    using namespace wz::asset;

    const CompilerRegistry registry =
        wz::engine::editor::build_asset_graph_schema_registry();
    const AssetKeyFactoryFn key_factory =
        ea::make_engine_asset_key_factory(registry);

    const auto param_node = [](
        AssetType type, SchemaID schema, ParamBlock params)
    {
        AssetNode node{};
        node.type = type;
        node.schema = schema;
        node.stage = AssetStage::Source;
        node.residency = ResidencyIntent::CompileOnly;
        node.payload = std::vector<uint8_t>{};
        node.meta = std::move(params);
        return node;
    };

    ParamBlock layout_params;
    layout_params.values["constants_semantic"] = std::string("custom_block");
    layout_params.values["binding0_semantic"] =
        std::string("scalar_field_texture");
    layout_params.values["binding0_kind"] = int64_t{ 0 };

    ParamBlock source_params;
    source_params.values["source_path"] = std::string("shaders/body.hlsl");

    ParamBlock prelude_params;
    prelude_params.values["name"] = std::string("");

    AssetGraphDraft draft{};
    const AssetGraphDraftNodeId layout_id = add_asset_graph_draft_node(
        draft,
        param_node(
            ea::kAssetTypeRenderBindingLayout,
            ea::kRenderBindingLayoutSchema,
            layout_params),
        AssetGraphDraftNodeState::Created);
    const AssetGraphDraftNodeId source_id = add_asset_graph_draft_node(
        draft,
        param_node(
            AssetType::ShaderSource, ea::kHLSLFileSchema, source_params),
        AssetGraphDraftNodeState::Created);
    const AssetGraphDraftNodeId prelude_id = add_asset_graph_draft_node(
        draft,
        param_node(
            AssetType::ShaderSource,
            ea::kHLSLBindingPreludeSchema,
            prelude_params),
        AssetGraphDraftNodeState::Created);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, source_id, prelude_id, 0),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);
    ASSERT_NE(
        connect_asset_graph_draft_nodes(draft, layout_id, prelude_id, 1),
        INVALID_ASSET_GRAPH_DRAFT_EDGE);

    const auto prelude_key = [&]() -> AssetKey
    {
        const std::vector<AssetGraphDraftRegistration> regs =
            asset_graph_draft_to_registrations(draft, &registry, key_factory);
        for (const AssetGraphDraftRegistration& reg : regs) {
            if (reg.draft_node == prelude_id) {
                return reg.node.key;
            }
        }
        return {};
    };

    const AssetKey key_before = prelude_key();
    ASSERT_TRUE(asset_graph_draft_key_valid(key_before));

    // Recomputing with no change is stable (content addressing).
    EXPECT_TRUE(key_before == prelude_key());

    // Edit the layout: a different declared binding. The layout re-keys, and
    // that flows into the prelude's dependency hash.
    AssetGraphDraftNode* layout_node =
        find_asset_graph_draft_node(draft, layout_id);
    ASSERT_NE(layout_node, nullptr);
    ParamBlock edited = layout_params;
    edited.values["binding0_semantic"] =
        std::string("mesh_field_visualization");
    layout_node->node.meta = edited;

    EXPECT_FALSE(key_before == prelude_key())
        << "editing the wired layout did not re-key the prelude node";
}
