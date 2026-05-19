// tests/gpu/dx12_descriptor_allocator.cpp

#include <gtest/gtest.h>

#include <gpu/dx12/dx12_descriptor_allocator.h>
#include <gpu/dx12/dx12_internal.h>
#include <gpu/gpu.h>
#include <window/window2.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

namespace
{
    struct GpuDeviceFixture : public ::testing::Test
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device          device{};

        void SetUp() override
        {
            wz::window::WindowDesc desc{};
            desc.title     = "dx12_descriptor_allocator_test";
            desc.width     = 64;
            desc.height    = 64;
            desc.resizable = false;

            window = wz::window::create_window(desc);
            ASSERT_TRUE(window.native);

            device = wz::gpu::create_device(window);
            ASSERT_TRUE(device.impl);
        }

        void TearDown() override
        {
            if (device.impl) wz::gpu::destroy_device(device);
            if (window.native) wz::window::destroy_window(window);
        }

        ID3D12Device* d3d_device() const
        {
            return wz::gpu::dx12::internal::get_device(
                const_cast<wz::gpu::Device&>(device));
        }
    };
}

// ── Unit tests (no GPU needed) ────────────────────────────────────────────────

TEST(DX12DescriptorTable, DefaultIsInvalid)
{
    wz::gpu::dx12::DX12DescriptorTable t{};
    EXPECT_FALSE(t.valid());
}

TEST(DX12DescriptorTable, CpuAtComputesCorrectOffset)
{
    wz::gpu::dx12::DX12DescriptorTable t{};
    t.cpu_start = { 1000 };
    t.gpu_start = { 2000 };
    t.count     = 4;
    t.stride    = 32;

    EXPECT_EQ(t.cpu_at(0).ptr, 1000u);
    EXPECT_EQ(t.cpu_at(1).ptr, 1032u);
    EXPECT_EQ(t.cpu_at(3).ptr, 1096u);
}

TEST(DX12DescriptorTable, GpuAtComputesCorrectOffset)
{
    wz::gpu::dx12::DX12DescriptorTable t{};
    t.cpu_start = { 1000 };
    t.gpu_start = { 5000 };
    t.count     = 3;
    t.stride    = 64;

    EXPECT_EQ(t.gpu_at(0).ptr, 5000u);
    EXPECT_EQ(t.gpu_at(2).ptr, 5128u);
}

TEST(DX12DescriptorAllocator, DefaultStateIsEmpty)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    EXPECT_EQ(alloc.heap(),     nullptr);
    EXPECT_EQ(alloc.capacity(), 0u);
    EXPECT_EQ(alloc.used(),     0u);
}

TEST(DX12DescriptorAllocator, AllocateWithoutInitReturnsInvalid)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    const auto t = alloc.allocate(1);
    EXPECT_FALSE(t.valid());
}

// ── GPU fixture tests ─────────────────────────────────────────────────────────

TEST_F(GpuDeviceFixture, InitSucceeds)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    ASSERT_TRUE(alloc.init(d3d_device(), 16));

    EXPECT_NE(alloc.heap(), nullptr);
    EXPECT_EQ(alloc.capacity(), 16u);
    EXPECT_EQ(alloc.used(), 0u);
    EXPECT_GT(alloc.stride(), 0u);

    alloc.destroy();
}

TEST_F(GpuDeviceFixture, AllocateSingleReturnsValidTable)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    ASSERT_TRUE(alloc.init(d3d_device(), 16));

    const auto t = alloc.allocate(1);
    ASSERT_TRUE(t.valid());
    EXPECT_EQ(t.count, 1u);
    EXPECT_EQ(t.stride, alloc.stride());
    EXPECT_EQ(alloc.used(), 1u);

    alloc.destroy();
}

TEST_F(GpuDeviceFixture, SequentialAllocationsDoNotOverlap)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    ASSERT_TRUE(alloc.init(d3d_device(), 8));

    const auto a = alloc.allocate(1);
    const auto b = alloc.allocate(1);
    const auto c = alloc.allocate(3);

    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());
    ASSERT_TRUE(c.valid());

    EXPECT_EQ(b.cpu_start.ptr, a.cpu_start.ptr + alloc.stride());
    EXPECT_EQ(c.cpu_start.ptr, b.cpu_start.ptr + alloc.stride());
    EXPECT_EQ(alloc.used(), 5u);

    alloc.destroy();
}

TEST_F(GpuDeviceFixture, ExhaustionReturnsInvalid)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    ASSERT_TRUE(alloc.init(d3d_device(), 4));

    EXPECT_TRUE(alloc.allocate(4).valid());   // fills exactly
    EXPECT_FALSE(alloc.allocate(1).valid());  // one more over capacity

    alloc.destroy();
}

TEST_F(GpuDeviceFixture, ResetRestoresCapacity)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    ASSERT_TRUE(alloc.init(d3d_device(), 4));

    EXPECT_TRUE(alloc.allocate(4).valid());
    EXPECT_EQ(alloc.used(), 4u);

    alloc.reset();
    EXPECT_EQ(alloc.used(), 0u);

    const auto t = alloc.allocate(2);
    EXPECT_TRUE(t.valid());
    EXPECT_EQ(t.count, 2u);

    alloc.destroy();
}

TEST_F(GpuDeviceFixture, HeapIsShaderVisible)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    ASSERT_TRUE(alloc.init(d3d_device(), 8));

    D3D12_DESCRIPTOR_HEAP_DESC desc = alloc.heap()->GetDesc();
    EXPECT_EQ(desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
              D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    EXPECT_EQ(desc.Type, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    alloc.destroy();
}

TEST_F(GpuDeviceFixture, DeviceAccessorMatchesInitDevice)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    ASSERT_TRUE(alloc.init(d3d_device(), 4));
    EXPECT_EQ(alloc.device(), d3d_device());
    alloc.destroy();
}

TEST_F(GpuDeviceFixture, CreateStructuredBufferSrvWritesDescriptor)
{
    wz::gpu::dx12::DX12DescriptorAllocator alloc{};
    ASSERT_TRUE(alloc.init(d3d_device(), 4));

    const auto table = alloc.allocate(1);
    ASSERT_TRUE(table.valid());

    // Create a small upload-heap buffer to act as the structured buffer.
    // StructuredBuffer element: float4 (16 bytes), 8 elements = 128 bytes.
    constexpr uint32_t kElemCount  = 8;
    constexpr uint32_t kStrideBytes = 16;
    constexpr uint64_t kBufferBytes = kElemCount * kStrideBytes;

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC res_desc{};
    res_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    res_desc.Width              = kBufferBytes;
    res_desc.Height             = 1;
    res_desc.DepthOrArraySize   = 1;
    res_desc.MipLevels          = 1;
    res_desc.Format             = DXGI_FORMAT_UNKNOWN;
    res_desc.SampleDesc.Count   = 1;
    res_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* buffer = nullptr;
    HRESULT hr = d3d_device()->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &res_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&buffer));
    ASSERT_TRUE(SUCCEEDED(hr));

    // Writing the SRV descriptor must not assert or crash.
    alloc.create_structured_buffer_srv(table, 0, buffer, kElemCount, kStrideBytes);

    buffer->Release();
    alloc.destroy();
}
