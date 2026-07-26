// src/engine/assets/puppet_program.cpp

#include <engine/assets/puppet_program.h>

#include <engine/assets/render_program/render_program.h>
#include <engine/assets/schema_ids.h>

#include <file/filesystem.h>

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <vector>

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
    float4 part_mask;
    uint4  part_pull;
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
    float2 mask_uv : TEXCOORD1;
};

VSOut main(uint vid : SV_VertexID)
{
    uint           idx = indices[part_pull.y + vid];
    WzPuppetVertex v   = vertices[part_pull.x + idx];
    float2 px = float2(
        xform_row0.x * v.pos.x + xform_row0.y * v.pos.y + xform_row0.z,
        xform_row1.x * v.pos.x + xform_row1.y * v.pos.y + xform_row1.z);
    float2 vp  = screen_constants[0].viewport.xy;
    float2 ndc = px * (2.0f / vp) - 1.0f;
    VSOut o;
    o.pos     = float4(ndc.x, -ndc.y, 0.0f, 1.0f);
    o.uv      = v.uv;
    o.mask_uv = px / vp;
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
    float4 part_mask;
    uint4  part_pull;
};

Texture2D<float4> atlas   : register(t2, space2);
Texture2D<float4> mask    : register(t3, space2);
SamplerState      atlas_s : register(s0, space2);

struct PSIn
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float2 mask_uv  : TEXCOORD1;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 tex = atlas.SampleLevel(atlas_s, input.uv, 0.0f);
    float3 rgb = tex.rgb * part_tint.rgb;
    rgb = rgb + part_screen_tint.rgb * (tex.a - rgb);
    float coverage = mask.SampleLevel(atlas_s, input.mask_uv, 0.0f).a;
    float covered  = step(part_mask.x, coverage);
    float keep     = lerp(covered, 1.0f - covered, part_mask.y);
    float scale = xform_row0.w * keep;
    return float4(rgb * scale, tex.a * scale);
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

    wz::rhi::BlendMode rhi_blend_for(PuppetProgramBlend blend)
    {
        switch (blend) {
        case PuppetProgramBlend::Multiply: return wz::rhi::BlendMode::Multiply;
        case PuppetProgramBlend::Screen:   return wz::rhi::BlendMode::Screen;
        case PuppetProgramBlend::Normal:   break;
        }
        return wz::rhi::BlendMode::PremultipliedAlpha;
    }

    PuppetProgramBlend puppet_program_blend_for_part(inochi::BlendMode part_blend)
    {
        switch (part_blend) {
        case inochi::BlendMode::Multiply: return PuppetProgramBlend::Multiply;
        case inochi::BlendMode::Screen:   return PuppetProgramBlend::Screen;
        default: break;
        }
        // Normal, the masks (S5) and every destination-reading mode (S6) draw
        // through the plain premultiplied "over" until their seams land.
        return PuppetProgramBlend::Normal;
    }

    PuppetProgramVariants puppet_program_variants(
        const wz::asset::AssetSystem& system,
        const wz::asset::AssetKey& base)
    {
        PuppetProgramVariants out{};
        if (base == wz::asset::AssetKey{}) {
            return out;
        }
        // Whatever else happens, the caller can always draw with `base`.
        out.by_blend[static_cast<std::size_t>(PuppetProgramBlend::Normal)] = base;

        const std::span<const wz::asset::AssetSystem::RegistrationEntry>
            registered = system.registered_assets();

        // The base's shader deps identify the variant FAMILY: two puppets in one
        // project that somehow resolved different shaders must not share a set.
        const auto base_entry = std::ranges::find_if(
            registered,
            [&base](const wz::asset::AssetSystem::RegistrationEntry& e) {
                return e.node.key == base;
            });
        if (base_entry == registered.end()) {
            return out;
        }
        const std::vector<wz::asset::AssetKey>& family = base_entry->dep_keys;

        for (const wz::asset::AssetSystem::RegistrationEntry& e : registered) {
            if (e.node.schema.value != kPuppetProgramSchema.value
                || e.dep_keys != family)
            {
                continue;
            }
            // The declared blend_mode param rides in the node's meta ParamBlock
            // (a compiler only gets meta when it declares .parameters -- see the
            // kPuppetProgramSchema registration). A node without it is a
            // pre-#274 program, i.e. Normal.
            PuppetProgramBlend blend = PuppetProgramBlend::Normal;
            if (const auto* params =
                    std::any_cast<wz::asset::ParamBlock>(&e.node.meta))
            {
                const int64_t raw = params->get<int64_t>(
                    kPuppetProgramBlendParam,
                    static_cast<int64_t>(PuppetProgramBlend::Normal));
                if (raw >= 0
                    && raw < static_cast<int64_t>(kPuppetProgramBlendCount))
                {
                    blend = static_cast<PuppetProgramBlend>(raw);
                }
            }
            out.by_blend[static_cast<std::size_t>(blend)] = e.node.key;
        }
        return out;
    }

    CustomRenderProgramDesc puppet_program_srg_desc(
        const std::string& name,
        PuppetProgramBlend blend)
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
        // The only thing that varies across the puppet program set (#274). The
        // atlas is resident premultiplied and the PS keeps it that way (#277),
        // so Normal is the PREMULTIPLIED "over" -- AlphaBlend would scale the
        // already-scaled colour by alpha a second time -- and Multiply/Screen
        // are the coverage-aware rhi recipes, which the premultiplied source is
        // likewise a precondition for.
        desc.blend_mode = rhi_blend_for(blend);
        desc.depth_mode = DepthMode::Disabled;
        desc.raster_mode = RasterMode::SolidCullNone;

        desc.root_constants.push_back(RootConstantBinding{
            .visibility = ShaderVisibility::All,
            .shader_register = 0,
            .register_space = 2,
            // 24 dwords = 6 rows: affine rows 0/1 (+opacity), tint, screen
            // tint, mask params, shared-buffer pull bases. Must match the
            // PuppetPartBlock packers in rhi_scene_renderer.cpp (realize +
            // per-frame).
            .value_count = 24,
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
        // The Part's coverage mask (#275). Declared on EVERY variant, so an
        // unmasked Part binds the puppet's 1x1 white texture rather than needing
        // a second program set without this row.
        desc.descriptor_bindings.push_back(DescriptorBinding{
            .kind = DescriptorKind::TextureSRV,
            .visibility = ShaderVisibility::Pixel,
            .semantic = DescriptorSemantic::PuppetMask,
            .shader_register = 3,
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

    const char* puppet_program_name(PuppetProgramBlend blend)
    {
        switch (blend) {
        case PuppetProgramBlend::Multiply: return "puppet/program/multiply";
        case PuppetProgramBlend::Screen:   return "puppet/program/screen";
        case PuppetProgramBlend::Normal:   break;
        }
        return "puppet/program";
    }

    RenderProgramAsset ensure_puppet_program(
        wz::Logger& logger,
        FileCarrierAssetModule& files,
        ShaderAssetModule& shaders,
        RenderProgramAssetModule& render_programs,
        PuppetProgramVariants* out_variants)
    {
        if (out_variants) {
            *out_variants = PuppetProgramVariants{};
        }
        if (!stage_puppet_shaders(
                logger,
                [&files](const wz::fs::Path& p) {
                    return files.resolve_path(p);
                }))
        {
            return {};
        }

        // ONE shader pair shared by every variant -- they differ only in the
        // PSO's blend state, so re-registering the shaders per variant would
        // just duplicate compiles.
        const ShaderPairAsset shader_pair = shaders.create_shader_pair({
            .name = puppet_program_name(PuppetProgramBlend::Normal),
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

        // One program per blend variant (#274); create_custom mixes blend_mode
        // into its key, so each is a distinct, deduped asset.
        RenderProgramAsset normal{};
        for (std::size_t i = 0; i < kPuppetProgramBlendCount; ++i) {
            const auto blend = static_cast<PuppetProgramBlend>(i);
            CustomRenderProgramDesc desc =
                puppet_program_srg_desc(puppet_program_name(blend), blend);
            desc.vertex_shader = shader_pair.vertex_shader;
            desc.pixel_shader = shader_pair.pixel_shader;

            const RenderProgramAsset program = render_programs.create_custom(desc);
            if (!program.valid()) {
                logger.error(
                    std::string("puppet program: variant creation failed: ")
                    + puppet_program_name(blend));
                return {};
            }
            if (out_variants) {
                out_variants->by_blend[i] = program.key;
            }
            if (blend == PuppetProgramBlend::Normal) {
                normal = program;
            }
        }
        return normal;
    }
}
