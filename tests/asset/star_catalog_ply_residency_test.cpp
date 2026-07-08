#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/rendering/engine_gpu_context.h>
#include <gpu/gpu.h>
#include <gpu/gpu_resource_types.h>
#include <window/window2.h>

#include <wozzits/rhi/gpu_resource_registry.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

// On-device coverage for Seam T-2 (issue #266): a baked binary PLY point cloud
// (the tycho2_prep output shape -- per-vertex x/y/z direction + vmag + bv),
// registered as a raw file and imported through StarCatalogAssetModule::
// create_from_ply, runs the starfield kernel at compile and publishes the built
// stars as a resident StructuredBuffer under "star_catalog" -- the SAME identity
// the JSON path and the render branch use. Byte-for-byte analog of the JSON
// star-catalog residency test, over the PLY importer instead.

namespace
{
    namespace ea = wz::engine::assets;

    // Build a binary-little-endian PLY: three stars, all inside the default
    // magnitude window -> 3 * 32 = 96 resident bytes.
    std::vector<char> make_star_ply()
    {
        struct V { float x, y, z, vmag, bv; };
        const V verts[3] = {
            { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f, 3.0f, 0.5f },
            { 0.0f, 0.0f, 1.0f, 5.0f, 1.0f },
        };
        const std::string header =
            "ply\n"
            "format binary_little_endian 1.0\n"
            "element vertex 3\n"
            "property float x\n"
            "property float y\n"
            "property float z\n"
            "property float vmag\n"
            "property float bv\n"
            "end_header\n";
        std::vector<char> bytes(header.begin(), header.end());
        const auto* raw = reinterpret_cast<const char*>(verts);
        bytes.insert(bytes.end(), raw, raw + sizeof(verts));
        return bytes;
    }

    wz::fs::Path make_root(const char* suffix)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_star_ply_residency_") + suffix);
        EXPECT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);
        return root;
    }
}

TEST(StarCatalogPlyResidency, ImportPublishesStarBufferIntoRhiRegistry)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "star_catalog_ply_residency_test";
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
        const wz::fs::Path root = make_root("import");

        const std::string relative_ply = "test.ply";
        {
            const std::vector<char> ply = make_star_ply();
            const wz::fs::Path ply_path = wz::fs::join(root, relative_ply);
            std::ofstream f(ply_path, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(f.is_open()) << "failed to open " << ply_path;
            f.write(ply.data(), static_cast<std::streamsize>(ply.size()));
        }

        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;
        ea::EngineAssetLibrary assets(gpu, logger, root);

        const wz::asset::AssetKey file_key =
            assets.files().register_file_node(
                relative_ply,
                ea::kRawFileSchema,
                ea::kAssetTypeRawFile);
        ASSERT_FALSE(file_key == wz::asset::AssetKey{});

        wz::engine::starfield::StarImportParams params;  // faithful defaults
        const ea::StarCatalogAsset catalog =
            assets.star_catalogs().create_from_ply({
                .name = "stars/ply",
                .source_file = file_key,
                .params = params,
            });
        ASSERT_TRUE(catalog.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const wz::rhi::ResourceIdentity identity{
            ea::rhi_asset_identity(catalog.output, "star_catalog"), {},
        };
        const wz::rhi::GpuResourceHandle handle = gpu.resources.find(identity);
        ASSERT_TRUE(handle.valid())
            << "PLY star catalog did not become resident";

        const wz::rhi::GpuResource* resource = gpu.resources.get(handle);
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(resource->desc.dimension, wz::rhi::ResourceDimension::Buffer);
        EXPECT_EQ(resource->desc.usage, wz::rhi::ResourceUsage_Sampled);
        EXPECT_EQ(resource->desc.stride_bytes, 32u);
        EXPECT_EQ(resource->desc.size_bytes, 3u * 32u);   // 3 stars
        ASSERT_TRUE(
            gpu.backend.gpu_handle_for(resource->backend).valid());

        gpu.resources.release(handle);
        gpu.resources.collect(UINT64_MAX);
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
