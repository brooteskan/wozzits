// tests/render/rhi_offscreen_rt_tests.cpp
//
// On-device coverage of the offscreen render-to-texture core (S6): create a
// render-target texture, clear it inside an offscreen pass, and read it back to
// verify the target actually holds the rendered result. Proves the RT-usable
// texture (RTV), the offscreen begin/end pass (bind + state transitions), and the
// texture readback. Skips cleanly without a window / GPU device.

#include <gtest/gtest.h>

#include <gpu/gpu.h>
#include <gpu/texture.h>
#include <gpu/dx12/dx12_internal.h>

#include <window/window2.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
    namespace gi = wz::gpu::dx12::internal;
}

TEST(OffscreenRenderTarget, ClearIntoTextureThenReadBack)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "rhi_offscreen_rt_test";
    window_desc.width = 256;
    window_desc.height = 256;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for the offscreen-RT test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for the offscreen-RT test";
    }

    // A render-target texture -- the usage flows through TextureDesc.render_target,
    // which the DX12 create path turns into ALLOW_RENDER_TARGET + an RTV.
    wz::gpu::TextureDesc rt_desc{};
    rt_desc.width = 64;
    rt_desc.height = 64;
    rt_desc.format = wz::gpu::TextureFormat::RGBA8Unorm;
    rt_desc.render_target = true;
    const wz::gpu::GPUHandle rt = wz::gpu::create_texture(device, rt_desc);
    ASSERT_TRUE(rt.valid());

    // Clear it to a known colour inside an offscreen pass (no draw needed to prove
    // the RT bind + transition + readback plumbing).
    const float clear[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    ASSERT_TRUE(wz::gpu::begin_frame(device));
    ASSERT_TRUE(gi::begin_offscreen_pass(device, rt, clear));
    ASSERT_TRUE(gi::end_offscreen_pass(device, rt));
    ASSERT_TRUE(wz::gpu::end_frame(device));

    // Read it back: every texel is the clear colour (RGBA8 = round(c * 255)).
    std::vector<std::uint8_t> pixels;
    ASSERT_TRUE(gi::read_texture_rgba8_dx12(device, rt, pixels));
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(64) * 64u * 4u);
    EXPECT_NEAR(pixels[0], 64, 2);    // R 0.25
    EXPECT_NEAR(pixels[1], 128, 2);   // G 0.5
    EXPECT_NEAR(pixels[2], 191, 2);   // B 0.75
    EXPECT_EQ(pixels[3], 255);        // A 1.0
    // A texel in the middle matches too (uniform clear, and it exercises the
    // row-pitch de-alignment in the readback).
    const std::size_t mid = (32u * 64u + 32u) * 4u;
    EXPECT_NEAR(pixels[mid + 0], 64, 2);
    EXPECT_NEAR(pixels[mid + 1], 128, 2);
    EXPECT_NEAR(pixels[mid + 2], 191, 2);
    EXPECT_EQ(pixels[mid + 3], 255);

    wz::gpu::release_texture(device, rt);
    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}

TEST(OffscreenRenderTarget, RejectsRenderTargetWithMipChain)
{
    // Pure validation (no device): a render target is a single 2D surface, so a
    // mip chain is rejected.
    wz::gpu::TextureDesc desc{};
    desc.width = 64;
    desc.height = 64;
    desc.format = wz::gpu::TextureFormat::RGBA8Unorm;
    desc.render_target = true;
    desc.mip_levels = 2;
    EXPECT_FALSE(desc.valid());

    desc.mip_levels = 1;
    EXPECT_TRUE(desc.valid());
}
