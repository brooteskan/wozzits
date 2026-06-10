#include "scene_authoring_materialize_test_support.h"

#include <engine/rendering/render_program_pipeline_cache.h>
#include <window/window2.h>

namespace
{
    struct SceneRenderShaderMaterializeGpuFixture : public ::testing::Test
    {
        wz::Logger logger;
        wz::window::WindowHandle window{};
        wz::gpu::Device device{};
        wz::fs::Path root;

        void SetUp() override
        {
            root = wz::fs::join(
                wz::fs::temp_directory_path(),
                "wozzits_scene_materialize_render_shader_test");
            ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
            ASSERT_EQ(
                wz::fs::create_directories(
                    wz::fs::join(root, "shaders/render")),
                wz::fs::FileError::None);
            ASSERT_EQ(
                wz::fs::create_directories(
                    wz::fs::join(root, "shaders/mesh_wavelet")),
                wz::fs::FileError::None);

            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(root, "shaders/render/custom_mesh_vs.hlsl"),
                    "cbuffer MeshConstants : register(b0) {\n"
                    "    float4x4 World;\n"
                    "    float4x4 ViewProj;\n"
                    "    float4 Style0;\n"
                    "    float4 Style1;\n"
                    "};\n"
                    "struct VSIn {\n"
                    "    float3 pos : POSITION;\n"
                    "    float3 normal : NORMAL;\n"
                    "    float2 uv : TEXCOORD0;\n"
                    "    uint vertex_id : SV_VertexID;\n"
                    "};\n"
                    "struct VSOut {\n"
                    "    float4 pos : SV_Position;\n"
                    "    float3 normal : NORMAL;\n"
                    "    float2 uv : TEXCOORD0;\n"
                    "    nointerpolation uint vertex_id : TEXCOORD1;\n"
                    "};\n"
                    "VSOut main(VSIn input) {\n"
                    "    VSOut output;\n"
                    "    const float4 world_pos = mul(World, float4(input.pos, 1.0));\n"
                    "    output.pos = mul(ViewProj, world_pos);\n"
                    "    output.normal = input.normal;\n"
                    "    output.uv = input.uv;\n"
                    "    output.vertex_id = input.vertex_id;\n"
                    "    return output;\n"
                    "}\n"),
                wz::fs::FileError::None);

            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(root, "shaders/render/custom_mesh_ps.hlsl"),
                    "struct PSIn {\n"
                    "    float4 pos : SV_Position;\n"
                    "    float3 normal : NORMAL;\n"
                    "    float2 uv : TEXCOORD0;\n"
                    "    nointerpolation uint vertex_id : TEXCOORD1;\n"
                    "};\n"
                    "float4 main(PSIn input) : SV_Target {\n"
                    "    const float shade = saturate(input.normal.y * 0.5 + 0.5);\n"
                    "    return float4(input.uv.x, shade, input.uv.y, 1.0);\n"
                    "}\n"),
                wz::fs::FileError::None);

            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(
                        root,
                        "shaders/render/custom_mesh_field_ps.hlsl"),
                    "StructuredBuffer<float> MeshFieldValues : register(t0);\n"
                    "struct PSIn {\n"
                    "    float4 pos : SV_Position;\n"
                    "    float3 normal : NORMAL;\n"
                    "    float2 uv : TEXCOORD0;\n"
                    "    nointerpolation uint vertex_id : TEXCOORD1;\n"
                    "};\n"
                    "float4 main(PSIn input) : SV_Target {\n"
                    "    const float value = saturate(MeshFieldValues[input.vertex_id]);\n"
                    "    return float4(value, input.uv.x, 1.0 - value, 1.0);\n"
                    "}\n"),
                wz::fs::FileError::None);

            ASSERT_EQ(
                wz::fs::write_file_text(
                    wz::fs::join(
                        root,
                        "shaders/mesh_wavelet/detail_heat_cs.hlsl"),
                    "struct VertexSignal {\n"
                    "    float3 position;\n"
                    "    float3 normal;\n"
                    "};\n"
                    "StructuredBuffer<VertexSignal> Vertices : register(t0);\n"
                    "RWStructuredBuffer<float> Values : register(u0);\n"
                    "cbuffer Params : register(b0) {\n"
                    "    uint VertexCount;\n"
                    "    uint ScaleCount;\n"
                    "    float LambdaMax;\n"
                    "    float Gamma;\n"
                    "    float3 BoundsMin;\n"
                    "    float BoundsRangeY;\n"
                    "    float3 BoundsMax;\n"
                    "    uint _Pad0;\n"
                    "};\n"
                    "[numthreads(128, 1, 1)]\n"
                    "void main(uint3 id : SV_DispatchThreadID) {\n"
                    "    const uint vertex_id = id.x;\n"
                    "    if (vertex_id >= VertexCount) {\n"
                    "        return;\n"
                    "    }\n"
                    "    const VertexSignal signal = Vertices[vertex_id];\n"
                    "    const float height01 = saturate(\n"
                    "        (signal.position.y - BoundsMin.y) / max(BoundsRangeY, 0.0001));\n"
                    "    const float slope = saturate(1.0 - abs(normalize(signal.normal).y));\n"
                    "    float detail_sum = 0.0;\n"
                    "    [loop]\n"
                    "    for (uint scale = 0; scale < ScaleCount; ++scale) {\n"
                    "        const float scale_weight =\n"
                    "            (float(scale) + 1.0) / max(float(ScaleCount), 1.0);\n"
                    "        const float wave = 0.5 + 0.5 * sin(\n"
                    "            height01 * max(LambdaMax, 0.0001)\n"
                    "            * (float(scale) + 1.0) * 6.2831853);\n"
                    "        const float position_energy = saturate(\n"
                    "            pow(abs(height01 - 0.5) * 2.0 * wave, max(Gamma, 0.0001)));\n"
                    "        const float normal_energy = saturate(\n"
                    "            pow(slope * scale_weight * wave, max(Gamma, 0.0001)));\n"
                    "        Values[scale * 2u * VertexCount + vertex_id] = position_energy;\n"
                    "        Values[(scale * 2u + 1u) * VertexCount + vertex_id] = normal_energy;\n"
                    "        detail_sum += 0.5 * position_energy + 0.5 * normal_energy;\n"
                    "    }\n"
                    "    Values[(ScaleCount * 2u) * VertexCount + vertex_id] =\n"
                    "        detail_sum / max(float(ScaleCount), 1.0);\n"
                    "}\n"),
                wz::fs::FileError::None);

            wz::window::WindowDesc desc{};
            desc.title = "scene_render_shader_materialize_test";
            desc.width = 64;
            desc.height = 64;
            desc.resizable = false;

            window = wz::window::create_window(desc);
            ASSERT_TRUE(window.native);

            device = wz::gpu::create_device(window);
            ASSERT_TRUE(device.impl);
        }

        void TearDown() override
        {
            if (device.impl) {
                wz::gpu::destroy_device(device);
            }
            if (window.native) {
                wz::window::destroy_window(window);
            }
        }
    };
}

TEST_F(
    SceneRenderShaderMaterializeGpuFixture,
    MeshRenderShaderMaterializesAndFeedsStyledRenderable)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    ASSERT_EQ(
        wz::fs::write_file_text(
            wz::fs::join(root, "render_shader.scene.json"),
            R"({
  "schema": "wozzits.scene.v0",
  "name": "render_shader_materialize",
  "nodes": [
    {
      "id": "mesh",
      "mesh_source": {
        "kind": "procedural_cube"
      },
      "render_shader": {
        "program_id": "mesh/custom_surface",
        "vertex_hlsl_path": "shaders/render/custom_mesh_vs.hlsl",
        "pixel_hlsl_path": "shaders/render/custom_mesh_ps.hlsl",
        "vertex_entry": "main",
        "pixel_entry": "main",
        "vertex_target": "vs_5_0",
        "pixel_target": "ps_5_0",
        "binding_model": "mesh_ia",
        "input_layout": "mesh_position_normal_uv",
        "blend": "opaque",
        "depth": "test_write",
        "raster": "solid_cull_none"
      }
    }
  ]
})"),
        wz::fs::FileError::None);

    const SceneAsset scene_asset =
        assets.scenes().create_scene_from_json({
            .name = "render_shader_materialize",
            .path = "render_shader.scene.json",
        });
    ASSERT_TRUE(scene_asset.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const SceneHandle scene_handle = assets.scenes().get_scene(scene_asset);
    ASSERT_TRUE(scene_handle.valid());
    const SceneAssetData* parsed_scene =
        assets.scenes().get_scene_data(scene_handle);
    ASSERT_NE(parsed_scene, nullptr);

    SceneAssetData scene = *parsed_scene;

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 1u);
    ASSERT_TRUE(scene.nodes[0].render_shader.has_value());
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    EXPECT_NE(
        scene.nodes[0].render_shader->render_program_asset,
        wz::asset::AssetKey{});

    ASSERT_TRUE(assets.commit());
    const auto resolve_report = assets.resolve_all();
    if (!resolve_report.ok()) {
        ADD_FAILURE() << "resolve_all failed with "
                      << resolve_report.failures.size() << " failure(s)";
        for (const auto& failure : resolve_report.failures) {
            ADD_FAILURE() << "  error=" << static_cast<int>(failure.error);
        }
    }
    ASSERT_TRUE(resolve_report.ok());

    const RenderableHandle renderable =
        assets.renderables().get_renderable(
            RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());

    const RenderableAssetData* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    ASSERT_TRUE(renderable_data->render_program.valid());
    EXPECT_EQ(renderable_data->program, BuiltinRenderProgram::MeshSurface);
    EXPECT_TRUE(renderable_data->mesh_style.surface.enabled);
    EXPECT_FALSE(renderable_data->mesh_style.wireframe.enabled);

    const RenderProgramData* program_data =
        assets.render_programs().get_render_program_data(
            renderable_data->render_program);
    ASSERT_NE(program_data, nullptr);
    EXPECT_EQ(program_data->binding_model, RenderBindingModel::MeshIA);
    EXPECT_EQ(program_data->topology, RenderPrimitiveTopology::TriangleList);
    EXPECT_EQ(program_data->default_domain, RenderDomain::Opaque);
    EXPECT_NE(
        program_data->default_policy_flags & RenderPolicy_DepthTest,
        0u);
    EXPECT_NE(
        program_data->default_policy_flags & RenderPolicy_DepthWrite,
        0u);
    EXPECT_EQ(
        program_data->input_layout,
        InputLayoutKind::MeshPositionNormalUV);
    EXPECT_EQ(program_data->blend_mode, BlendMode::Opaque);
    EXPECT_EQ(program_data->depth_mode, DepthMode::TestWrite);
    EXPECT_EQ(program_data->raster_mode, RasterMode::SolidCullNone);
    ASSERT_EQ(program_data->root_constants.size(), 1u);
    EXPECT_EQ(program_data->root_constants[0].visibility, ShaderVisibility::All);
    EXPECT_EQ(program_data->root_constants[0].shader_register, 0u);
    EXPECT_EQ(program_data->root_constants[0].register_space, 0u);
    EXPECT_EQ(program_data->root_constants[0].value_count, 40u);
    EXPECT_TRUE(program_data->descriptor_bindings.empty());
    EXPECT_TRUE(program_data->vertex_shader.valid());
    EXPECT_TRUE(program_data->pixel_shader.valid());

    SceneAssetData rebuilt_scene = *parsed_scene;
    const auto rebuilt_report =
        materialize_scene_authoring_components(rebuilt_scene, assets);
    ASSERT_TRUE(rebuilt_report.ok) << rebuilt_report.error;
    ASSERT_TRUE(rebuilt_scene.nodes[0].render_shader.has_value());
    EXPECT_EQ(
        rebuilt_scene.nodes[0].render_shader->render_program_asset,
        scene.nodes[0].render_shader->render_program_asset);
}

TEST_F(
    SceneRenderShaderMaterializeGpuFixture,
    MeshRenderShaderCanShareStyledRenderableWithFieldVisualization)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "render_shader_field_visualization";

    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.mesh_wavelet_analysis = SceneMeshWaveletAnalysisAsset{
        .enabled = true,
        .function = SceneMeshWaveletAnalysisFunction::BuiltinDetailHeatV0,
        .scale_count = 3,
        .lambda_max_estimate = 2.0f,
        .gamma = 0.8f,
    };
    node.mesh_render_style = SceneMeshRenderStyleAsset{
        .surface = SceneMeshRenderLayerAsset{ .enabled = true },
        .depth_test = true,
        .depth_write = true,
        .field_visualization_enabled = true,
        .field_visualization_channel_id = MeshWaveletChannelID::kDetailCost,
        .field_visualization_value_min = 0.0f,
        .field_visualization_value_max = 1.0f,
        .field_visualization_gamma = 0.8f,
    };
    node.render_shader = SceneRenderShaderAsset{
        .program_id = "mesh/custom_surface_with_field",
        .vertex_hlsl_path = "shaders/render/custom_mesh_vs.hlsl",
        .pixel_hlsl_path = "shaders/render/custom_mesh_field_ps.hlsl",
        .descriptor_bindings = {{
            .kind = "structured_buffer_srv",
            .visibility = "pixel",
            .semantic = "mesh_field_visualization",
            .shader_register = 0,
            .register_space = 0,
            .descriptor_count = 1,
        }},
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    ASSERT_TRUE(report.ok) << report.error;
    ASSERT_EQ(scene.nodes.size(), 1u);
    ASSERT_TRUE(scene.nodes[0].render_shader.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_render_style.has_value());
    ASSERT_TRUE(scene.nodes[0].mesh_wavelet_analysis.has_value());
    ASSERT_TRUE(scene.nodes[0].renderable_asset.has_value());
    EXPECT_NE(
        scene.nodes[0].render_shader->render_program_asset,
        wz::asset::AssetKey{});
    EXPECT_NE(
        scene.nodes[0].mesh_render_style->field_visualization_asset,
        wz::asset::AssetKey{});
    EXPECT_EQ(
        scene.nodes[0].mesh_wavelet_analysis->field_asset,
        scene.nodes[0].mesh_render_style->field_visualization_asset);

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    const RenderableHandle renderable =
        assets.renderables().get_renderable(
            RenderableAsset{ .output = *scene.nodes[0].renderable_asset });
    ASSERT_TRUE(renderable.valid());

    const RenderableAssetData* renderable_data =
        assets.renderables().get_renderable_data(renderable);
    ASSERT_NE(renderable_data, nullptr);
    EXPECT_EQ(
        renderable_data->mesh_field_visualization_asset,
        scene.nodes[0].mesh_render_style->field_visualization_asset);
    ASSERT_TRUE(renderable_data->render_program.valid());
    EXPECT_EQ(renderable_data->program, BuiltinRenderProgram::MeshFieldHeatmap);
    EXPECT_TRUE(renderable_data->mesh_style.field_visualization.enabled);

    const RenderProgramData* program_data =
        assets.render_programs().get_render_program_data(
            renderable_data->render_program);
    ASSERT_NE(program_data, nullptr);
    EXPECT_EQ(program_data->binding_model, RenderBindingModel::MeshIA);
    EXPECT_EQ(
        program_data->input_layout,
        InputLayoutKind::MeshPositionNormalUV);
    ASSERT_EQ(program_data->descriptor_bindings.size(), 1u);
    EXPECT_EQ(
        program_data->descriptor_bindings[0].kind,
        DescriptorKind::StructuredBufferSRV);
    EXPECT_EQ(
        program_data->descriptor_bindings[0].visibility,
        ShaderVisibility::Pixel);
    EXPECT_EQ(
        program_data->descriptor_bindings[0].semantic,
        DescriptorSemantic::MeshFieldVisualization);
    EXPECT_EQ(program_data->descriptor_bindings[0].shader_register, 0u);
    EXPECT_EQ(program_data->descriptor_bindings[0].register_space, 0u);
    EXPECT_EQ(program_data->descriptor_bindings[0].descriptor_count, 1u);

    wz::engine::rendering::RenderProgramPipelineCache pipeline_cache;
    ASSERT_TRUE(
        pipeline_cache.realize(
            device,
            assets.render_programs().table(),
            renderable_data->render_program));
    const auto* layout =
        pipeline_cache.get_binding_layout(renderable_data->render_program);
    ASSERT_NE(layout, nullptr);
    ASSERT_EQ(layout->root_constants.size(), 1u);
    EXPECT_EQ(layout->root_constants[0].root_parameter_index, 0u);
    ASSERT_EQ(layout->desc_tables.size(), 1u);
    EXPECT_EQ(layout->desc_tables[0].root_parameter_index, 1u);
    EXPECT_EQ(layout->desc_tables[0].heap_start_slot, 0u);
    EXPECT_EQ(layout->desc_tables[0].slot_count, 1u);
    ASSERT_EQ(layout->descriptors.size(), 1u);
    EXPECT_EQ(
        layout->descriptors[0].semantic,
        DescriptorSemantic::MeshFieldVisualization);
    EXPECT_EQ(layout->descriptors[0].root_parameter_index, 1u);
    EXPECT_EQ(layout->descriptors[0].descriptor_table_offset, 0u);
}

TEST_F(
    SceneRenderShaderMaterializeGpuFixture,
    MeshRenderShaderFieldDescriptorRequiresEnabledFieldVisualization)
{
    using namespace wz::engine::assets;

    EngineAssetLibrary assets{ device, logger, root };

    SceneAssetData scene{};
    scene.name = "render_shader_field_visualization_reject";

    SceneNodeAsset node = make_scene_node("mesh");
    node.mesh_source = SceneMeshSourceAsset{
        .kind = SceneMeshSourceKind::ProceduralCube,
    };
    node.render_shader = SceneRenderShaderAsset{
        .program_id = "mesh/custom_surface_with_field",
        .vertex_hlsl_path = "shaders/render/custom_mesh_vs.hlsl",
        .pixel_hlsl_path = "shaders/render/custom_mesh_field_ps.hlsl",
        .descriptor_bindings = {{
            .kind = "structured_buffer_srv",
            .visibility = "pixel",
            .semantic = "mesh_field_visualization",
            .shader_register = 0,
            .register_space = 0,
            .descriptor_count = 1,
        }},
    };
    scene.nodes.push_back(std::move(node));

    const auto report =
        materialize_scene_authoring_components(scene, assets);
    EXPECT_FALSE(report.ok);
    EXPECT_NE(
        report.error.find("field visualization"),
        std::string::npos);
}
