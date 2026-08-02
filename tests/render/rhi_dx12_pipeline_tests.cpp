#include <gtest/gtest.h>

#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/rhi_render_program_bridge.h>

#include <cstddef>
#include <span>
#include <vector>

namespace ea = wz::engine::assets;

namespace
{
    ea::CustomRenderProgramDesc make_pull_cube_program()
    {
        ea::CustomRenderProgramDesc desc;
        desc.name = "pull_cube";
        desc.binding_model = ea::RenderBindingModel::MeshVertexPull;
        desc.input_layout = ea::InputLayoutKind::None;
        desc.topology = ea::RenderPrimitiveTopology::TriangleList;
        desc.blend_mode = wz::rhi::BlendMode::Opaque;
        desc.depth_mode = ea::DepthMode::TestWrite;
        desc.raster_mode = ea::RasterMode::SolidCullNone;
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV,
            ea::ShaderVisibility::Vertex,
            ea::DescriptorSemantic::PulledMeshPositions,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        desc.descriptor_bindings.push_back(ea::DescriptorBinding{
            ea::DescriptorKind::StructuredBufferSRV,
            ea::ShaderVisibility::Vertex,
            ea::DescriptorSemantic::PulledMeshIndices,
            /*shader_register*/ 1,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        desc.root_constants.push_back(ea::RootConstantBinding{
            ea::ShaderVisibility::Vertex,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*value_count*/ 16,
            "mvp" });
        return desc;
    }

    void expect_layouts_equal(
        const wz::engine::rendering::RhiDx12PipelineLayout& lhs,
        const wz::engine::rendering::RhiDx12PipelineLayout& rhs)
    {
        ASSERT_EQ(lhs.descriptor_tables.size(), rhs.descriptor_tables.size());
        for (size_t i = 0; i < lhs.descriptor_tables.size(); ++i) {
            EXPECT_EQ(
                lhs.descriptor_tables[i].binding_slot,
                rhs.descriptor_tables[i].binding_slot);
            EXPECT_EQ(
                lhs.descriptor_tables[i].root_parameter_index,
                rhs.descriptor_tables[i].root_parameter_index);
            EXPECT_EQ(
                lhs.descriptor_tables[i].descriptor_count,
                rhs.descriptor_tables[i].descriptor_count);
            EXPECT_EQ(
                lhs.descriptor_tables[i].descriptor_kinds,
                rhs.descriptor_tables[i].descriptor_kinds);
        }

        EXPECT_EQ(lhs.root_constants.valid, rhs.root_constants.valid);
        EXPECT_EQ(
            lhs.root_constants.root_parameter_index,
            rhs.root_constants.root_parameter_index);
        EXPECT_EQ(
            lhs.root_constants.dword_count,
            rhs.root_constants.dword_count);
        EXPECT_EQ(
            lhs.root_constants.shader_register,
            rhs.root_constants.shader_register);
        EXPECT_EQ(
            lhs.root_constants.register_space,
            rhs.root_constants.register_space);
        EXPECT_EQ(
            lhs.root_constants.visibility,
            rhs.root_constants.visibility);
    }
}

TEST(RhiDx12Pipeline, PullCubeRootPlanMatchesShaderContract)
{
    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            make_pull_cube_program(),
            descriptors,
            constants);
    ASSERT_TRUE(converted.has_value());

    const auto plan =
        wz::engine::rendering::plan_dx12_pipeline_layout(*converted);
    ASSERT_TRUE(plan.has_value());

    ASSERT_EQ(plan->descriptor_tables.size(), 1u);
    EXPECT_EQ(plan->descriptor_tables[0].binding_slot, 2u);
    EXPECT_EQ(plan->descriptor_tables[0].root_parameter_index, 0u);
    EXPECT_EQ(plan->descriptor_tables[0].descriptor_count, 2u);
    ASSERT_EQ(plan->descriptor_tables[0].descriptor_kinds.size(), 2u);
    EXPECT_EQ(
        plan->descriptor_tables[0].descriptor_kinds[0],
        wz::rhi::DescriptorKind::StructuredBufferSRV);
    EXPECT_EQ(
        plan->descriptor_tables[0].descriptor_kinds[1],
        wz::rhi::DescriptorKind::StructuredBufferSRV);
    ASSERT_TRUE(plan->root_param_for_slot(2).has_value());
    EXPECT_EQ(*plan->root_param_for_slot(2), 0u);

    EXPECT_TRUE(plan->root_constants.valid);
    EXPECT_EQ(plan->root_constants.root_parameter_index, 1u);
    EXPECT_EQ(plan->root_constants.dword_count, 16u);
    EXPECT_EQ(plan->root_constants.shader_register, 0u);
    EXPECT_EQ(plan->root_constants.register_space, 2u);
    EXPECT_EQ(plan->root_constants.visibility, wz::rhi::ShaderStage::Vertex);
}

TEST(RhiDx12Pipeline, StaticSamplerFlowsThroughBridgeOntoObjectSrg)
{
    // A clipmap-style program: pull SRVs + a height texture on the object SRG
    // (space2) PLUS a static linear-clamp sampler at s0. The sampler must ride
    // through the bridge onto the rhi SRG (baked into the root signature by the
    // backend; it consumes no descriptor-table slot), and the descriptor-table
    // plan must be unaffected -- still just the two SRVs + the texture.
    ea::CustomRenderProgramDesc desc = make_pull_cube_program();
    desc.name = "clipmap_like_with_sampler";
    desc.descriptor_bindings.push_back(ea::DescriptorBinding{
        ea::DescriptorKind::TextureSRV,
        ea::ShaderVisibility::Vertex,
        ea::DescriptorSemantic::ScalarFieldTexture,
        /*shader_register*/ 2,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });
    desc.static_samplers.push_back(ea::StaticSamplerBinding{
        ea::StaticSamplerKind::LinearClamp,
        ea::ShaderVisibility::Vertex,
        /*shader_register*/ 0,
        /*register_space*/ 2 });

    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            desc, descriptors, constants);
    ASSERT_TRUE(converted.has_value());

    // The object SRG (slot 2) carries the static sampler verbatim.
    const wz::rhi::ShaderResourceGroupLayout* object =
        wz::rhi::find_shader_resource_group_layout(
            converted->shader_resource_groups, 2);
    ASSERT_NE(object, nullptr);
    ASSERT_EQ(object->static_samplers.size(), 1u);
    EXPECT_EQ(
        object->static_samplers[0].kind,
        wz::rhi::StaticSamplerKind::LinearClamp);
    EXPECT_EQ(
        object->static_samplers[0].visibility,
        wz::rhi::ShaderStage::Vertex);
    EXPECT_EQ(object->static_samplers[0].shader_register, 0u);
    EXPECT_EQ(object->static_samplers[0].register_space, 2u);

    // The descriptor-table plan is unchanged by the static sampler: the object
    // table still holds exactly the three SRV/texture descriptors.
    const auto plan =
        wz::engine::rendering::plan_dx12_pipeline_layout(*converted);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->descriptor_tables.size(), 1u);
    EXPECT_EQ(plan->descriptor_tables[0].descriptor_count, 3u);
}

// #201 step 4: the filtered-wrap sampler recipe an equirect environment map /
// tiling texture needs rides the SAME generic static-sampler seam as the
// clipmap's LinearClamp, mapping 1:1 through the bridge onto the rhi SRG.
TEST(RhiDx12Pipeline, LinearWrapStaticSamplerFlowsThroughBridge)
{
    ea::CustomRenderProgramDesc desc = make_pull_cube_program();
    desc.name = "environment_like_with_wrap_sampler";
    desc.descriptor_bindings.push_back(ea::DescriptorBinding{
        ea::DescriptorKind::TextureSRV,
        ea::ShaderVisibility::Pixel,
        ea::DescriptorSemantic::ScalarFieldTexture,
        /*shader_register*/ 2,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });
    desc.static_samplers.push_back(ea::StaticSamplerBinding{
        ea::StaticSamplerKind::LinearWrap,
        ea::ShaderVisibility::Pixel,
        /*shader_register*/ 0,
        /*register_space*/ 2 });

    wz::rhi::DescriptorSemanticRegistry descriptors;
    wz::rhi::ConstantSemanticRegistry constants;
    const auto converted =
        wz::engine::rendering::to_rhi_render_program_desc(
            desc, descriptors, constants);
    ASSERT_TRUE(converted.has_value());

    const wz::rhi::ShaderResourceGroupLayout* object =
        wz::rhi::find_shader_resource_group_layout(
            converted->shader_resource_groups, 2);
    ASSERT_NE(object, nullptr);
    ASSERT_EQ(object->static_samplers.size(), 1u);
    EXPECT_EQ(
        object->static_samplers[0].kind,
        wz::rhi::StaticSamplerKind::LinearWrap);
    EXPECT_EQ(
        object->static_samplers[0].visibility,
        wz::rhi::ShaderStage::Pixel);
}

TEST(RhiDx12Pipeline, ComputeLayoutMatchesRenderLayoutForIdenticalSrgs)
{
    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag descriptor = tags.acquire("field_output");
    const wz::rhi::Tag constants = tags.acquire("dispatch_constants");
    ASSERT_TRUE(descriptor.valid());
    ASSERT_TRUE(constants.valid());

    wz::rhi::ShaderResourceGroupLayout slot_2;
    slot_2.binding_slot = 2;
    slot_2.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::UAV,
        wz::rhi::ShaderStage::All,
        descriptor,
        /*shader_register*/ 0,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });
    slot_2.constants_binding = wz::rhi::RootConstantsBinding{
        wz::rhi::ShaderStage::All,
        /*shader_register*/ 0,
        /*register_space*/ 2 };
    ASSERT_TRUE(slot_2.constants.append(constants, 16));

    wz::rhi::RenderProgramDesc render_program;
    render_program.shader_resource_groups = { slot_2 };
    wz::rhi::ComputeProgramDesc compute_program;
    compute_program.shader_resource_groups =
        render_program.shader_resource_groups;

    const auto render_plan =
        wz::engine::rendering::plan_dx12_pipeline_layout(render_program);
    const auto compute_plan =
        wz::engine::rendering::plan_dx12_pipeline_layout(compute_program);
    ASSERT_TRUE(render_plan.has_value());
    ASSERT_TRUE(compute_plan.has_value());

    expect_layouts_equal(*render_plan, *compute_plan);
}

TEST(RhiDx12Pipeline, DescriptorTablesAreBeforeRootConstantsInSlotOrder)
{
    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag a = tags.acquire("a");
    const wz::rhi::Tag b = tags.acquire("b");
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());

    wz::rhi::RenderProgramDesc program;
    wz::rhi::ShaderResourceGroupLayout slot_2;
    slot_2.binding_slot = 2;
    slot_2.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::StructuredBufferSRV,
        wz::rhi::ShaderStage::Vertex,
        a,
        /*shader_register*/ 0,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });
    slot_2.constants_binding = wz::rhi::RootConstantsBinding{
        wz::rhi::ShaderStage::Vertex,
        /*shader_register*/ 0,
        /*register_space*/ 2 };
    ASSERT_TRUE(slot_2.constants.append(b, 64));

    wz::rhi::ShaderResourceGroupLayout slot_0;
    slot_0.binding_slot = 0;
    slot_0.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::StructuredBufferSRV,
        wz::rhi::ShaderStage::Vertex,
        a,
        /*shader_register*/ 0,
        /*register_space*/ 0,
        /*descriptor_count*/ 1 });

    program.shader_resource_groups = { slot_2, slot_0 };

    const auto plan =
        wz::engine::rendering::plan_dx12_pipeline_layout(program);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->descriptor_tables.size(), 2u);
    EXPECT_EQ(plan->descriptor_tables[0].binding_slot, 0u);
    EXPECT_EQ(plan->descriptor_tables[0].root_parameter_index, 0u);
    EXPECT_EQ(plan->descriptor_tables[1].binding_slot, 2u);
    EXPECT_EQ(plan->descriptor_tables[1].root_parameter_index, 1u);
    ASSERT_TRUE(plan->root_constants.valid);
    EXPECT_EQ(plan->root_constants.root_parameter_index, 2u);
}

TEST(RhiDx12Pipeline, SpanPlannerRejectsDuplicateBindingSlots)
{
    wz::rhi::ShaderResourceGroupLayout slot_a;
    slot_a.binding_slot = 1;
    wz::rhi::ShaderResourceGroupLayout slot_b;
    slot_b.binding_slot = 1;

    const std::vector<wz::rhi::ShaderResourceGroupLayout> srgs{
        slot_a,
        slot_b };
    const std::span<const wz::rhi::ShaderResourceGroupLayout> srg_span{
        srgs.data(),
        srgs.size() };

    EXPECT_FALSE(
        wz::engine::rendering::plan_dx12_pipeline_layout(srg_span)
            .has_value());
}

TEST(RhiDx12Pipeline, SpanPlannerRejectsMultipleRootConstantRanges)
{
    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag constants = tags.acquire("dispatch_constants");
    ASSERT_TRUE(constants.valid());

    wz::rhi::ShaderResourceGroupLayout slot_0;
    slot_0.binding_slot = 0;
    slot_0.constants_binding = wz::rhi::RootConstantsBinding{
        wz::rhi::ShaderStage::All,
        0,
        0 };
    ASSERT_TRUE(slot_0.constants.append(constants, 16));

    wz::rhi::ShaderResourceGroupLayout slot_2;
    slot_2.binding_slot = 2;
    slot_2.constants_binding = wz::rhi::RootConstantsBinding{
        wz::rhi::ShaderStage::Compute,
        0,
        2 };
    ASSERT_TRUE(slot_2.constants.append(constants, 16));

    const std::vector<wz::rhi::ShaderResourceGroupLayout> srgs{
        slot_0,
        slot_2 };
    const std::span<const wz::rhi::ShaderResourceGroupLayout> srg_span{
        srgs.data(),
        srgs.size() };

    EXPECT_FALSE(
        wz::engine::rendering::plan_dx12_pipeline_layout(srg_span)
            .has_value());
}

TEST(RhiDx12Pipeline, MultipleRootConstantRangesAreRejectedForStageOne)
{
    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag a = tags.acquire("a");
    ASSERT_TRUE(a.valid());

    wz::rhi::RenderProgramDesc program;
    wz::rhi::ShaderResourceGroupLayout slot_0;
    slot_0.binding_slot = 0;
    slot_0.constants_binding = wz::rhi::RootConstantsBinding{
        wz::rhi::ShaderStage::Vertex,
        0,
        0 };
    ASSERT_TRUE(slot_0.constants.append(a, 16));

    wz::rhi::ShaderResourceGroupLayout slot_2;
    slot_2.binding_slot = 2;
    slot_2.constants_binding = wz::rhi::RootConstantsBinding{
        wz::rhi::ShaderStage::Vertex,
        0,
        2 };
    ASSERT_TRUE(slot_2.constants.append(a, 16));
    program.shader_resource_groups = { slot_0, slot_2 };

    EXPECT_FALSE(
        wz::engine::rendering::plan_dx12_pipeline_layout(program).has_value());
}

// ── Input-element semantics (#317) ──────────────────────────────────────────
//
// D3D12 requires every (SemanticName, SemanticIndex) pair in an input layout to
// be unique. The mapping used to hardcode SemanticIndex 0, so locations 2 and 4
// both produced ("TEXCOORD", 0) and CreateInputLayout rejected the entire PSO --
// which is exactly what InputLayoutKind::GaussianSplatVertex declares
// (locations 0..4), so BuiltinRenderProgram::GaussianSplatDebug could not
// create a pipeline at all.
//
// This is device-free on purpose: the collision survived because
// build_input_layout lived in an anonymous namespace where no test could reach
// it, and the only IA layouts in use (1 and 3 attributes at locations 0..2)
// happened to dodge it.
TEST(RhiDx12InputElementSemantic, PairsAreUniqueAcrossEveryLocation)
{
    using wz::engine::rendering::dx12_input_element_semantic;

    // 8 covers every location any in-tree VertexLayout declares (the widest is
    // GaussianSplatVertex at 0..4) with headroom.
    constexpr uint32_t kLocations = 8;
    for (uint32_t a = 0; a < kLocations; ++a) {
        for (uint32_t b = a + 1; b < kLocations; ++b) {
            const auto sa = dx12_input_element_semantic(a);
            const auto sb = dx12_input_element_semantic(b);
            const bool same = std::string_view(sa.name)
                    == std::string_view(sb.name)
                && sa.index == sb.index;
            EXPECT_FALSE(same)
                << "locations " << a << " and " << b
                << " both map to " << sa.name << sa.index
                << " -- D3D12 rejects the whole input layout";
        }
    }
}

// The specific pair that was broken, named so a future simplification of the
// mapping cannot quietly re-collide them. Location 2 is GaussianSplatVertex's
// `scale`, location 4 its `color`.
TEST(RhiDx12InputElementSemantic, GaussianSplatScaleAndColorDoNotCollide)
{
    using wz::engine::rendering::dx12_input_element_semantic;

    const auto scale = dx12_input_element_semantic(2);
    const auto color = dx12_input_element_semantic(4);
    EXPECT_STREQ(scale.name, "TEXCOORD");
    EXPECT_EQ(scale.index, 0u);
    EXPECT_STREQ(color.name, "TEXCOORD");
    EXPECT_NE(color.index, scale.index);
}

// -- Root-signature DWORD budget (#317) --------------------------------------
//
// A D3D12 root signature may total at most 64 DWORDs: one per 32-bit root
// constant, one per descriptor table. Nothing bounded this anywhere --
// constants_dwords is an authored integer with only a LOWER bound -- so an
// over-budget layout reached CreateRootSignature and came back E_INVALIDARG
// with no attribution.
//
// Measured on WARP before the fix: 60 constants + 1 table -> S_OK with 3
// DWORDs to spare (that is the shipped GaussianSplatTerrainCoverageDebug
// preset); 64 constants + 1 table -> E_INVALIDARG. And
// D3D12SerializeRootSignature returned S_OK for ALL of them, so the one place
// we capture a D3D error blob never fired.
//
// Device-free on purpose: the boundary is arithmetic, and the whole point is
// to reject at PLAN time while the program is still identifiable.
namespace
{
    wz::rhi::RenderProgramDesc program_with_constant_dwords(
        wz::rhi::Tag semantic, uint32_t dwords)
    {
        wz::rhi::RenderProgramDesc program;
        program.name = "budget_probe";
        program.vertex_shader = "vs";
        program.pixel_shader = "ps";

        wz::rhi::ShaderResourceGroupLayout slot_2;
        slot_2.binding_slot = 2;
        slot_2.constants_binding = wz::rhi::RootConstantsBinding{
            wz::rhi::ShaderStage::All, 0, 2 };
        // One descriptor => one descriptor table => 1 DWORD of the budget.
        slot_2.descriptors.push_back(wz::rhi::DescriptorBinding{
            wz::rhi::DescriptorKind::StructuredBufferSRV,
            wz::rhi::ShaderStage::Vertex,
            semantic,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        EXPECT_TRUE(slot_2.constants.append(
            semantic, dwords * sizeof(uint32_t)));
        program.shader_resource_groups = { slot_2 };
        return program;
    }
}

TEST(RhiDx12Pipeline, RootSignatureBudgetIsCheckedAtPlanTime)
{
    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag a = tags.acquire("a");
    ASSERT_TRUE(a.valid());

    // 63 constants + 1 table = 64 DWORDs exactly: the last legal layout.
    const auto at_budget =
        wz::engine::rendering::plan_dx12_pipeline_layout(
            program_with_constant_dwords(a, 63));
    ASSERT_TRUE(at_budget.has_value())
        << "a layout costing exactly 64 DWORDs is legal and must plan";
    EXPECT_EQ(
        wz::engine::rendering::dx12_root_signature_dword_cost(*at_budget),
        wz::engine::rendering::kDx12MaxRootSignatureDwords);

    // 64 constants + 1 table = 65: one DWORD over, and CreateRootSignature
    // would return a bare E_INVALIDARG naming nothing.
    EXPECT_FALSE(
        wz::engine::rendering::plan_dx12_pipeline_layout(
            program_with_constant_dwords(a, 64)).has_value())
        << "an over-budget layout must be refused while the program is still "
           "identifiable, not at CreateRootSignature";
}

// The shipped preset that sits closest to the ceiling, so shrinking the budget
// (or adding a second descriptor table to this shape) fails here rather than
// in a user's project. 60 constants + 1 table, measured S_OK with 3 to spare.
TEST(RhiDx12Pipeline, WidestShippedPresetStillFitsTheBudget)
{
    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag a = tags.acquire("a");

    const auto planned =
        wz::engine::rendering::plan_dx12_pipeline_layout(
            program_with_constant_dwords(a, 60));
    ASSERT_TRUE(planned.has_value());
    EXPECT_EQ(
        wz::engine::rendering::dx12_root_signature_dword_cost(*planned), 61u);
}
