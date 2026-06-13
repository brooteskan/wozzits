#include "wz_test.h"

#include <wozzits/rhi/render_program.h>
#include <wozzits/rhi/render_program_registry.h>

// Bridge contract test.
//
// wozzits-rhi stays dependency-free: it does NOT include any engine header.
// The engine -> rhi adapter (which DOES know engine types) lives at the seam,
// not in this repo, and performs this mapping:
//
//   wz::rhi::RenderProgramDesc from_engine(
//       const wz::engine::assets::RenderProgramData& src,
//       wz::rhi::DescriptorSemanticRegistry&         semantics);
//
//   - src.builtin_program (the enum we are escaping) is DROPPED. The program's
//     identity becomes its registered name in RenderProgramRegistry.
//   - src.binding_model / topology / input_layout / blend / depth / raster map
//     1:1 to the rhi enums (closed value sets, intentionally enums).
//   - each src.descriptor_bindings[i].semantic (engine enum DescriptorSemantic)
//     becomes semantics.acquire("<name>") -> Tag (open set, intentionally a
//     registry, not an enum).
//   - shader AssetKeys become resolved shader refs (strings here).
//
// This test constructs what that adapter would OUTPUT for the real
// mesh_mask_style program (t0 = field values SRV, t1 = mask rules SRV) and
// proves the contract carries it faithfully through the registry.

using namespace wz::rhi;

namespace
{
    // Stand-in for the engine adapter's output. In production this is produced
    // by from_engine() at the boundary; here we build it by hand so the test
    // has zero engine dependency.
    RenderProgramDesc adapt_mesh_mask_style(DescriptorSemanticRegistry& semantics)
    {
        RenderProgramDesc desc;
        desc.name = "mesh_mask_style";
        desc.vertex_shader = "shaders/mesh_mask_style/mesh_mask_style_vs.hlsl";
        desc.pixel_shader  = "shaders/mesh_mask_style/mesh_mask_style_ps.hlsl";

        desc.vertex_source = VertexSource::InputAssembler;
        desc.vertex_layout.attributes = {
            VertexAttribute{ 0, VertexFormat::Float32x3, 0,  0, VertexStepRate::PerVertex },
            VertexAttribute{ 1, VertexFormat::Float32x3, 12, 0, VertexStepRate::PerVertex },
            VertexAttribute{ 2, VertexFormat::Float32x2, 24, 0, VertexStepRate::PerVertex },
        };
        desc.topology      = PrimitiveTopology::TriangleList;
        desc.blend_mode    = BlendMode::Opaque;
        desc.depth_mode    = DepthMode::TestWrite;
        desc.raster_mode   = RasterMode::SolidCullBack;

        // 48 root constants (transform + view_proj + mask params), matching the
        // mesh_mask_style root signature.
        desc.root_constants.push_back(RootConstantBinding{
            ShaderStage::All, /*reg*/ 0, /*space*/ 0, /*value_count*/ 48 });

        // t0: the face-domain field values. t1: the packed mask rules.
        // The engine's DescriptorSemantic enum entries become registered Tags.
        desc.descriptor_bindings.push_back(DescriptorBinding{
            DescriptorKind::StructuredBufferSRV, ShaderStage::Pixel,
            semantics.acquire("mesh_field_visualization"),
            /*reg*/ 0, /*space*/ 0, /*count*/ 1 });
        desc.descriptor_bindings.push_back(DescriptorBinding{
            DescriptorKind::StructuredBufferSRV, ShaderStage::Pixel,
            semantics.acquire("mesh_mask_rules"),
            /*reg*/ 1, /*space*/ 0, /*count*/ 1 });

        return desc;
    }
}

// A full, realistic program crosses the boundary and round-trips through the
// registry without loss.
static void mesh_mask_style_round_trips_through_registry()
{
    DescriptorSemanticRegistry semantics;
    RenderProgramRegistry programs;

    const Tag program = programs.register_program(adapt_mesh_mask_style(semantics));
    WZ_CHECK(program.valid());

    const RenderProgramDesc* desc = programs.get(programs.find("mesh_mask_style"));
    WZ_CHECK(desc != nullptr);
    if (!desc) {
        return;
    }

    WZ_CHECK_EQ(desc->blend_mode, BlendMode::Opaque);
    WZ_CHECK(desc->vertex_source == VertexSource::InputAssembler);
    WZ_CHECK_EQ(desc->vertex_layout.attributes.size(), static_cast<size_t>(3));
    WZ_CHECK_EQ(desc->root_constants.size(), static_cast<size_t>(1));
    WZ_CHECK_EQ(desc->descriptor_bindings.size(), static_cast<size_t>(2));
}

// Descriptor semantics survive as registered Tags, and resolve back to their
// names — the open identity set that used to be the DescriptorSemantic enum.
static void descriptor_semantics_resolve_by_name_not_enum()
{
    DescriptorSemanticRegistry semantics;
    RenderProgramRegistry programs;
    programs.register_program(adapt_mesh_mask_style(semantics));

    const RenderProgramDesc* desc = programs.get(programs.find("mesh_mask_style"));
    WZ_CHECK(desc != nullptr);
    if (!desc || desc->descriptor_bindings.size() < 2) {
        WZ_CHECK(false);
        return;
    }

    const Tag t0 = desc->descriptor_bindings[0].semantic;
    const Tag t1 = desc->descriptor_bindings[1].semantic;
    WZ_CHECK(t0.valid());
    WZ_CHECK(t1.valid());
    WZ_CHECK_FALSE(t0 == t1);
    WZ_CHECK_EQ(semantics.name_of(t0), std::string_view{ "mesh_field_visualization" });
    WZ_CHECK_EQ(semantics.name_of(t1), std::string_view{ "mesh_mask_rules" });
}

// A binding whose semantic was never registered carries a null Tag — a
// checkable error at bind time, not a silent default. (Adding a new render
// path with a new semantic cannot silently bind the wrong resource.)
static void unregistered_semantic_is_a_null_tag()
{
    DescriptorSemanticRegistry semantics;
    const Tag missing = semantics.find("never_registered_semantic");
    WZ_CHECK_FALSE(missing.valid());

    DescriptorBinding binding{};
    binding.semantic = missing;
    WZ_CHECK_FALSE(binding.semantic.valid());
}

int main()
{
    WZ_RUN(mesh_mask_style_round_trips_through_registry);
    WZ_RUN(descriptor_semantics_resolve_by_name_not_enum);
    WZ_RUN(unregistered_semantic_is_a_null_tag);
    WZ_TEST_RETURN();
}
