// tests/asset_texture/image_decode_limits_tests.cpp
//
// The FIRST test of decode_image_rgba8 (issue #310, A4-C12) -- it had none.
//
// stb sizes its pixel buffer from the image HEADER before decompressing
// anything, so a tiny file that merely CLAIMS enormous dimensions costs the
// full allocation, and costs it even when the payload is truncated and the
// decode ultimately fails. In the Inochi path the result was worse than
// transient: a decoded page is retained for the asset's lifetime, once per
// texture entry, with nothing bounding the entries.
//
// Measured with the cap neutered in place, on the `bomb` fixture below:
//     1042-byte file -> ok=1, 1,073,741,824 bytes of RGBA, 2052 MB peak
// i.e. it SUCCEEDED, so the gigabyte was kept. With the cap it is rejected at
// a peak of 4 MB.
//
// These fixtures use uncompressed 32-bit TGA because its 18-byte header carries
// the dimensions directly, so the file can be tiny while claiming any size --
// which is exactly the shape of the attack.

#include <gtest/gtest.h>

#include <engine/assets/texture/image_decode.h>

#include <cstdint>
#include <vector>

namespace
{
    // A TGA header declaring w x h, followed by `payload` bytes of pixel data
    // (deliberately fewer than the declared size demands).
    std::vector<std::uint8_t> tga_claiming(int w, int h, std::size_t payload)
    {
        std::vector<std::uint8_t> t;
        t.push_back(0); t.push_back(0); t.push_back(2);  // uncompressed RGB
        t.insert(t.end(), 5, 0);                         // colour-map spec
        t.push_back(0); t.push_back(0);                  // x origin
        t.push_back(0); t.push_back(0);                  // y origin
        t.push_back(static_cast<std::uint8_t>(w & 0xFF));
        t.push_back(static_cast<std::uint8_t>((w >> 8) & 0xFF));
        t.push_back(static_cast<std::uint8_t>(h & 0xFF));
        t.push_back(static_cast<std::uint8_t>((h >> 8) & 0xFF));
        t.push_back(32);                                 // bits per pixel
        t.push_back(0x28);                               // top-left, 8 alpha bits
        t.insert(t.end(), payload, 0x7Fu);
        return t;
    }
}

TEST(ImageDecodeLimits, DecodesAnOrdinarySmallImage)
{
    const auto bytes = tga_claiming(4, 4, 4u * 4u * 4u);
    const auto result = wz::engine::assets::decode_image_rgba8(bytes);

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.width, 4u);
    EXPECT_EQ(result.height, 4u);
    EXPECT_EQ(result.rgba8.size(), 4u * 4u * 4u);
}

// The cap must not cost us real content. A 4096-square atlas page is the
// largest thing this engine plausibly loads (the reference Inochi puppet uses
// three pages), and it is 64 MiB of RGBA -- comfortably inside the limit.
TEST(ImageDecodeLimits, StillAcceptsAPlausibleAtlasPage)
{
    const auto bytes = tga_claiming(4096, 4096, 1024);
    const auto result = wz::engine::assets::decode_image_rgba8(bytes);

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.width, 4096u);
    EXPECT_EQ(result.height, 4096u);
    EXPECT_EQ(result.rgba8.size(), 4096u * 4096u * 4u);
}

// The bomb: 1 KB in, 1 GiB of RGBA claimed. Rejected from the header, so the
// allocation never happens.
//
// If this test ever starts consuming gigabytes rather than failing fast, the
// header pre-check has been removed and A4-C12 is back.
TEST(ImageDecodeLimits, RejectsAnImageWhoseDecodedSizeExceedsTheLimit)
{
    const auto bytes = tga_claiming(16384, 16384, 1024);
    ASSERT_LT(bytes.size(), 2048u) << "fixture must stay tiny to be meaningful";

    const auto result = wz::engine::assets::decode_image_rgba8(bytes);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.rgba8.empty());
}

// The per-axis cap is D3D12's maximum 2D texture dimension: past it the image
// could never become a texture, so decoding it leads nowhere regardless of its
// total size. 20000 x 4 is only 320 KB decoded, so it is the axis limit rather
// than the byte limit doing the work here.
TEST(ImageDecodeLimits, RejectsAnAxisBeyondTheTextureDimensionLimit)
{
    const auto bytes = tga_claiming(20000, 4, 1024);
    const auto result = wz::engine::assets::decode_image_rgba8(bytes);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(ImageDecodeLimits, RejectsEmptyInput)
{
    const auto result =
        wz::engine::assets::decode_image_rgba8(std::span<const std::uint8_t>{});
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}
