// src/engine/assets/mesh_style_pull_program.cpp

#include <engine/assets/mesh_style_pull_program.h>

#include <file/filesystem.h>

#include <string>

namespace wz::engine::assets
{
    namespace
    {
        constexpr const char* kMeshStylePullVsPath =
            "shaders/mesh_style/mesh_style_pull_vs.hlsl";
        constexpr const char* kMeshStylePullPsPath =
            "shaders/mesh_style/mesh_style_pull_ps.hlsl";

        // Canonical source for the styled RHI pull-mesh shaders (issue #195).
        // Staged into <project>/shaders/mesh_style/ on demand — the file carrier
        // resolves relative paths against the project root only, so the source
        // must live in the project for the shader asset to compile. This is the
        // ONLY copy (no engine resources/shaders/ twin), so the HLSL and the
        // MeshStyleDrawConstants packing in rhi_scene_renderer.cpp cannot drift
        // from two files.
        //
        // Byte layout MUST match MeshStyleDrawConstants (28 dwords / 112 bytes):
        //   0    mvp              (float4x4, column-major, = view_proj * world)
        //   64   wireframe_color  (float4)
        //   80   surface_color    (float4)
        //   96   style_params     (float4 = wireframe_emissive,
        //                          surface_emissive, alpha, mode)
        // where mode >= 0.5 selects the wireframe layer, else the surface layer.
        constexpr const char* kMeshStylePullVsSource = R"HLSL(
// shaders/mesh_style/mesh_style_pull_vs.hlsl
//
// Styled RHI pull-mesh vertex shader (issue #195). Staged into the project by
// the engine (ensure_mesh_style_pull_program) — do not edit in place; the
// canonical source lives in mesh_style_pull_program.cpp.
//
// Same vertex-pull mechanics as the plain MVP pull program (SV_VertexID ->
// index buffer -> position buffer, both StructuredBuffers in the space2 object
// SRG), but the cbuffer is the 28-dword "mesh_style" root-constant block: the
// 16-float MVP the VS consumes, then 12 floats of baked style shading the PS
// reads. One b0/space2 cbuffer bound to both stages.
cbuffer MeshStyle : register(b0, space2)
{
    float4x4 mvp;
    float4 wireframe_color;
    float4 surface_color;
    float4 style_params;
};

StructuredBuffer<float3> g_positions : register(t0, space2);
StructuredBuffer<uint>   g_indices   : register(t1, space2);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    uint source_vertex = g_indices[vertex_id];
    float3 position = g_positions[source_vertex];

    VSOutput output;
    output.position = mul(mvp, float4(position, 1.0f));
    // The pull path carries no per-vertex normals; a fixed up-facing normal
    // keeps the PS facing term stable.
    output.normal = float3(0.0f, 1.0f, 0.0f);
    return output;
}
)HLSL";

        constexpr const char* kMeshStylePullPsSource = R"HLSL(
// shaders/mesh_style/mesh_style_pull_ps.hlsl
//
// Styled RHI pull-mesh pixel shader (issue #195). Staged into the project by
// the engine (ensure_mesh_style_pull_program) — do not edit in place; the
// canonical source lives in mesh_style_pull_program.cpp.
//
// Reads the baked mesh-style shading from the shared 28-dword "mesh_style"
// root-constant block. style_params.w selects the layer whose colour drives
// the shade: mode >= 0.5 = wireframe layer (paired with a WireframeCullNone
// raster program), else the surface layer. Wireframe vs. solid RASTER is a
// PROGRAM property (RasterMode on the render_program asset), never encoded
// here — this PS only picks which style colour to shade with.
cbuffer MeshStyle : register(b0, space2)
{
    float4x4 mvp;
    float4 wireframe_color;
    float4 surface_color;
    float4 style_params;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    bool wireframe = style_params.w >= 0.5f;
    float4 layer_color = wireframe ? wireframe_color : surface_color;
    float emissive =
        max(wireframe ? style_params.x : style_params.y, 0.0f);

    float3 n = normalize(input.normal);
    float facing = saturate(n.y * 0.45f + 0.55f);
    float3 lit = layer_color.rgb * (0.35f + 0.65f * facing);
    float3 color = lit + layer_color.rgb * emissive;

    // style_params.z is the overall style alpha; fold the layer's own alpha in.
    float alpha = layer_color.a * style_params.z;
    return float4(color, alpha);
}
)HLSL";

        bool stage_shader_source(
            wz::Logger& logger,
            FileCarrierAssetModule& files,
            const char* project_relative_path,
            const char* source)
        {
            const wz::fs::Path full = files.resolve_path(project_relative_path);
            if (wz::fs::exists(full)) {
                return true;
            }

            // <project>/shaders/mesh_style — both files share this directory.
            const wz::fs::Path dir =
                files.resolve_path("shaders/mesh_style");
            if (wz::fs::create_directories(dir) != wz::fs::FileError::None) {
                logger.error(
                    "mesh style program: failed to create shader directory: "
                    + dir);
                return false;
            }
            if (wz::fs::write_file_text(full, source)
                != wz::fs::FileError::None)
            {
                logger.error(
                    "mesh style program: failed to stage shader source: "
                    + full);
                return false;
            }
            return true;
        }
    }

    RenderProgramAsset ensure_mesh_style_pull_program(
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        ShaderAssetModule& shaders,
        RenderProgramAssetModule& render_programs,
        const MeshRenderStyleData& style)
    {
        if (!stage_shader_source(
                logger, files, kMeshStylePullVsPath, kMeshStylePullVsSource)
            || !stage_shader_source(
                logger, files, kMeshStylePullPsPath, kMeshStylePullPsSource))
        {
            return {};
        }

        // Derive the pipeline state (program properties) from the style. The
        // wireframe LOOK is the raster mode; the per-style colours flow as data
        // via the recipe's baked shading, not the program.
        const bool wireframe_look =
            style.wireframe.enabled && !style.surface.enabled;
        const RasterMode raster =
            wireframe_look
                ? RasterMode::WireframeCullNone
                : style.double_sided
                    ? RasterMode::SolidCullNone
                    : RasterMode::SolidCullBack;
        const DepthMode depth =
            style.depth_test
                ? (style.depth_write
                    ? DepthMode::TestWrite
                    : DepthMode::TestNoWrite)
                : DepthMode::Disabled;
        const bool transparent = is_mesh_render_style_transparent(style);
        const wz::rhi::BlendMode blend =
            transparent ? wz::rhi::BlendMode::AlphaBlend
                        : wz::rhi::BlendMode::Opaque;

        // Fold the derived state into the name so distinct pipeline states get
        // distinct program assets while identical states dedup to one.
        const std::string name =
            std::string("mesh_style/pull")
            + (wireframe_look ? "#wire" : "#solid")
            + (raster == RasterMode::SolidCullBack ? ".cullback" : ".cullnone")
            + (depth == DepthMode::TestWrite
                ? ".depth_rw"
                : depth == DepthMode::TestNoWrite ? ".depth_r" : ".depth_off")
            + (transparent ? ".blend" : ".opaque");

        const ShaderPairAsset shader_pair = shaders.create_shader_pair({
            .name = name,
            .vertex_path = kMeshStylePullVsPath,
            .pixel_path = kMeshStylePullPsPath,
            .vertex_entry = "main",
            .pixel_entry = "main",
            .vertex_target = "vs_5_1",
            .pixel_target = "ps_5_1",
        });
        if (!shader_pair.valid()) {
            logger.error("mesh style program: shader pair registration failed");
            return {};
        }

        uint32_t policy_flags = RenderPolicy_None;
        if (wireframe_look) {
            policy_flags |= RenderPolicy_Wireframe;
        }
        if (style.depth_test) {
            policy_flags |= RenderPolicy_DepthTest;
        }
        if (style.depth_write) {
            policy_flags |= RenderPolicy_DepthWrite;
        }
        if (transparent) {
            policy_flags |= RenderPolicy_AlphaBlend;
        }

        // Object SRG shape = binding_layout==4 (render_program_compilers.cpp):
        // one 28-dword "mesh_style" root constant (visibility All — the block is
        // one b0/space2 cbuffer bound to both stages) + the two pulled-mesh
        // SRVs. RhiSceneRenderer packs MeshStyleDrawConstants into it per draw.
        return render_programs.create_custom({
            .name = name,
            .vertex_shader = shader_pair.vertex_shader,
            .pixel_shader = shader_pair.pixel_shader,
            .binding_model = RenderBindingModel::MeshVertexPull,
            .topology = RenderPrimitiveTopology::TriangleList,
            .default_domain =
                transparent ? RenderDomain::Transparent : RenderDomain::Opaque,
            .default_policy_flags = policy_flags,
            .input_layout = InputLayoutKind::None,
            .blend_mode = blend,
            .depth_mode = depth,
            .raster_mode = raster,
            .root_constants = {{
                .visibility = ShaderVisibility::All,
                .shader_register = 0,
                .register_space = 2,
                .value_count = 28,
                .semantic = "mesh_style",
            }},
            .descriptor_bindings = {
                DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::PulledMeshPositions,
                    .shader_register = 0,
                    .register_space = 2,
                    .descriptor_count = 1,
                },
                DescriptorBinding{
                    .kind = DescriptorKind::StructuredBufferSRV,
                    .visibility = ShaderVisibility::Vertex,
                    .semantic = DescriptorSemantic::PulledMeshIndices,
                    .shader_register = 1,
                    .register_space = 2,
                    .descriptor_count = 1,
                },
            },
        });
    }
}
