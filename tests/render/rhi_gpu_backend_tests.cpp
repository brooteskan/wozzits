#include <gtest/gtest.h>

#include <engine/rendering/rhi_gpu_backend.h>

namespace rr = wz::engine::rendering;

// These cover the pure mapping logic — no GPU device required. The create/
// destroy paths that call the engine's DX12 functions need a live device and
// are exercised by on-device integration tests (window/gpu test groups).

TEST(RhiGpuBackend, StructuredBufferDescMapsStrideAndCount)
{
    const wz::rhi::GpuResourceDesc d = wz::rhi::GpuResourceDesc::buffer(
        /*size*/ 2048, /*stride*/ 32, wz::rhi::ResourceUsage_Sampled);
    const wz::gpu::ComputeBufferDesc out = rr::to_compute_buffer_desc(d);
    EXPECT_EQ(out.stride_bytes, 32u);
    EXPECT_EQ(out.element_count, 64u);   // 2048 / 32
    EXPECT_TRUE(out.valid());
}

TEST(RhiGpuBackend, RawBufferGetsDefaultStride)
{
    const wz::rhi::GpuResourceDesc d =
        wz::rhi::GpuResourceDesc::buffer(/*size*/ 256);
    const wz::gpu::ComputeBufferDesc out = rr::to_compute_buffer_desc(d);
    EXPECT_EQ(out.stride_bytes, 4u);
    EXPECT_EQ(out.element_count, 64u);   // 256 / 4
    EXPECT_TRUE(out.valid());
}

TEST(RhiGpuBackend, StorageUsageSelectsReadWrite)
{
    const wz::rhi::GpuResourceDesc rw = wz::rhi::GpuResourceDesc::buffer(
        256, 4, wz::rhi::ResourceUsage_Storage);
    const wz::rhi::GpuResourceDesc ro = wz::rhi::GpuResourceDesc::buffer(
        256, 4, wz::rhi::ResourceUsage_Sampled);
    EXPECT_TRUE(rr::desc_wants_rw(rw));
    EXPECT_FALSE(rr::desc_wants_rw(ro));
}

// ── Texture path (#197) ───────────────────────────────────────────────────────

TEST(RhiGpuBackend, Texture2dDescMapsDimsAndFormat)
{
    const wz::rhi::GpuResourceDesc d = wz::rhi::GpuResourceDesc::texture_2d(
        /*w*/ 64, /*h*/ 32, wz::rhi::TextureFormat::R32Float,
        wz::rhi::ResourceUsage_Sampled);

    wz::gpu::TextureDesc out{};
    EXPECT_TRUE(rr::to_texture_desc(d, out));
    EXPECT_EQ(out.dimension, wz::gpu::TextureDimension::Texture2D);
    EXPECT_EQ(out.width, 64u);
    EXPECT_EQ(out.height, 32u);
    EXPECT_EQ(out.depth, 1u);
    EXPECT_EQ(out.format, wz::gpu::TextureFormat::R32Float);
    EXPECT_TRUE(out.valid());
}

TEST(RhiGpuBackend, Texture3dDimensionRoutes)
{
    wz::rhi::GpuResourceDesc d = wz::rhi::GpuResourceDesc::texture_2d(
        16, 16, wz::rhi::TextureFormat::R32Float);
    d.dimension = wz::rhi::ResourceDimension::Texture3D;
    d.depth = 8;

    wz::gpu::TextureDesc out{};
    EXPECT_TRUE(rr::to_texture_desc(d, out));
    EXPECT_EQ(out.dimension, wz::gpu::TextureDimension::Texture3D);
    EXPECT_EQ(out.depth, 8u);
    EXPECT_TRUE(out.valid());
}

TEST(RhiGpuBackend, UnsupportedTextureFormatRejected)
{
    wz::rhi::GpuResourceDesc d = wz::rhi::GpuResourceDesc::texture_2d(
        8, 8, wz::rhi::TextureFormat::RGBA8Unorm);

    wz::gpu::TextureDesc out{};
    EXPECT_FALSE(rr::to_texture_desc(d, out));

    wz::gpu::TextureFormat fmt{};
    EXPECT_TRUE(rr::to_gpu_texture_format(wz::rhi::TextureFormat::R32Float, fmt));
    EXPECT_EQ(fmt, wz::gpu::TextureFormat::R32Float);
    EXPECT_FALSE(
        rr::to_gpu_texture_format(wz::rhi::TextureFormat::Undefined, fmt));
}
