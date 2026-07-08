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

// On-device coverage for Seam C-2 (issue #266): a baked .star_catalog.json,
// resolved through an EngineGpuContext-backed library, runs the starfield
// astronomy kernel at compile time and publishes the built stars onto the
// wozzits-rhi GpuResourceRegistry as a resident StructuredBuffer (32-byte
// ResidentStar stride, ResourceUsage_Sampled, WriteOnce). Because
// publish_resident_star_catalog releases the handle when the upload fails, a
// resolvable resource here proves the whole buffer path ran end-to-end on the
// GPU. The identity discriminator "star_catalog" against the catalog asset key
// (render_binding_sources.h) is the one a star render binding resolves against,
// so residency and rendering agree. Byte-for-byte analog of the sky-gaussian
// points residency test.

namespace
{
    namespace ea = wz::engine::assets;

    // Baked star-catalog JSON: three real bright stars (RA hours / Dec deg /
    // Vmag / B-V). All well inside the default magnitude window, so the built
    // catalog keeps 3 -> 3 * 32 = 96 bytes at a 32-byte stride.
    const std::string kStarJson = R"JSON({
  "version": 1,
  "source_name": "test_stars",
  "params": { "exposure": 1.0, "magnitude_max": 6.5 },
  "stars": [
    { "ra_hours": 6.7525, "dec_deg": -16.7161, "vmag": -1.46, "bv": 0.00 },
    { "ra_hours": 18.6156, "dec_deg": 38.7837, "vmag": 0.03, "bv": 0.00 },
    { "ra_hours": 5.9195, "dec_deg": 7.4071, "vmag": 0.42, "bv": 1.85 }
  ]
})JSON";

    wz::fs::Path make_root(const char* suffix)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_star_catalog_rhi_residency_") + suffix);
        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);
        return root;
    }
}

TEST(StarCatalogRhiResidency, ResolvePublishesStarBufferIntoRhiRegistry)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "star_catalog_rhi_residency_test";
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

        const std::string relative_json = "test.star_catalog.json";
        {
            const wz::fs::Path json_path = wz::fs::join(root, relative_json);
            std::ofstream f(json_path, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(f.is_open()) << "failed to open " << json_path;
            f.write(
                kStarJson.data(),
                static_cast<std::streamsize>(kStarJson.size()));
        }

        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;
        ea::EngineAssetLibrary assets(gpu, logger, root);

        // JSON document -> star catalog. The compiler reads the compiled
        // JSONDocument, deserializes the rows + dials, builds the stars, and
        // publishes residency.
        const ea::JSONAsset json = assets.json().create_json({
            .name = "stars/json",
            .path = relative_json,
        });
        ASSERT_TRUE(json.valid());

        const ea::StarCatalogAsset catalog =
            assets.star_catalogs().create_from_json({
                .name = "stars/set",
                .json_key = json.output,
            });
        ASSERT_TRUE(catalog.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const wz::rhi::ResourceIdentity identity{
            ea::rhi_asset_identity(catalog.output, "star_catalog"),
            {},
        };

        const wz::rhi::GpuResourceHandle handle = gpu.resources.find(identity);
        ASSERT_TRUE(handle.valid())
            << "star catalog did not become resident as a structured buffer";

        const wz::rhi::GpuResource* resource = gpu.resources.get(handle);
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(resource->desc.identity, identity);
        EXPECT_EQ(resource->desc.dimension,
            wz::rhi::ResourceDimension::Buffer);
        EXPECT_EQ(resource->desc.usage, wz::rhi::ResourceUsage_Sampled);
        EXPECT_EQ(resource->desc.cpu_access,
            wz::rhi::ResourceCpuAccess::WriteOnce);
        // 32-byte structured stride (the resident star record) -- must match the
        // ResidentStar / point-source StructuredBuffer stride.
        EXPECT_EQ(resource->desc.stride_bytes, 32u);
        // 3 stars * 32 bytes.
        EXPECT_EQ(resource->desc.size_bytes, 3u * 32u);

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
