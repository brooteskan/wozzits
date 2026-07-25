// src/engine/assets/puppet_program.cpp

#include <engine/assets/puppet_program.h>

#include <engine/assets/render_program/render_program.h>

#include <file/filesystem.h>

#include <string>

namespace wz::engine::assets
{
    namespace
    {
        // Canonical puppet shader source, embedded so it can be staged into the
        // project root on demand (the file carrier has no engine-resource
        // fallback). This is the single source of truth for the puppet shaders;
        // it MUST stay byte-compatible with the SRG built below and with the
        // per-Part packet build in rhi_scene_renderer.cpp. The space2 bindings
        // require Shader Model 5.1.
        //
        // VS: a Screen view head at (t0, space0) supplies the viewport; the
        // interleaved WzPuppetVertex (pos+uv) is pulled at (t0, space2) via the
        // index buffer at t1; the PuppetPartBlock (2D affine + opacity + the two
        // colour-modulation triples) is the 16-dword "puppet_part" root constant
        // at (b0, space2). PS: sample the Part's atlas at t2/space2 (resident
        // PREMULTIPLIED, #277), apply tint/screen_tint, scale by opacity, and
        // emit premultiplied for the program's PremultipliedAlpha blend.
        constexpr const char* kPuppetVsSource = R"HLSL(
struct WzScreenConstants
{
    float4 viewport;
};
StructuredBuffer<WzScreenConstants> screen_constants : register(t0, space0);

cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;
    float4 xform_row1;
    float4 part_tint;
    float4 part_screen_tint;
};

struct WzPuppetVertex
{
    float2 pos;
    float2 uv;
};
StructuredBuffer<WzPuppetVertex> vertices : register(t0, space2);
StructuredBuffer<uint>           indices  : register(t1, space2);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
    uint           idx = indices[vid];
    WzPuppetVertex v   = vertices[idx];
    float2 px = float2(
        xform_row0.x * v.pos.x + xform_row0.y * v.pos.y + xform_row0.z,
        xform_row1.x * v.pos.x + xform_row1.y * v.pos.y + xform_row1.z);
    float2 vp  = screen_constants[0].viewport.xy;
    float2 ndc = px * (2.0f / vp) - 1.0f;
    VSOut o;
    o.pos = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
    o.uv  = v.uv;
    return o;
}
)HLSL";

        constexpr const char* kPuppetPsSource = R"HLSL(
cbuffer PuppetPartBlock : register(b0, space2)
{
    float4 xform_row0;
    float4 xform_row1;
    float4 part_tint;
    float4 part_screen_tint;
};

Texture2D<float4> atlas   : register(t2, space2);
SamplerState      atlas_s : register(s0, space2);

struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 tex = atlas.SampleLevel(atlas_s, input.uv, 0.0f);
    float3 rgb = tex.rgb * part_tint.rgb;
    rgb = rgb + part_screen_tint.rgb * (tex.a - rgb);
    float opacity = xform_row0.w;
    return float4(rgb * opacity, tex.a * opacity);
}
)HLSL";

        bool stage_shader_source(
            wz::Logger& logger,
            const std::function<wz::fs::Path(const wz::fs::Path&)>& resolve_path,
            const char* project_relative_path,
            const char* source)
        {
            const wz::fs::Path full = resolve_path(project_relative_path);
            if (wz::fs::exists(full)) {
                return true;
            }

            // <project>/shaders/puppet — both files share this directory.
            const wz::fs::Path dir = resolve_path("shaders/puppet");
            if (wz::fs::create_directories(dir) != wz::fs::FileError::None) {
                logger.error(
                    "puppet program: failed to create shader directory: " + dir);
                return false;
            }
            if (wz::fs::write_file_text(full, source)
                != wz::fs::FileError::None)
            {
                logger.error(
                    "puppet program: failed to stage shader source: " + full);
                return false;
            }
            return true;
        }
    }

    CustomRenderProgramDesc puppet_program_srg_desc(const std::string& name)
    {
        // The fixed puppet SRG. Built imperatively (matching the on-device render
        // test) so the field order can't drift from the desc's declaration.
        // Shaders are left UNSET — the caller assigns vertex_shader/pixel_shader
        // (typed path) or wires them as compiler deps (graph path).
        CustomRenderProgramDesc desc{};
        desc.name = name;
        desc.binding_model = RenderBindingModel::MeshVertexPull;
        desc.topology = RenderPrimitiveTopology::TriangleList;
        desc.default_domain = RenderDomain::Opaque;
        desc.default_policy_flags = RenderPolicy_None;
        desc.input_layout = InputLayoutKind::None;
        // The atlas is resident premultiplied and the PS keeps it that way
        // (#277), so the "over" blend must be the premultiplied one -- AlphaBlend
        // would scale the already-scaled colour by alpha a second time.
        desc.blend_mode = wz::rhi::BlendMode::PremultipliedAlpha;
        desc.depth_mode = DepthMode::Disabled;
        desc.raster_mode = RasterMode::SolidCullNone;

        desc.root_constants.push_back(RootConstantBinding{
            .visibility = ShaderVisibility::All,
            .shader_register = 0,
            .register_space = 2,
            // 16 dwords = 4 float4: affine rows 0/1 (+opacity), tint, screen
            // tint. Must match the PuppetPartBlock packers in
            // rhi_scene_renderer.cpp (realize + per-frame).
            .value_count = 16,
            .semantic = "puppet_part",
        });
        desc.descriptor_bindings.push_back(DescriptorBinding{
            .kind = DescriptorKind::StructuredBufferSRV,
            .visibility = ShaderVisibility::Vertex,
            .semantic = DescriptorSemantic::ScreenConstants,
            .shader_register = 0,
            .register_space = 0,
            .descriptor_count = 1,
        });
        desc.descriptor_bindings.push_back(DescriptorBinding{
            .kind = DescriptorKind::StructuredBufferSRV,
            .visibility = ShaderVisibility::Vertex,
            .semantic = DescriptorSemantic::PuppetVertices,
            .shader_register = 0,
            .register_space = 2,
            .descriptor_count = 1,
        });
        desc.descriptor_bindings.push_back(DescriptorBinding{
            .kind = DescriptorKind::StructuredBufferSRV,
            .visibility = ShaderVisibility::Vertex,
            .semantic = DescriptorSemantic::PuppetIndices,
            .shader_register = 1,
            .register_space = 2,
            .descriptor_count = 1,
        });
        desc.descriptor_bindings.push_back(DescriptorBinding{
            .kind = DescriptorKind::TextureSRV,
            .visibility = ShaderVisibility::Pixel,
            .semantic = DescriptorSemantic::PuppetAtlas,
            .shader_register = 2,
            .register_space = 2,
            .descriptor_count = 1,
        });
        desc.static_samplers.push_back(StaticSamplerBinding{
            .kind = StaticSamplerKind::LinearClamp,
            .visibility = ShaderVisibility::Pixel,
            .shader_register = 0,
            .register_space = 2,
        });
        return desc;
    }

    bool stage_puppet_shaders(
        wz::Logger& logger,
        const std::function<wz::fs::Path(const wz::fs::Path&)>& resolve_path)
    {
        return stage_shader_source(
                   logger,
                   resolve_path,
                   kPuppetVertexShaderProjectPath,
                   kPuppetVsSource)
            && stage_shader_source(
                   logger,
                   resolve_path,
                   kPuppetPixelShaderProjectPath,
                   kPuppetPsSource);
    }

    RenderProgramAsset ensure_puppet_program(
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        ShaderAssetModule& shaders,
        RenderProgramAssetModule& render_programs)
    {
        if (!stage_puppet_shaders(
                logger,
                [&files](const wz::fs::Path& p) {
                    return files.resolve_path(p);
                }))
        {
            return {};
        }

        // One fixed program (no per-style variation), so a fixed name dedups it
        // to a single asset via create_custom's deterministic key.
        const std::string name = "puppet/program";

        const ShaderPairAsset shader_pair = shaders.create_shader_pair({
            .name = name,
            .vertex_path = kPuppetVertexShaderProjectPath,
            .pixel_path = kPuppetPixelShaderProjectPath,
            .vertex_entry = "main",
            .pixel_entry = "main",
            .vertex_target = "vs_5_1",
            .pixel_target = "ps_5_1",
        });
        if (!shader_pair.valid()) {
            logger.error("puppet program: shader pair registration failed");
            return {};
        }

        CustomRenderProgramDesc desc = puppet_program_srg_desc(name);
        desc.vertex_shader = shader_pair.vertex_shader;
        desc.pixel_shader = shader_pair.pixel_shader;
        return render_programs.create_custom(desc);
    }
}
