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
        desc.blend_mode = ea::BlendMode::Opaque;
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
