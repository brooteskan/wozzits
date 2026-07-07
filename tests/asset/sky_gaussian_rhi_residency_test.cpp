#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/rendering/engine_gpu_context.h>
#include <gpu/gpu.h>
#include <gpu/gpu_resource_types.h>
#include <window/window2.h>

#include <wozzits/rhi/gpu_resource_registry.h>

#include <fstream>
#include <ios>
#include <string>

// On-device coverage for Seam B1c (issue #260): a baked .sky_gaussian.json,
// resolved through an EngineGpuContext-backed library, publishes its
// spherical-Gaussian lobes onto the wozzits-rhi GpuResourceRegistry as a
// resident StructuredBuffer (32-byte stride, ResourceUsage_Sampled, WriteOnce).
// Because publish_resident_sky_gaussian releases the handle when the upload
// fails, a resolvable resource here proves the whole buffer path
// (EngineGpuBackend::create -> create_buffer, then update -> the upload) ran
// end-to-end on the GPU. It also asserts the buffer's identity matches the one
// a sky render binding resolves against — the discriminator "sky_gaussian"
// against the sky-gaussian asset key (render_binding_sources.h) — so residency
// and rendering agree. This is the byte-for-byte analog of the gaussian splat
// cloud residency test.

namespace
{
    namespace ea = wz::engine::assets;

    // Baked sky-gaussian JSON with exactly two lobes (Seam A schema v1). Two
    // lobes → 2 * 32 = 64 bytes at a 32-byte stride.
    const std::string kSkyJson = R"JSON({
  "version": 1,
  "source_name": "test_sky",
  "source_width": 4,
  "source_height": 2,
  "lobes": [
    { "direction": [0.0, 1.0, 0.0], "sharpness": 2.0, "amplitude": [1.0, 0.9, 0.8] },
    { "direction": [1.0, 0.0, 0.0], "sharpness": 4.0, "amplitude": [0.2, 0.3, 0.4] }
  ],
  "point_sources": []
})JSON";

    wz::fs::Path make_root(const char* suffix)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_sky_gaussian_rhi_residency_") + suffix);
        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);
        return root;
    }
}

TEST(SkyGaussianRhiResidency, ResolvePublishesLobeBufferIntoRhiRegistry)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "sky_gaussian_rhi_residency_test";
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
        const wz::fs::Path root = make_root("publish");

        // Write the baked JSON sidecar into the resource root so the JSON
        // document asset can read it (path-based file node) at compile time.
        const std::string relative_json = "test.sky_gaussian.json";
        {
            const wz::fs::Path json_path = wz::fs::join(root, relative_json);
            std::ofstream f(json_path, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(f.is_open()) << "failed to open " << json_path;
            f.write(
                kSkyJson.data(),
                static_cast<std::streamsize>(kSkyJson.size()));
        }

        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;
        ea::EngineAssetLibrary assets(gpu, logger, root);

        // JSON document → sky-gaussian set. The sky-gaussian compiler reads the
        // compiled JSONDocument, deserializes the lobes, and publishes residency.
        const ea::JSONAsset json = assets.json().create_json({
            .name = "sky/json",
            .path = relative_json,
        });
        ASSERT_TRUE(json.valid());

        const ea::SkyGaussianAsset sky =
            assets.sky_gaussians().create_from_json({
                .name = "sky/set",
                .json_key = json.output,
            });
        ASSERT_TRUE(sky.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const wz::rhi::ResourceIdentity identity{
            ea::rhi_asset_identity(sky.output, "sky_gaussian"),
            {},
        };

        const wz::rhi::GpuResourceHandle handle = gpu.resources.find(identity);
        ASSERT_TRUE(handle.valid())
            << "sky gaussian did not become resident as a structured buffer";

        const wz::rhi::GpuResource* resource = gpu.resources.get(handle);
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(resource->desc.identity, identity);
        EXPECT_EQ(resource->desc.dimension,
            wz::rhi::ResourceDimension::Buffer);
        EXPECT_EQ(resource->desc.usage, wz::rhi::ResourceUsage_Sampled);
        EXPECT_EQ(resource->desc.cpu_access,
            wz::rhi::ResourceCpuAccess::WriteOnce);
        // 32-byte structured stride (the resident lobe record) — must match the
        // ResidentSkyLobe layout the (future) sky VS reads.
        EXPECT_EQ(resource->desc.stride_bytes, 32u);
        // 2 lobes * 32 bytes.
        EXPECT_EQ(resource->desc.size_bytes, 2u * 32u);

        // A valid backend GPUHandle (buffer-typed) means the buffer was minted.
        const wz::gpu::GPUHandle gpu_handle =
            gpu.backend.gpu_handle_for(resource->backend);
        ASSERT_TRUE(gpu_handle.valid());

        gpu.resources.release(handle);
        gpu.resources.collect(UINT64_MAX);
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
