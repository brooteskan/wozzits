#include <gtest/gtest.h>

#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/rhi_render_program_bridge.h>

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
    ASSERT_TRUE(plan->root_param_for_slot(2).has_value());
    EXPECT_EQ(*plan->root_param_for_slot(2), 0u);

    EXPECT_TRUE(plan->root_constants.valid);
    EXPECT_EQ(plan->root_constants.root_parameter_index, 1u);
    EXPECT_EQ(plan->root_constants.dword_count, 16u);
    EXPECT_EQ(plan->root_constants.shader_register, 0u);
    EXPECT_EQ(plan->root_constants.register_space, 2u);
    EXPECT_EQ(plan->root_constants.visibility, wz::rhi::ShaderStage::Vertex);
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
