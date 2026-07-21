#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/texture/texture.h>
#include <engine/assets/texture_asset_module.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/engine_gpu_context.h>

#include <file/filesystem.h>
#include <gpu/gpu.h>
#include <gpu/gpu_resource_types.h>
#include <logging/logger.h>
#include <window/window2.h>

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

// Seam 1 of the 2D-overlay track: an image file imported as a Texture asset.
// The compiler decodes a RawFile dependency to RGBA8 (stb_image), stores the
// CPU-side metadata (dimensions + colour space + usage), and -- with a device --
// publishes a resident RGBA8 Texture2D under rhi_asset_identity(key, "texture").
// The colour-space resolution (Auto derives from usage) is the load-bearing
// semantic, so it is covered device-free; residency is covered on-device.

namespace
{
    namespace ea = wz::engine::assets;

    // A distinct-per-texel image as an uncompressed 32-bit TGA, decoded back
    // through the real stb_image path in the compiler. Hand-written so the test
    // needs no image-WRITE library; non-square so a transposed width/height fails
    // rather than passing by coincidence. TGA stores pixels BGRA.
    std::vector<uint8_t> make_test_tga(int w, int h)
    {
        std::vector<uint8_t> tga;
        tga.push_back(0);              // id length
        tga.push_back(0);              // colour-map type
        tga.push_back(2);              // image type: uncompressed true-colour
        tga.insert(tga.end(), 5, 0);   // colour-map spec (unused)
        tga.push_back(0); tga.push_back(0);   // x origin
        tga.push_back(0); tga.push_back(0);   // y origin
        tga.push_back(static_cast<uint8_t>(w & 0xFF));
        tga.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
        tga.push_back(static_cast<uint8_t>(h & 0xFF));
        tga.push_back(static_cast<uint8_t>((h >> 8) & 0xFF));
        tga.push_back(32);             // bits per pixel
        tga.push_back(0x28);           // descriptor: top-left origin + 8 alpha bits
        for (int i = 0; i < w * h; ++i) {
            tga.push_back(static_cast<uint8_t>((i * 97) & 0xFF));   // B
            tga.push_back(static_cast<uint8_t>((i * 53) & 0xFF));   // G
            tga.push_back(static_cast<uint8_t>((i * 37) & 0xFF));   // R
            tga.push_back(255);                                    // A
        }
        return tga;
    }

    wz::fs::Path make_root(const char* suffix)
    {
        const wz::fs::Path root = wz::fs::join(
            wz::fs::temp_directory_path(),
            std::string("wozzits_texture_asset_") + suffix);
        EXPECT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
        return root;
    }

    // Write a PNG file into `root` and register it as a RawFile, returning its key.
    wz::asset::AssetKey stage_png(
        ea::EngineAssetLibrary& assets,
        const wz::fs::Path& root,
        const char* relative,
        int w, int h)
    {
        const std::vector<uint8_t> image = make_test_tga(w, h);
        const wz::fs::Path path = wz::fs::join(root, relative);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        EXPECT_TRUE(f.is_open()) << "failed to open " << path;
        f.write(
            reinterpret_cast<const char*>(image.data()),
            static_cast<std::streamsize>(image.size()));
        f.close();
        return assets.files().register_file_node(
            relative, ea::kRawFileSchema, ea::kAssetTypeRawFile);
    }

    // Compile a texture device-free and return its resolved metadata.
    const ea::TextureData* compile_texture(
        ea::EngineAssetLibrary& assets,
        const wz::asset::AssetKey& file_key,
        ea::TextureUsage usage,
        ea::TextureColorSpaceChoice color_space)
    {
        const ea::TextureAsset tex = assets.textures().create_from_file({
            .name        = "sprite",
            .source_file = file_key,
            .usage       = usage,
            .color_space = color_space,
        });
        EXPECT_TRUE(tex.valid());
        EXPECT_TRUE(assets.commit());
        EXPECT_TRUE(assets.resolve_all().ok());
        return assets.textures().get_texture_data(
            assets.textures().get_texture(tex));
    }
}

TEST(TextureAssetModule, ColorTextureAutoResolvesSrgbKeepingDimensions)
{
    const wz::fs::Path root = make_root("color");
    wz::gpu::Device device{};   // null device: decode + metadata are device-free
    wz::Logger logger;
    ea::EngineAssetLibrary assets{ device, logger, root };

    const wz::asset::AssetKey file = stage_png(assets, root, "sprite.png", 4, 2);
    ASSERT_FALSE(file == wz::asset::AssetKey{});

    const ea::TextureData* data = compile_texture(
        assets, file, ea::TextureUsage::Color,
        ea::TextureColorSpaceChoice::Auto);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->width, 4u);
    EXPECT_EQ(data->height, 2u);
    EXPECT_EQ(data->format, ea::TexturePixelFormat::RGBA8);
    EXPECT_EQ(data->usage, ea::TextureUsage::Color);
    // Auto + a colour/UI usage is display-referred.
    EXPECT_EQ(data->color_space, ea::TextureColorSpace::Srgb);
}

TEST(TextureAssetModule, DataTextureAutoResolvesLinear)
{
    const wz::fs::Path root = make_root("data");
    wz::gpu::Device device{};
    wz::Logger logger;
    ea::EngineAssetLibrary assets{ device, logger, root };

    const wz::asset::AssetKey file = stage_png(assets, root, "mask.png", 3, 5);
    ASSERT_FALSE(file == wz::asset::AssetKey{});

    const ea::TextureData* data = compile_texture(
        assets, file, ea::TextureUsage::Data,
        ea::TextureColorSpaceChoice::Auto);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->width, 3u);
    EXPECT_EQ(data->height, 5u);
    EXPECT_EQ(data->usage, ea::TextureUsage::Data);
    // Auto + data (mask / normal / LUT) is linear -- never sRGB-decoded.
    EXPECT_EQ(data->color_space, ea::TextureColorSpace::Linear);
}

TEST(TextureAssetModule, ExplicitColorSpaceOverridesUsageDefault)
{
    const wz::fs::Path root = make_root("override");
    wz::gpu::Device device{};
    wz::Logger logger;
    ea::EngineAssetLibrary assets{ device, logger, root };

    const wz::asset::AssetKey file = stage_png(assets, root, "odd.png", 2, 2);
    ASSERT_FALSE(file == wz::asset::AssetKey{});

    // A Color usage would default to sRGB, but an explicit Linear wins.
    const ea::TextureData* data = compile_texture(
        assets, file, ea::TextureUsage::Color,
        ea::TextureColorSpaceChoice::Linear);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->color_space, ea::TextureColorSpace::Linear);
}

TEST(TextureAssetModule, ImportPublishesTexture2DIntoRhiRegistry)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "texture_asset_module_test";
    window_desc.width = 64;
    window_desc.height = 64;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for on-device registry test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for on-device registry test";
    }

    {
        const wz::fs::Path root = make_root("residency");
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;
        ea::EngineAssetLibrary assets(gpu, logger, root);

        const wz::asset::AssetKey file = stage_png(assets, root, "sprite.png", 8, 4);
        ASSERT_FALSE(file == wz::asset::AssetKey{});

        const ea::TextureAsset tex = assets.textures().create_from_file({
            .name        = "sprite",
            .source_file = file,
            .usage       = ea::TextureUsage::UI,
            .color_space = ea::TextureColorSpaceChoice::Auto,
        });
        ASSERT_TRUE(tex.valid());
        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const wz::rhi::ResourceIdentity identity{
            ea::rhi_asset_identity(tex.output, "texture"), {},
        };
        const wz::rhi::GpuResourceHandle handle = gpu.resources.find(identity);
        ASSERT_TRUE(handle.valid()) << "texture did not become resident";

        const wz::rhi::GpuResource* resource = gpu.resources.get(handle);
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(resource->desc.dimension,
            wz::rhi::ResourceDimension::Texture2D);
        EXPECT_EQ(resource->desc.format, wz::rhi::TextureFormat::RGBA8Unorm);
        EXPECT_EQ(resource->desc.width, 8u);
        EXPECT_EQ(resource->desc.height, 4u);
        EXPECT_NE(resource->desc.usage & wz::rhi::ResourceUsage_Sampled, 0u);

        gpu.resources.release(handle);
        gpu.resources.collect(UINT64_MAX);
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
