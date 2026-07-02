#include <engine/rendering/rhi_render_program_bridge.h>

#include <engine/assets/engine_asset_key_core.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace wz::engine::rendering
{
    std::string shader_ref(const wz::asset::AssetKey& key);

    namespace
    {
        namespace ea = wz::engine::assets;

        // Closed-enum maps. No `default:` — a new engine enum member must break
        // these switches at compile time (-Wswitch), the exhaustiveness side of
        // the "no enums is precise" rule. The trailing return is unreachable.

        wz::rhi::ShaderStage map_visibility(ea::ShaderVisibility v)
        {
            switch (v) {
            case ea::ShaderVisibility::All:    return wz::rhi::ShaderStage::All;
            case ea::ShaderVisibility::Vertex: return wz::rhi::ShaderStage::Vertex;
            case ea::ShaderVisibility::Pixel:  return wz::rhi::ShaderStage::Pixel;
            }
            return wz::rhi::ShaderStage::All;
        }

        // The engine's content-named BindingModel maps onto rhi's small,
        // structural VertexSource. Several engine renderables share one
        // structural strategy (Mesh and instanced-splat are both IA-sourced).
        wz::rhi::VertexSource map_vertex_source(ea::RenderBindingModel m)
        {
            switch (m) {
            case ea::RenderBindingModel::MeshIA:
            case ea::RenderBindingModel::SplatVertexInstanced:
                return wz::rhi::VertexSource::InputAssembler;
            case ea::RenderBindingModel::SplatPull:
            case ea::RenderBindingModel::MeshVertexPull:
            case ea::RenderBindingModel::ParticlePull:
                return wz::rhi::VertexSource::Pull;
            case ea::RenderBindingModel::ScalarFieldTexture:
            case ea::RenderBindingModel::Fullscreen:
                return wz::rhi::VertexSource::None;
            }
            return wz::rhi::VertexSource::InputAssembler;
        }

        wz::rhi::PrimitiveTopology map_topology(ea::RenderPrimitiveTopology t)
        {
            switch (t) {
            case ea::RenderPrimitiveTopology::TriangleList:
                return wz::rhi::PrimitiveTopology::TriangleList;
            case ea::RenderPrimitiveTopology::TriangleStrip:
                return wz::rhi::PrimitiveTopology::TriangleStrip;
            }
            return wz::rhi::PrimitiveTopology::TriangleList;
        }

        // This is the ONE place the engine's InputLayoutKind enum gets expanded
        // into rhi's data-driven VertexLayout. rhi never gains a per-format enum
        // member; a new engine layout adds a case here only.
        wz::rhi::VertexLayout vertex_layout_for(ea::InputLayoutKind k)
        {
            using VF = wz::rhi::VertexFormat;
            using VA = wz::rhi::VertexAttribute;
            using SR = wz::rhi::VertexStepRate;

            wz::rhi::VertexLayout layout;
            switch (k) {
            case ea::InputLayoutKind::None:
                break;
            case ea::InputLayoutKind::MeshPositionOnly:
                layout.attributes = {
                    VA{ 0, VF::Float32x3, 0, 0, SR::PerVertex },
                };
                break;
            case ea::InputLayoutKind::MeshPositionNormalUV:
                layout.attributes = {
                    VA{ 0, VF::Float32x3, 0,  0, SR::PerVertex },  // position
                    VA{ 1, VF::Float32x3, 12, 0, SR::PerVertex },  // normal
                    VA{ 2, VF::Float32x2, 24, 0, SR::PerVertex },  // uv0
                };
                break;
            case ea::InputLayoutKind::GaussianSplatVertex:
                // Per-instance splat attributes for the IA-sourced splat path
                // (SplatVertexInstanced). This is the 64-byte
                // DX12GaussianSplatVertex stride: position(0) opacity(12)
                // scale(16) pad0(28) rotation(32) color(48) lod(60). The RHI
                // SplatPull path (issue #208) does NOT use this IA layout — it
                // reads the same 64-byte record from a StructuredBuffer at the
                // SplatCloud semantic (binding_layout==3) with
                // InputLayoutKind::None, so no IA vertex layout is bound there.
                layout.attributes = {
                    VA{ 0, VF::Float32x3, 0,  0, SR::PerInstance },  // position
                    VA{ 1, VF::Float32,   12, 0, SR::PerInstance },  // opacity
                    VA{ 2, VF::Float32x3, 16, 0, SR::PerInstance },  // scale
                    VA{ 3, VF::Float32x4, 32, 0, SR::PerInstance },  // rotation
                    VA{ 4, VF::Float32x3, 48, 0, SR::PerInstance },  // color
                };
                break;
            }
            return layout;
        }

        wz::rhi::BlendMode map_blend(ea::BlendMode b)
        {
            switch (b) {
            case ea::BlendMode::Opaque:     return wz::rhi::BlendMode::Opaque;
            case ea::BlendMode::AlphaBlend: return wz::rhi::BlendMode::AlphaBlend;
            }
            return wz::rhi::BlendMode::Opaque;
        }

        wz::rhi::DepthMode map_depth(ea::DepthMode d)
        {
            switch (d) {
            case ea::DepthMode::Disabled:    return wz::rhi::DepthMode::Disabled;
            case ea::DepthMode::TestNoWrite: return wz::rhi::DepthMode::TestNoWrite;
            case ea::DepthMode::TestWrite:   return wz::rhi::DepthMode::TestWrite;
            }
            return wz::rhi::DepthMode::Disabled;
        }

        wz::rhi::RasterMode map_raster(ea::RasterMode r)
        {
            switch (r) {
            case ea::RasterMode::SolidCullBack:
                return wz::rhi::RasterMode::SolidCullBack;
            case ea::RasterMode::SolidCullNone:
                return wz::rhi::RasterMode::SolidCullNone;
            case ea::RasterMode::WireframeCullNone:
                return wz::rhi::RasterMode::WireframeCullNone;
            }
            return wz::rhi::RasterMode::SolidCullBack;
        }

        wz::rhi::DescriptorKind map_descriptor_kind(ea::DescriptorKind k)
        {
            switch (k) {
            case ea::DescriptorKind::StructuredBufferSRV:
                return wz::rhi::DescriptorKind::StructuredBufferSRV;
            case ea::DescriptorKind::TextureSRV:
                return wz::rhi::DescriptorKind::TextureSRV;
            case ea::DescriptorKind::Sampler:
                return wz::rhi::DescriptorKind::Sampler;
            case ea::DescriptorKind::UAV:
                return wz::rhi::DescriptorKind::UAV;
            }
            return wz::rhi::DescriptorKind::StructuredBufferSRV;
        }

        wz::rhi::StaticSamplerKind map_static_sampler_kind(
            ea::StaticSamplerKind k)
        {
            switch (k) {
            case ea::StaticSamplerKind::LinearClamp:
                return wz::rhi::StaticSamplerKind::LinearClamp;
            }
            return wz::rhi::StaticSamplerKind::LinearClamp;
        }

        // The engine's DescriptorSemantic enum -> a stable name. The rhi side
        // registers that name as a Tag, dissolving the enum into an open
        // identity set that new render paths extend without editing a central
        // enum that many sites switch on.
        const char* descriptor_semantic_name(ea::DescriptorSemantic s)
        {
            switch (s) {
            case ea::DescriptorSemantic::Unknown:
                return "unknown";
            case ea::DescriptorSemantic::SplatCloud:
                return "splat_cloud";
            case ea::DescriptorSemantic::SortedSplatIndices:
                return "sorted_splat_indices";
            case ea::DescriptorSemantic::ScalarFieldTexture:
                return "scalar_field_texture";
            case ea::DescriptorSemantic::MeshFieldVisualization:
                return "mesh_field_visualization";
            case ea::DescriptorSemantic::MeshMaskRules:
                return "mesh_mask_rules";
            case ea::DescriptorSemantic::PulledMeshPositions:
                return "pulled_mesh_positions";
            case ea::DescriptorSemantic::PulledMeshIndices:
                return "pulled_mesh_indices";
            case ea::DescriptorSemantic::PulledMeshSourceVertices:
                return "pulled_mesh_source_vertices";
            }
            return "unknown";
        }

        struct RenderProgramBridgeSource
        {
            std::string name;
            wz::asset::AssetKey vertex_shader;
            wz::asset::AssetKey pixel_shader;
            ea::RenderBindingModel binding_model{};
            ea::RenderPrimitiveTopology topology{};
            ea::InputLayoutKind input_layout{};
            ea::BlendMode blend_mode{};
            ea::DepthMode depth_mode{};
            ea::RasterMode raster_mode{};
            std::span<const ea::RootConstantBinding> root_constants;
            std::span<const ea::DescriptorBinding> descriptor_bindings;
            std::span<const ea::StaticSamplerBinding> static_samplers;
        };

        std::optional<wz::rhi::RenderProgramDesc>
        convert_render_program_desc(
            const RenderProgramBridgeSource& src,
            wz::rhi::DescriptorSemanticRegistry& descriptors,
            wz::rhi::ConstantSemanticRegistry& constants)
        {
            wz::rhi::RenderProgramDesc out;
            out.name = src.name;
            out.vertex_shader = shader_ref(src.vertex_shader);
            out.pixel_shader = shader_ref(src.pixel_shader);

            out.vertex_source = map_vertex_source(src.binding_model);
            out.vertex_layout = vertex_layout_for(src.input_layout);
            out.topology      = map_topology(src.topology);
            out.blend_mode    = map_blend(src.blend_mode);
            out.depth_mode    = map_depth(src.depth_mode);
            out.raster_mode   = map_raster(src.raster_mode);

            std::vector<uint32_t> register_spaces;
            register_spaces.reserve(
                src.root_constants.size() + src.descriptor_bindings.size());
            for (const ea::RootConstantBinding& rc : src.root_constants) {
                register_spaces.push_back(rc.register_space);
            }
            for (const ea::DescriptorBinding& db : src.descriptor_bindings) {
                register_spaces.push_back(db.register_space);
            }
            for (const ea::StaticSamplerBinding& ss : src.static_samplers) {
                register_spaces.push_back(ss.register_space);
            }
            std::sort(register_spaces.begin(), register_spaces.end());
            register_spaces.erase(
                std::unique(register_spaces.begin(), register_spaces.end()),
                register_spaces.end());

            out.shader_resource_groups.reserve(register_spaces.size());
            for (const uint32_t space : register_spaces) {
                wz::rhi::ShaderResourceGroupLayout layout;
                layout.binding_slot = space;

                for (const ea::DescriptorBinding& db : src.descriptor_bindings) {
                    if (db.register_space != space) {
                        continue;
                    }
                    if (db.semantic == ea::DescriptorSemantic::Unknown) {
                        return std::nullopt;
                    }
                    const wz::rhi::Tag semantic =
                        descriptors.acquire(descriptor_semantic_name(db.semantic));
                    if (!semantic.valid()) {
                        return std::nullopt;
                    }
                    layout.descriptors.push_back(wz::rhi::DescriptorBinding{
                        map_descriptor_kind(db.kind),
                        map_visibility(db.visibility),
                        semantic,
                        db.shader_register,
                        db.register_space,
                        db.descriptor_count });
                }

                for (const ea::StaticSamplerBinding& ss : src.static_samplers) {
                    if (ss.register_space != space) {
                        continue;
                    }
                    layout.static_samplers.push_back(
                        wz::rhi::StaticSamplerBinding{
                            map_static_sampler_kind(ss.kind),
                            map_visibility(ss.visibility),
                            ss.shader_register,
                            ss.register_space });
                }

                for (const ea::RootConstantBinding& rc : src.root_constants) {
                    if (rc.register_space != space) {
                        continue;
                    }
                    const wz::rhi::RootConstantsBinding binding{
                        map_visibility(rc.visibility),
                        rc.shader_register,
                        rc.register_space
                    };
                    if (layout.constants.empty()) {
                        layout.constants_binding = binding;
                    }
                    else if (!(layout.constants_binding == binding)) {
                        return std::nullopt;
                    }
                    if (rc.semantic.empty()
                        || !layout.constants.append(
                            constants.acquire(rc.semantic),
                            rc.value_count * sizeof(uint32_t)))
                    {
                        return std::nullopt;
                    }
                }

                out.shader_resource_groups.push_back(layout);
            }

            return out;
        }

    }

    // Project the FULL asset identity into the rhi module/program name. Folding
    // ALL FOUR component hashes (content+schema+compiler+deps) is required, not
    // just content_hash: an HLSL shader key puts only (entry, target, stage) in
    // content_hash and the source-file identity in deps_hash (make_hlsl_shader_
    // key). Truncating to content_hash made every main/vs_5_1 (and main/ps_5_1)
    // shader alias to a single tag, so distinct shaders collided in the last-
    // writer-wins ShaderModuleRegistry and a program silently bound whichever
    // shader compiled last. key_to_dep_hash mixes all four hashes into one 128-
    // bit value, so the ref is 1:1 with the asset key (same 32-hex width as
    // before; refs are runtime-only, nothing persisted depends on the format).
    std::string shader_ref(const wz::asset::AssetKey& key)
    {
        const wz::asset::Hash id =
            wz::engine::assets::detail::key_to_dep_hash(key);
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "asset:%016llx%016llx",
            static_cast<unsigned long long>(id.hi),
            static_cast<unsigned long long>(id.lo));
        return std::string(buffer);
    }

    std::string program_ref(const wz::asset::AssetKey& key)
    {
        const wz::asset::Hash id =
            wz::engine::assets::detail::key_to_dep_hash(key);
        char buffer[72];
        std::snprintf(buffer, sizeof(buffer), "program#%016llx%016llx",
            static_cast<unsigned long long>(id.hi),
            static_cast<unsigned long long>(id.lo));
        return std::string(buffer);
    }

    std::optional<std::vector<uint8_t>> compile_hlsl_bytecode(
        std::span<const uint8_t> source,
        std::string_view entry,
        const char* target,
        wz::Logger& logger)
    {
        ID3DBlob* shader_blob = nullptr;
        ID3DBlob* error_blob = nullptr;
        const std::string entry_text =
            entry.empty() ? std::string("main") : std::string(entry);

        const HRESULT hr = D3DCompile(
            source.data(), source.size(), nullptr, nullptr, nullptr,
            entry_text.c_str(), target, 0, 0, &shader_blob, &error_blob);

        if (FAILED(hr)) {
            std::string detail;
            if (error_blob) {
                detail.assign(
                    static_cast<const char*>(error_blob->GetBufferPointer()),
                    error_blob->GetBufferSize());
                error_blob->Release();
            }
            if (shader_blob) {
                shader_blob->Release();
            }
            logger.error(
                std::string("rhi render-program: HLSL compile failed target=")
                + target + " entry=" + entry_text
                + (detail.empty() ? std::string{} : " error=" + detail));
            return std::nullopt;
        }
        if (error_blob) {
            error_blob->Release();
        }
        if (!shader_blob || shader_blob->GetBufferSize() == 0u) {
            if (shader_blob) {
                shader_blob->Release();
            }
            return std::nullopt;
        }
        std::vector<uint8_t> bytecode(shader_blob->GetBufferSize());
        std::memcpy(
            bytecode.data(),
            shader_blob->GetBufferPointer(),
            shader_blob->GetBufferSize());
        shader_blob->Release();
        return bytecode;
    }

    std::optional<wz::rhi::RenderProgramDesc> to_rhi_render_program_desc(
        const ea::CustomRenderProgramDesc& src,
        wz::rhi::DescriptorSemanticRegistry& descriptors,
        wz::rhi::ConstantSemanticRegistry& constants)
    {
        return convert_render_program_desc(
            RenderProgramBridgeSource{
                src.name,
                src.vertex_shader,
                src.pixel_shader,
                src.binding_model,
                src.topology,
                src.input_layout,
                src.blend_mode,
                src.depth_mode,
                src.raster_mode,
                src.root_constants,
                src.descriptor_bindings,
                src.static_samplers },
            descriptors,
            constants);
    }

    std::optional<wz::rhi::RenderProgramDesc> to_rhi_render_program_desc(
        const ea::RenderProgramData& data,
        const wz::asset::AssetKey& program_key,
        const wz::asset::AssetKey& vertex_key,
        const wz::asset::AssetKey& pixel_key,
        wz::rhi::DescriptorSemanticRegistry& descriptors,
        wz::rhi::ConstantSemanticRegistry& constants)
    {
        return convert_render_program_desc(
            RenderProgramBridgeSource{
                program_ref(program_key),
                vertex_key,
                pixel_key,
                data.binding_model,
                data.topology,
                data.input_layout,
                data.blend_mode,
                data.depth_mode,
                data.raster_mode,
                data.root_constants,
                data.descriptor_bindings,
                data.static_samplers },
            descriptors,
            constants);
    }
}
