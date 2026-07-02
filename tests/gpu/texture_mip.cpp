// tests/gpu/texture_mip.cpp
//
// Device-level coverage for #209 mip-chain support on the generic engine
// texture path (gpu/texture.h -> dx12_texture.cpp). A real device is required
// to create the committed texture and run the per-mip upload; GPU readback is
// impractical in this harness, so the assertions are "create + per-mip write +
// SRV-capable state succeed" (the upload path transitions to a shader-resource
// state) plus the validation rule for the mip count. These are the strongest
// checks available without a readback path, and match the residency tests'
// posture.

#include <gtest/gtest.h>

#include <gpu/dx12/dx12_descriptor_allocator.h>
#include <gpu/dx12/dx12_internal.h>
#include <gpu/gpu.h>
#include <gpu/texture.h>
#include <window/window2.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <vector>

namespace
{
    struct GpuDeviceFixture : public ::testing::Test
    {
        wz::window::WindowHandle window{};
        wz::gpu::Device          device{};

        void SetUp() override
        {
            wz::window::WindowDesc desc{};
            desc.title     = "texture_mip_test";
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

    // Tightly-packed R32Float payload of `count` floats.
    std::vector<float> ramp(uint32_t count, float base)
    {
        std::vector<float> v(count);
        for (uint32_t i = 0; i < count; ++i) {
            v[i] = base + static_cast<float>(i);
        }
        return v;
    }
}

// ── Validation (no device needed): mip count vs. dimensions ───────────────────

TEST(TextureMipValidation, SingleMipIsDefaultAndValid)
{
    wz::gpu::TextureDesc d{};
    d.width = 64; d.height = 32; d.format = wz::gpu::TextureFormat::R32Float;
    EXPECT_EQ(d.mip_levels, 1u);
    EXPECT_TRUE(d.valid());
}

TEST(TextureMipValidation, FullChainIsValid)
{
    // 64x32 -> log2(64)+1 = 7 levels.
    EXPECT_EQ(wz::gpu::max_mip_levels(64, 32), 7u);
    wz::gpu::TextureDesc d{};
    d.width = 64; d.height = 32; d.mip_levels = 7;
    d.format = wz::gpu::TextureFormat::R32Float;
    EXPECT_TRUE(d.valid());
}

TEST(TextureMipValidation, ExcessMipLevelsRejected)
{
    // One more than the chain can hold -> invalid (rejected, not clamped).
    wz::gpu::TextureDesc d{};
    d.width = 64; d.height = 32; d.mip_levels = 8;
    d.format = wz::gpu::TextureFormat::R32Float;
    EXPECT_FALSE(d.valid());
}

TEST(TextureMipValidation, ZeroMipLevelsRejected)
{
    wz::gpu::TextureDesc d{};
    d.width = 64; d.height = 32; d.mip_levels = 0;
    d.format = wz::gpu::TextureFormat::R32Float;
    EXPECT_FALSE(d.valid());
}

TEST(TextureMipValidation, Texture3dMipChainRejected)
{
    wz::gpu::TextureDesc d{};
    d.dimension = wz::gpu::TextureDimension::Texture3D;
    d.width = 16; d.height = 16; d.depth = 4; d.mip_levels = 2;
    d.format = wz::gpu::TextureFormat::R32Float;
    EXPECT_FALSE(d.valid());

    d.mip_levels = 1;  // single-mip Texture3D is fine
    EXPECT_TRUE(d.valid());
}

// ── Device-level: create a 3-mip texture, upload each level, SRV ──────────────

TEST_F(GpuDeviceFixture, CreateThreeMipTextureAndUploadEachLevel)
{
    // 8x8 R32Float, 3 mips: 8x8, 4x4, 2x2.
    wz::gpu::TextureDesc desc{};
    desc.dimension  = wz::gpu::TextureDimension::Texture2D;
    desc.width      = 8;
    desc.height     = 8;
    desc.mip_levels = 3;
    desc.format     = wz::gpu::TextureFormat::R32Float;
    ASSERT_TRUE(desc.valid());

    const wz::gpu::GPUHandle tex = wz::gpu::create_texture(device, desc);
    ASSERT_TRUE(tex.valid());

    // Distinct payloads per level, each tightly packed for its dimensions.
    const std::vector<float> mip0 = ramp(8 * 8, 100.0f);
    const std::vector<float> mip1 = ramp(4 * 4, 200.0f);
    const std::vector<float> mip2 = ramp(2 * 2, 300.0f);

    EXPECT_TRUE(wz::gpu::update_texture_mip(
        device, tex, 0, mip0.data(), mip0.size() * sizeof(float)));
    EXPECT_TRUE(wz::gpu::update_texture_mip(
        device, tex, 1, mip1.data(), mip1.size() * sizeof(float)));
    EXPECT_TRUE(wz::gpu::update_texture_mip(
        device, tex, 2, mip2.data(), mip2.size() * sizeof(float)));

    // The underlying resource must report the full mip chain, and an SRV over
    // it (full-chain view) must be creatable.
    const wz::gpu::dx12::internal::DX12Texture* dx =
        wz::gpu::dx12::internal::get_dx12_texture(device, tex);
    ASSERT_NE(dx, nullptr);
    EXPECT_EQ(dx->mip_levels, 3u);
    EXPECT_EQ(dx->texture->GetDesc().MipLevels, 3);

    wz::gpu::dx12::DX12DescriptorAllocator alloc;
    ASSERT_TRUE(alloc.init(d3d_device(), 4));
    const wz::gpu::dx12::DX12DescriptorTable table = alloc.allocate(1);
    ASSERT_TRUE(table.valid());
    // Views the whole chain (MipLevels = -1); no crash / debug-layer break.
    alloc.create_texture_srv(table, 0, dx->texture, dx->format, /*is_3d*/ false);
    alloc.destroy();

    EXPECT_TRUE(wz::gpu::release_texture(device, tex));
}

TEST_F(GpuDeviceFixture, MipWriteWrongSizeRejected)
{
    wz::gpu::TextureDesc desc{};
    desc.width = 8; desc.height = 8; desc.mip_levels = 3;
    desc.format = wz::gpu::TextureFormat::R32Float;
    const wz::gpu::GPUHandle tex = wz::gpu::create_texture(device, desc);
    ASSERT_TRUE(tex.valid());

    // mip 1 is 4x4 = 16 floats; a full-res (64-float) payload must be rejected.
    const std::vector<float> wrong = ramp(8 * 8, 1.0f);
    EXPECT_FALSE(wz::gpu::update_texture_mip(
        device, tex, 1, wrong.data(), wrong.size() * sizeof(float)));

    // Out-of-range mip level rejected.
    const std::vector<float> mip2 = ramp(2 * 2, 1.0f);
    EXPECT_FALSE(wz::gpu::update_texture_mip(
        device, tex, 3, mip2.data(), mip2.size() * sizeof(float)));

    EXPECT_TRUE(wz::gpu::release_texture(device, tex));
}

TEST_F(GpuDeviceFixture, SingleMipUploadUnchanged)
{
    // The pre-#209 path: default single mip, full-res write via update_texture.
    wz::gpu::TextureDesc desc{};
    desc.width = 8; desc.height = 8;  // mip_levels defaults to 1
    desc.format = wz::gpu::TextureFormat::R32Float;
    const wz::gpu::GPUHandle tex = wz::gpu::create_texture(device, desc);
    ASSERT_TRUE(tex.valid());

    const std::vector<float> data = ramp(8 * 8, 5.0f);
    EXPECT_TRUE(wz::gpu::update_texture(
        device, tex, data.data(), data.size() * sizeof(float)));

    const wz::gpu::dx12::internal::DX12Texture* dx =
        wz::gpu::dx12::internal::get_dx12_texture(device, tex);
    ASSERT_NE(dx, nullptr);
    EXPECT_EQ(dx->mip_levels, 1u);

    EXPECT_TRUE(wz::gpu::release_texture(device, tex));
}
