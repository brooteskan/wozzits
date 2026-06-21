#include <gtest/gtest.h>

#include <engine/rendering/rhi_dx12_command_recorder.h>
#include <engine/rendering/engine_gpu_context.h>

#include <wozzits/rhi/shader_resource_group.h>

namespace
{
    struct RecorderHarness
    {
        wz::gpu::Device device;
        wz::engine::rendering::EngineGpuContext gpu{ device };
        wz::rhi::RenderProgramRegistry programs;
        wz::rhi::ComputeProgramRegistry compute_programs;
        wz::rhi::ShaderModuleRegistry shaders;
        wz::engine::rendering::RhiDx12PipelineCache pipelines{
            device,
            programs,
            compute_programs,
            shaders };
        wz::engine::rendering::RhiDx12CommandRecorder recorder{
            device,
            pipelines,
            gpu.resources,
            gpu.backend };
    };
}

TEST(RhiDx12CommandRecorder, InvalidPipelineDisablesRecording)
{
    RecorderHarness harness;

    harness.recorder.set_pipeline(wz::rhi::Tag{});

    EXPECT_FALSE(harness.recorder.ready());

    harness.recorder.set_root_constants({});
    harness.recorder.bind_resource_group(0, wz::rhi::ShaderResourceGroup{});
    harness.recorder.draw(wz::rhi::DrawArgs{ .vertex_count = 3 });

    EXPECT_FALSE(harness.recorder.ready());
}

TEST(RhiDx12CommandRecorder, BindResourceGroupRejectsMissingBackendResource)
{
    RecorderHarness harness;
    wz::engine::rendering::RhiDx12RealizedPipeline pipeline;
    pipeline.root_signature = reinterpret_cast<ID3D12RootSignature*>(1);
    pipeline.pipeline_state = reinterpret_cast<ID3D12PipelineState*>(1);
    pipeline.layout.descriptor_tables.push_back({
        /*binding_slot*/ 2,
        /*root_parameter_index*/ 0,
        /*descriptor_count*/ 1,
        { wz::rhi::DescriptorKind::StructuredBufferSRV } });
    harness.recorder.set_current_for_testing(&pipeline);

    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag positions = tags.acquire("positions");
    ASSERT_TRUE(positions.valid());

    wz::rhi::ShaderResourceGroupLayout layout;
    layout.binding_slot = 2;
    layout.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::StructuredBufferSRV,
        wz::rhi::ShaderStage::Vertex,
        positions,
        /*shader_register*/ 0,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });
    wz::rhi::ShaderResourceGroup srg(layout);

    ASSERT_TRUE(harness.recorder.ready());
    harness.recorder.bind_resource_group(2, srg);

    EXPECT_FALSE(harness.recorder.ready());
}
