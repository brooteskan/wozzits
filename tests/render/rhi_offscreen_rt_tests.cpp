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

// The material compositor: build a material texture from a base colour plus a
// placed layer. Proves the general operation the puppet-on-a-sphere consumes --
// base + art composited into one texture a mesh can sample -- including that the
// layer lands in the sub-rect the placement asks for (so "move/scale the art on
// the material" is real, not decorative).
TEST(OffscreenRenderTarget, CompositeLayersPlacesArtIntoMaterialTexture)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "rhi_composite_test";
    window_desc.width = 256;
    window_desc.height = 256;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for the composite test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for the composite test";
    }

    constexpr std::uint32_t kSize = 128;
    wz::gpu::TextureDesc rt_desc{};
    rt_desc.width = kSize;
    rt_desc.height = kSize;
    rt_desc.format = wz::gpu::TextureFormat::RGBA8Unorm;
    rt_desc.render_target = true;

    // The layer source: a second render target cleared to opaque RED, so we can
    // tell composited layer pixels from the material's base colour.
    const wz::gpu::GPUHandle layer_tex = wz::gpu::create_texture(device, rt_desc);
    const wz::gpu::GPUHandle material = wz::gpu::create_texture(device, rt_desc);
    ASSERT_TRUE(layer_tex.valid());
    ASSERT_TRUE(material.valid());

    const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    ASSERT_TRUE(wz::gpu::begin_frame(device));
    ASSERT_TRUE(gi::begin_offscreen_pass(device, layer_tex, red));
    ASSERT_TRUE(gi::end_offscreen_pass(device, layer_tex));
    ASSERT_TRUE(wz::gpu::end_frame(device));

    // Composite: base BLUE, with the red layer occupying the top-left quadrant
    // (centre uv (0.25,0.25), half-extent 0.25 => u,v in [0,0.5)).
    gi::TextureCompositeLayer layer{};
    layer.texture = layer_tex;
    layer.center_uv[0] = 0.25f;
    layer.center_uv[1] = 0.25f;
    layer.half_size_uv[0] = 0.25f;
    layer.half_size_uv[1] = 0.25f;
    layer.opacity = 1.0f;

    const float base_blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    ASSERT_TRUE(wz::gpu::begin_frame(device));
    ASSERT_TRUE(gi::composite_texture_layers_dx12(
        device, material, base_blue, &layer, 1));
    ASSERT_TRUE(wz::gpu::end_frame(device));

    std::vector<std::uint8_t> px;
    ASSERT_TRUE(gi::read_texture_rgba8_dx12(device, material, px));
    ASSERT_EQ(px.size(), static_cast<std::size_t>(kSize) * kSize * 4u);
    auto texel = [&](std::uint32_t x, std::uint32_t y) {
        return &px[(static_cast<std::size_t>(y) * kSize + x) * 4u];
    };

    // Inside the placed rect: the RED layer.
    const std::uint8_t* inside = texel(kSize / 4u, kSize / 4u);
    EXPECT_GT(inside[0], 200) << "layer did not composite into its placed rect";
    EXPECT_LT(inside[2], 60)  << "base colour bled through an opaque layer";

    // Outside it: the BLUE base survives (the layer is placed, not fullscreen).
    const std::uint8_t* outside = texel(kSize * 3u / 4u, kSize * 3u / 4u);
    EXPECT_GT(outside[2], 200) << "base colour missing outside the placed layer";
    EXPECT_LT(outside[0], 60)  << "layer covered the whole target (placement "
                                  "transform ignored)";

    // Moving the layer moves the art: same layer at the BOTTOM-RIGHT quadrant
    // must flip which quadrant is red -- this is the "move it around" knob.
    layer.center_uv[0] = 0.75f;
    layer.center_uv[1] = 0.75f;
    ASSERT_TRUE(wz::gpu::begin_frame(device));
    ASSERT_TRUE(gi::composite_texture_layers_dx12(
        device, material, base_blue, &layer, 1));
    ASSERT_TRUE(wz::gpu::end_frame(device));
    ASSERT_TRUE(gi::read_texture_rgba8_dx12(device, material, px));

    const std::uint8_t* moved_to = texel(kSize * 3u / 4u, kSize * 3u / 4u);
    const std::uint8_t* moved_from = texel(kSize / 4u, kSize / 4u);
    EXPECT_GT(moved_to[0], 200) << "layer did not follow its centre_uv";
    EXPECT_GT(moved_from[2], 200) << "layer did not leave its old position";

    wz::gpu::release_texture(device, material);
    wz::gpu::release_texture(device, layer_tex);
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
