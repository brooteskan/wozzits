#include <gtest/gtest.h>

#include <engine/assets/hdri/hdri_image_loader.h>
#include <engine/assets/hdri/hdri_lighting_metadata.h>

#include <external/tinyexr/tinyexr.h>

#include <cstdlib>
#include <cstdint>
#include <file/filesystem.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    struct TinyEXRBuffer
    {
        unsigned char* data = nullptr;
        int size = 0;

        ~TinyEXRBuffer()
        {
            std::free(data);
        }

        std::span<const uint8_t> bytes() const noexcept
        {
            return {
                reinterpret_cast<const uint8_t*>(data),
                static_cast<size_t>(size),
            };
        }
    };

    struct TinyEXRError
    {
        const char* message = nullptr;

        ~TinyEXRError()
        {
            if (message) {
                FreeEXRErrorMessage(message);
            }
        }
    };

    TinyEXRBuffer make_test_exr(
        int width,
        int height,
        const std::vector<float>& rgba)
    {
        TinyEXRBuffer out{};
        TinyEXRError error{};
        out.size = SaveEXRToMemory(
            rgba.data(),
            width,
            height,
            4,
            0,
            &out.data,
            &error.message);
        return out;
    }
}

TEST(HDRIImageLoader, DecodesOpenEXRMemoryToRGBAFloatPixels)
{
    using namespace wz::engine::assets;

    const std::vector<float> rgba{
        1.0f, 0.0f, 0.25f, 1.0f,
        0.5f, 0.75f, 1.5f, 1.0f,
    };
    TinyEXRBuffer exr = make_test_exr(2, 1, rgba);
    ASSERT_GT(exr.size, 0);
    ASSERT_NE(exr.data, nullptr);

    HDRImageData image{};
    std::string error;
    ASSERT_TRUE(load_openexr_image_from_memory(exr.bytes(), image, error))
        << error;

    EXPECT_TRUE(image.valid());
    EXPECT_EQ(image.width, 2u);
    EXPECT_EQ(image.height, 1u);
    EXPECT_EQ(image.channels, 4u);
    ASSERT_EQ(image.pixels.size(), rgba.size());
    for (size_t i = 0; i < rgba.size(); ++i) {
        EXPECT_FLOAT_EQ(image.pixels[i], rgba[i]);
    }
}

TEST(HDRIImageLoader, RejectsInvalidOpenEXRMemory)
{
    using namespace wz::engine::assets;

    const uint8_t bytes[]{ 'n', 'o', 't', ' ', 'e', 'x', 'r' };
    HDRImageData image{};
    std::string error;

    EXPECT_FALSE(load_openexr_image_from_memory(bytes, image, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(image.valid());
}

TEST(HDRIImageLoader, RejectsEmptyOpenEXRMemory)
{
    using namespace wz::engine::assets;

    HDRImageData image{};
    std::string error;

    EXPECT_FALSE(load_openexr_image_from_memory({}, image, error));
    EXPECT_EQ(error, "OpenEXR image payload is empty");
    EXPECT_FALSE(image.valid());
}

TEST(HDRIImageLoader, CachedFileDecodeReusesDecodedPixels)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root =
        wz::fs::join(
            wz::fs::temp_directory_path(),
            "wozzits_hdri_cached_file_decode_test");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    const std::vector<float> rgba{
        1.0f, 0.0f, 0.25f, 1.0f,
        0.5f, 0.75f, 1.5f, 1.0f,
    };
    TinyEXRBuffer exr = make_test_exr(2, 1, rgba);
    ASSERT_GT(exr.size, 0);
    const wz::fs::Path path = wz::fs::join(root, "cached.exr");
    ASSERT_EQ(
        wz::fs::write_file(
            path,
            wz::fs::Buffer(exr.bytes().begin(), exr.bytes().end()),
            true),
        wz::fs::FileError::None);

    std::shared_ptr<const HDRImageData> first;
    std::shared_ptr<const HDRImageData> second;
    std::string first_identity;
    std::string second_identity;
    std::string error;
    ASSERT_TRUE(openexr_image_file_identity_key(path, first_identity, error))
        << error;
    ASSERT_FALSE(first_identity.empty());
    ASSERT_TRUE(load_openexr_image_from_file_cached(path, first, error))
        << error;
    ASSERT_TRUE(load_openexr_image_from_file_cached(path, second, error))
        << error;
    ASSERT_TRUE(openexr_image_file_identity_key(path, second_identity, error))
        << error;

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(first_identity, second_identity);
    EXPECT_EQ(first->width, 2u);
    EXPECT_EQ(first->height, 1u);
}

TEST(HDRILightingMetadata, SampleResolutionLimitsLargeImageScan)
{
    using namespace wz::engine::assets;

    HDRImageData image{};
    image.width = 4;
    image.height = 2;
    image.channels = 4;
    image.pixels.assign(
        {
            10.0f, 10.0f, 10.0f, 1.0f,
            0.1f, 0.1f, 0.1f, 1.0f,
            0.1f, 0.1f, 0.1f, 1.0f,
            0.1f, 0.1f, 0.1f, 1.0f,
            0.1f, 0.1f, 0.1f, 1.0f,
            0.1f, 0.1f, 0.1f, 1.0f,
            0.1f, 0.1f, 0.1f, 1.0f,
            0.1f, 0.1f, 0.1f, 1.0f,
        });

    HDRILightingMetadata full{};
    HDRILightingMetadata sampled{};
    ASSERT_TRUE(derive_hdri_lighting_metadata(
        image,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0,
        full));
    ASSERT_TRUE(derive_hdri_lighting_metadata(
        image,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        2,
        sampled));

    EXPECT_GT(
        full.dominant_light_intensity,
        sampled.dominant_light_intensity);
}
