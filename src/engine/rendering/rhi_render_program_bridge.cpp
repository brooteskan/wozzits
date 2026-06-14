#include <engine/rendering/rhi_render_program_bridge.h>

#include <cstdio>
#include <string>

namespace wz::engine::rendering
{
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
                // Per-instance splat attributes; refine to the exact packed
                // layout when the splat path is wired through rhi.
                layout.attributes = {
                    VA{ 0, VF::Float32x3, 0,  0, SR::PerInstance },  // position
                    VA{ 1, VF::Float32,   12, 0, SR::PerInstance },  // opacity
                    VA{ 2, VF::Float32x3, 16, 0, SR::PerInstance },  // scale
                    VA{ 3, VF::Float32x4, 28, 0, SR::PerInstance },  // rotation
                    VA{ 4, VF::Float32x4, 44, 0, SR::PerInstance },  // color
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

        std::string shader_ref(const wz::asset::AssetKey& key)
        {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "asset:%016llx%016llx",
                static_cast<unsigned long long>(key.content_hash.hi),
                static_cast<unsigned long long>(key.content_hash.lo));
            return std::string(buffer);
        }
    }

    wz::rhi::RenderProgramDesc to_rhi_render_program_desc(
        const ea::CustomRenderProgramDesc& src,
        wz::rhi::DescriptorSemanticRegistry& semantics)
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

        out.root_constants.reserve(src.root_constants.size());
        for (const ea::RootConstantBinding& rc : src.root_constants) {
            out.root_constants.push_back(wz::rhi::RootConstantBinding{
                map_visibility(rc.visibility),
                rc.shader_register,
                rc.register_space,
                rc.value_count });
        }

        out.descriptor_bindings.reserve(src.descriptor_bindings.size());
        for (const ea::DescriptorBinding& db : src.descriptor_bindings) {
            out.descriptor_bindings.push_back(wz::rhi::DescriptorBinding{
                map_descriptor_kind(db.kind),
                map_visibility(db.visibility),
                semantics.acquire(descriptor_semantic_name(db.semantic)),
                db.shader_register,
                db.register_space,
                db.descriptor_count });
        }

        return out;
    }
}
