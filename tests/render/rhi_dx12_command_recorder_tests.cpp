#include <gtest/gtest.h>

#include <engine/rendering/rhi_dx12_command_recorder.h>
#include <engine/rendering/engine_gpu_context.h>

#include <gpu/dx12/dx12_descriptor_allocator.h>
#include <gpu/dx12/dx12_internal.h>
#include <gpu/gpu.h>
#include <gpu/gpu_resource_types.h>
#include <window/window2.h>

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/shader_resource_group.h>

#include <array>

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

// #198 step 1: a Sampler descriptor has no heap/root-param path yet, so the
// recorder must fail closed rather than mis-bind it as a buffer/texture view.
// This also proves the recorder no longer collapses every DescriptorKind to a
// UAV-or-not bool — an unsupported kind is now distinguishable and rejected.
TEST(RhiDx12CommandRecorder, BindResourceGroupRejectsSamplerDescriptor)
{
    RecorderHarness harness;
    wz::engine::rendering::RhiDx12RealizedPipeline pipeline;
    pipeline.root_signature = reinterpret_cast<ID3D12RootSignature*>(1);
    pipeline.pipeline_state = reinterpret_cast<ID3D12PipelineState*>(1);
    pipeline.layout.descriptor_tables.push_back({
        /*binding_slot*/ 2,
        /*root_parameter_index*/ 0,
        /*descriptor_count*/ 1,
        { wz::rhi::DescriptorKind::Sampler } });
    harness.recorder.set_current_for_testing(&pipeline);

    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag sampler = tags.acquire("sampler");
    ASSERT_TRUE(sampler.valid());

    wz::rhi::ShaderResourceGroupLayout layout;
    layout.binding_slot = 2;
    layout.descriptors.push_back(wz::rhi::DescriptorBinding{
        wz::rhi::DescriptorKind::Sampler,
        wz::rhi::ShaderStage::Pixel,
        sampler,
        /*shader_register*/ 0,
        /*register_space*/ 2,
        /*descriptor_count*/ 1 });
    wz::rhi::ShaderResourceGroup srg(layout);

    ASSERT_TRUE(harness.recorder.ready());
    harness.recorder.bind_resource_group(2, srg);

    EXPECT_FALSE(harness.recorder.ready());
}

// ── On-device: TextureSRV descriptor-table build (#198 step 1) ─────────────────
//
// The crux of #198 step 1: a TextureSRV descriptor binds a resident Texture2D.
// These drive the generic builder the recorder's bind path now calls
// (create_resource_descriptor_table) with a real device + a real R32Float
// Texture2D made resident through the same rhi registry door #197 publishes the
// scalar-field height texture through. A valid table means a
// D3D12_SRV_DIMENSION_TEXTURE2D SRV was created against the resident texture —
// the capability that was missing (the path could only make structured-buffer
// SRVs). Skipped if no GPU device can be created.
namespace
{
    struct RecorderDeviceFixture : public ::testing::Test
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device          device{};

        void SetUp() override
        {
            wz::window::WindowDesc desc{};
            desc.title     = "rhi_dx12_command_recorder_device_test";
            desc.width     = 64;
            desc.height    = 64;
            desc.resizable = false;

            window = wz::window::create_window(desc);
            if (!window.valid()) {
                GTEST_SKIP() << "no window available for on-device recorder test";
            }
            device = wz::gpu::create_device(window);
            if (!device.valid()) {
                wz::window::destroy_window(window);
                window = {};
                GTEST_SKIP() << "no GPU device available for on-device test";
            }
        }

        void TearDown() override
        {
            if (device.valid()) wz::gpu::destroy_device(device);
            if (window.valid()) wz::window::destroy_window(window);
        }
    };
}

TEST_F(RecorderDeviceFixture, TextureSrvDescriptorTableBuilds)
{
    wz::engine::rendering::EngineGpuContext gpu(device);

    // Make an R32Float Texture2D resident via the rhi registry — the same door
    // (EngineGpuBackend::create -> create_texture_dx12) the scalar-field height
    // texture uses. Anonymous identity: a fresh resource we own for the test.
    const wz::rhi::GpuResourceHandle tex_handle = gpu.resources.acquire(
        wz::rhi::GpuResourceDesc::texture_2d(
            /*w*/ 16, /*h*/ 8,
            wz::rhi::TextureFormat::R32Float,
            wz::rhi::ResourceUsage_Sampled));
    ASSERT_TRUE(tex_handle.valid());

    const wz::rhi::GpuResource* tex_resource = gpu.resources.get(tex_handle);
    ASSERT_NE(tex_resource, nullptr);
    const wz::gpu::GPUHandle tex_gpu =
        gpu.backend.gpu_handle_for(tex_resource->backend);
    ASSERT_TRUE(tex_gpu.valid());
    ASSERT_EQ(tex_gpu.type, wz::gpu::kGPUTextureResourceType);

    // The generic builder the recorder's bind path calls. A TextureSRV kind
    // resolves the handle in the texture table and writes a Texture2D SRV.
    const std::array handles{ tex_gpu };
    const std::array kinds{
        wz::gpu::dx12::internal::DescriptorViewKind::Texture2DSRV };

    wz::gpu::dx12::DX12DescriptorTable table{};
    EXPECT_TRUE(wz::gpu::dx12::internal::create_resource_descriptor_table(
        device, handles, kinds, table));
    EXPECT_TRUE(table.valid());
    EXPECT_EQ(table.count, 1u);

    wz::gpu::dx12::internal::release_compute_buffer_srv_table(device, table);

    gpu.resources.release(tex_handle);
    gpu.resources.collect(UINT64_MAX);
}

// A mixed table (a structured pull buffer + a texture, the clipmap shape: mesh
// positions in one slot, the height texture in another) builds in one heap
// range, proving the per-slot kind branch and that buffer + texture descriptors
// coexist — not just an all-texture table.
TEST_F(RecorderDeviceFixture, MixedBufferAndTextureDescriptorTableBuilds)
{
    wz::engine::rendering::EngineGpuContext gpu(device);

    const wz::rhi::GpuResourceHandle buffer_handle = gpu.resources.acquire(
        wz::rhi::GpuResourceDesc::buffer(
            /*size*/ 256, /*stride*/ 16, wz::rhi::ResourceUsage_Sampled));
    ASSERT_TRUE(buffer_handle.valid());

    const wz::rhi::GpuResourceHandle tex_handle = gpu.resources.acquire(
        wz::rhi::GpuResourceDesc::texture_2d(
            16, 8, wz::rhi::TextureFormat::R32Float,
            wz::rhi::ResourceUsage_Sampled));
    ASSERT_TRUE(tex_handle.valid());

    const wz::gpu::GPUHandle buffer_gpu =
        gpu.backend.gpu_handle_for(gpu.resources.get(buffer_handle)->backend);
    const wz::gpu::GPUHandle tex_gpu =
        gpu.backend.gpu_handle_for(gpu.resources.get(tex_handle)->backend);
    ASSERT_TRUE(buffer_gpu.valid());
    ASSERT_TRUE(tex_gpu.valid());

    const std::array handles{ buffer_gpu, tex_gpu };
    const std::array kinds{
        wz::gpu::dx12::internal::DescriptorViewKind::StructuredBufferSRV,
        wz::gpu::dx12::internal::DescriptorViewKind::Texture2DSRV };

    wz::gpu::dx12::DX12DescriptorTable table{};
    EXPECT_TRUE(wz::gpu::dx12::internal::create_resource_descriptor_table(
        device, handles, kinds, table));
    EXPECT_TRUE(table.valid());
    EXPECT_EQ(table.count, 2u);

    wz::gpu::dx12::internal::release_compute_buffer_srv_table(device, table);

    gpu.resources.release(buffer_handle);
    gpu.resources.release(tex_handle);
    gpu.resources.collect(UINT64_MAX);
}

// A TextureSRV kind that resolves a NON-texture handle (a buffer in the texture
// table) must fail the build, not bind garbage — the validation that the handle
// matches the table its descriptor kind selects.
TEST_F(RecorderDeviceFixture, TextureSrvWithBufferHandleFailsBuild)
{
    wz::engine::rendering::EngineGpuContext gpu(device);

    const wz::rhi::GpuResourceHandle buffer_handle = gpu.resources.acquire(
        wz::rhi::GpuResourceDesc::buffer(
            256, 16, wz::rhi::ResourceUsage_Sampled));
    ASSERT_TRUE(buffer_handle.valid());

    const wz::gpu::GPUHandle buffer_gpu =
        gpu.backend.gpu_handle_for(gpu.resources.get(buffer_handle)->backend);
    ASSERT_TRUE(buffer_gpu.valid());

    const std::array handles{ buffer_gpu };
    const std::array kinds{
        wz::gpu::dx12::internal::DescriptorViewKind::Texture2DSRV };

    wz::gpu::dx12::DX12DescriptorTable table{};
    EXPECT_FALSE(wz::gpu::dx12::internal::create_resource_descriptor_table(
        device, handles, kinds, table));
    EXPECT_FALSE(table.valid());

    gpu.resources.release(buffer_handle);
    gpu.resources.collect(UINT64_MAX);
}

namespace
{
    // Build a fake realized pipeline exposing a single-descriptor table at the
    // given slot, so bind_resource_group reaches its resource-resolution loop
    // (where the timeline touch lives). The root-signature/PSO pointers are
    // non-null sentinels — the SetRootDescriptorTable call they'd drive is not
    // what these tests assert on; the touch fires before it.
    wz::engine::rendering::RhiDx12RealizedPipeline fake_single_srv_pipeline(
        uint32_t slot)
    {
        wz::engine::rendering::RhiDx12RealizedPipeline pipeline;
        pipeline.root_signature = reinterpret_cast<ID3D12RootSignature*>(1);
        pipeline.pipeline_state = reinterpret_cast<ID3D12PipelineState*>(1);
        pipeline.layout.descriptor_tables.push_back({
            /*binding_slot*/ slot,
            /*root_parameter_index*/ 0,
            /*descriptor_count*/ 1,
            { wz::rhi::DescriptorKind::StructuredBufferSRV } });
        return pipeline;
    }

    // A one-descriptor SRG referencing `handle` at `slot`, matching the fake
    // pipeline above so bind_resource_group resolves (and touches) it.
    wz::rhi::ShaderResourceGroup single_srv_srg(
        wz::rhi::Tag semantic,
        uint32_t slot,
        wz::rhi::GpuResourceHandle handle)
    {
        wz::rhi::ShaderResourceGroupLayout layout;
        layout.binding_slot = slot;
        layout.descriptors.push_back(wz::rhi::DescriptorBinding{
            wz::rhi::DescriptorKind::StructuredBufferSRV,
            wz::rhi::ShaderStage::Vertex,
            semantic,
            /*shader_register*/ 0,
            /*register_space*/ 2,
            /*descriptor_count*/ 1 });
        wz::rhi::ShaderResourceGroup srg(layout);
        srg.set(0u, handle);
        return srg;
    }
}

// ── The core lock: bind touches the resolved handle with the frame timeline, so
// precise reclamation keeps it resident until the GPU passes that frame ──────
//
// A resource referenced by a bound SRG is tagged with the frame's timeline
// value. After release(), collect(V-1) must NOT reclaim it (the GPU has not
// passed the frame that referenced it); collect(V) must. This is the exact
// guarantee the whole timeline wiring exists to provide.
TEST_F(RecorderDeviceFixture, BindTouchesResourceForPreciseReclamation)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    wz::engine::rendering::RhiDx12PipelineCache pipelines(
        device, gpu.programs, gpu.compute_programs, gpu.shaders);
    wz::engine::rendering::RhiDx12CommandRecorder recorder(
        device, pipelines, gpu.resources, gpu.backend);

    const wz::rhi::GpuResourceHandle handle = gpu.resources.acquire(
        wz::rhi::GpuResourceDesc::buffer(
            /*size*/ 256, /*stride*/ 16, wz::rhi::ResourceUsage_Sampled));
    ASSERT_TRUE(handle.valid());

    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag positions = tags.acquire("positions");
    ASSERT_TRUE(positions.valid());

    const wz::engine::rendering::RhiDx12RealizedPipeline pipeline =
        fake_single_srv_pipeline(/*slot*/ 2);
    recorder.set_current_for_testing(&pipeline);

    constexpr uint64_t kFrame = 100;
    recorder.set_frame_timeline(kFrame);

    wz::rhi::ShaderResourceGroup srg =
        single_srv_srg(positions, /*slot*/ 2, handle);
    recorder.bind_resource_group(2, srg);

    // The bind touched the handle with kFrame. Release it, then verify the
    // registry reclaims it exactly at kFrame — not before.
    gpu.resources.release(handle);

    gpu.resources.collect(/*completed*/ kFrame - 1);
    EXPECT_NE(gpu.resources.get(handle), nullptr)
        << "resource reclaimed before the GPU passed the frame that bound it";

    gpu.resources.collect(/*completed*/ kFrame);
    EXPECT_EQ(gpu.resources.get(handle), nullptr)
        << "resource not reclaimed after the GPU passed its last-use frame";
}

// Re-binding at a later frame must refresh last_use: a resource bound at V1 then
// re-bound at V2 (> V1) survives collect(V1) — a stale first touch must not let
// it be reclaimed while a later frame still references it.
TEST_F(RecorderDeviceFixture, RebindRefreshesLastUseToLatestFrame)
{
    wz::engine::rendering::EngineGpuContext gpu(device);
    wz::engine::rendering::RhiDx12PipelineCache pipelines(
        device, gpu.programs, gpu.compute_programs, gpu.shaders);
    wz::engine::rendering::RhiDx12CommandRecorder recorder(
        device, pipelines, gpu.resources, gpu.backend);

    const wz::rhi::GpuResourceHandle handle = gpu.resources.acquire(
        wz::rhi::GpuResourceDesc::buffer(
            256, 16, wz::rhi::ResourceUsage_Sampled));
    ASSERT_TRUE(handle.valid());

    wz::rhi::TagRegistry<8> tags;
    const wz::rhi::Tag positions = tags.acquire("positions");
    ASSERT_TRUE(positions.valid());

    const wz::engine::rendering::RhiDx12RealizedPipeline pipeline =
        fake_single_srv_pipeline(/*slot*/ 2);
    recorder.set_current_for_testing(&pipeline);

    constexpr uint64_t kFrame1 = 10;
    constexpr uint64_t kFrame2 = 20;

    wz::rhi::ShaderResourceGroup srg =
        single_srv_srg(positions, /*slot*/ 2, handle);

    // Frame 1 bind, then a later Frame 2 bind: the second touch must win.
    recorder.set_frame_timeline(kFrame1);
    recorder.bind_resource_group(2, srg);
    recorder.set_frame_timeline(kFrame2);
    recorder.bind_resource_group(2, srg);

    gpu.resources.release(handle);

    // collect at frame 1: the LATEST touch (frame 2) has not been passed, so the
    // resource must stay — proving the re-bind refreshed last_use to kFrame2.
    gpu.resources.collect(/*completed*/ kFrame1);
    EXPECT_NE(gpu.resources.get(handle), nullptr)
        << "resource reclaimed at the first touch; re-bind did not refresh "
           "last_use to the latest frame";

    gpu.resources.collect(/*completed*/ kFrame2);
    EXPECT_EQ(gpu.resources.get(handle), nullptr)
        << "resource not reclaimed after the GPU passed the latest frame";
}
