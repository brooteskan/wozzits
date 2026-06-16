#include "wz_test.h"

#include <wozzits/rhi/render_program.h>
#include <wozzits/rhi/render_program_registry.h>

#include <span>

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
//   - src SRG layouts become RenderProgramDesc::shader_resource_groups by
//     binding slot; descriptor and constant semantics are Tags inside the SRG.
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
    RenderProgramDesc adapt_mesh_mask_style(
        DescriptorSemanticRegistry& descriptors,
        ConstantSemanticRegistry& constants)
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

        // Object-frequency SRG: 48 dwords (transform + view_proj + mask params),
        // plus t0/t1 resources. The engine's DescriptorSemantic enum entries
        // become registered Tags inside this group layout.
        const RootConstantBinding object_constants{
            ShaderStage::All,
            constants.acquire("object_constants"),
            /*reg*/ 0, /*space*/ 2, /*value_count*/ 48 };

        ShaderResourceGroupLayout object_srg;
        object_srg.binding_slot = 2;
        const auto object_layout =
            make_constants_layout(
                std::span<const RootConstantBinding>{ &object_constants, 1 });
        WZ_CHECK(object_layout.has_value());
        if (object_layout) {
            object_srg.constants = *object_layout;
        }

        // t0: the face-domain field values. t1: the packed mask rules.
        object_srg.descriptors.push_back(DescriptorBinding{
            DescriptorKind::StructuredBufferSRV, ShaderStage::Pixel,
            descriptors.acquire("mesh_field_visualization"),
            /*reg*/ 0, /*space*/ 2, /*count*/ 1 });
        object_srg.descriptors.push_back(DescriptorBinding{
            DescriptorKind::StructuredBufferSRV, ShaderStage::Pixel,
            descriptors.acquire("mesh_mask_rules"),
            /*reg*/ 1, /*space*/ 2, /*count*/ 1 });
        desc.shader_resource_groups.push_back(object_srg);

        return desc;
    }
}

// A full, realistic program crosses the boundary and round-trips through the
// registry without loss.
static void mesh_mask_style_round_trips_through_registry()
{
    DescriptorSemanticRegistry semantics;
    ConstantSemanticRegistry constants;
    RenderProgramRegistry programs;

    const Tag program =
        programs.register_program(adapt_mesh_mask_style(semantics, constants));
    WZ_CHECK(program.valid());

    const RenderProgramDesc* desc = programs.get(programs.find("mesh_mask_style"));
    WZ_CHECK(desc != nullptr);
    if (!desc) {
        return;
    }

    WZ_CHECK_EQ(desc->blend_mode, BlendMode::Opaque);
    WZ_CHECK(desc->vertex_source == VertexSource::InputAssembler);
    WZ_CHECK_EQ(desc->vertex_layout.attributes.size(), static_cast<size_t>(3));
    WZ_CHECK_EQ(desc->shader_resource_groups.size(), static_cast<size_t>(1));
    const ShaderResourceGroupLayout* object_srg =
        find_shader_resource_group_layout(desc->shader_resource_groups, 2);
    WZ_CHECK(object_srg != nullptr);
    if (object_srg) {
        WZ_CHECK_EQ(object_srg->constants.dword_count(), 48u);
        WZ_CHECK_EQ(object_srg->descriptors.size(), static_cast<size_t>(2));
    }
}

// Descriptor semantics survive as registered Tags, and resolve back to their
// names — the open identity set that used to be the DescriptorSemantic enum.
static void descriptor_semantics_resolve_by_name_not_enum()
{
    DescriptorSemanticRegistry semantics;
    ConstantSemanticRegistry constants;
    RenderProgramRegistry programs;
    programs.register_program(adapt_mesh_mask_style(semantics, constants));

    const RenderProgramDesc* desc = programs.get(programs.find("mesh_mask_style"));
    WZ_CHECK(desc != nullptr);
    const ShaderResourceGroupLayout* object_srg =
        desc ? find_shader_resource_group_layout(desc->shader_resource_groups, 2)
             : nullptr;
    if (!object_srg || object_srg->descriptors.size() < 2) {
        WZ_CHECK(false);
        return;
    }

    const Tag t0 = object_srg->descriptors[0].semantic;
    const Tag t1 = object_srg->descriptors[1].semantic;
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
