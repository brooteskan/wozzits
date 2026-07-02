#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/scalar_field/scalar_field.h>
#include <engine/rendering/engine_gpu_context.h>
#include <gpu/gpu.h>
#include <gpu/gpu_resource_types.h>
#include <window/window2.h>

#include <wozzits/rhi/gpu_resource_registry.h>

#include <string>

// On-device coverage for #197: a procedural scalar field, resolved through an
// EngineGpuContext-backed library, publishes its GPU residency onto the
// wozzits-rhi GpuResourceRegistry as an R32F Texture2D. Because
// publish_resident_scalar_field releases the handle when the upload fails, a
// resolvable resource here proves the whole texture path
// (EngineGpuBackend::create -> create_texture_dx12, then update -> the upload)
// ran end-to-end on the GPU.

namespace
{
    namespace ea = wz::engine::assets;

    ea::EngineAssetLibrary make_assets(
        wz::engine::rendering::EngineGpuContext& gpu,
        wz::Logger& logger,
        const char* suffix)
    {
        const wz::fs::Path root =
            wz::fs::join(
                wz::fs::temp_directory_path(),
                std::string("wozzits_scalar_field_rhi_residency_") + suffix);
        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);
        return ea::EngineAssetLibrary(gpu, logger, root);
    }
}

TEST(ScalarFieldRhiResidency, ResolvePublishesTextureIntoRhiRegistry)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "scalar_field_rhi_residency_test";
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
        wz::engine::rendering::EngineGpuContext gpu(device);
        wz::Logger logger;
        auto assets = make_assets(gpu, logger, "publish");

        const ea::ScalarFieldAsset field =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "debug/gradient_x",
                .width = 8,
                .height = 4,
                .generator = ea::ScalarFieldGenerator::GradientX,
            });
        ASSERT_TRUE(field.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const wz::rhi::ResourceIdentity identity{
            ea::rhi_asset_identity(field.output, "field_texture"),
            {},
        };

        const wz::rhi::GpuResourceHandle handle = gpu.resources.find(identity);
        ASSERT_TRUE(handle.valid());

        const wz::rhi::GpuResource* resource = gpu.resources.get(handle);
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(resource->desc.identity, identity);
        EXPECT_EQ(resource->desc.dimension,
            wz::rhi::ResourceDimension::Texture2D);
        EXPECT_EQ(resource->desc.format, wz::rhi::TextureFormat::R32Float);
        EXPECT_EQ(resource->desc.usage, wz::rhi::ResourceUsage_Sampled);
        EXPECT_EQ(resource->desc.cpu_access,
            wz::rhi::ResourceCpuAccess::WriteOnce);
        EXPECT_EQ(resource->desc.width, 8u);
        EXPECT_EQ(resource->desc.height, 4u);
        EXPECT_EQ(resource->desc.depth, 1u);
        // #210: the resident height texture now carries its full box-filter mip
        // chain (floor(log2(max(w,h)))+1). For 8x4 that is 4 levels
        // (8x4 -> 4x2 -> 2x1 -> 1x1). A resolvable resource here proves every
        // level uploaded (publish releases the handle if any update_mip fails).
        EXPECT_EQ(resource->desc.mip_levels, 4u);

        // A valid backend GPUHandle (texture-typed) means create_texture_dx12
        // actually minted the texture for this resident resource.
        const wz::gpu::GPUHandle gpu_handle =
            gpu.backend.gpu_handle_for(resource->backend);
        ASSERT_TRUE(gpu_handle.valid());
        EXPECT_EQ(gpu_handle.type, wz::gpu::kGPUTextureResourceType);

        gpu.resources.release(handle);
        gpu.resources.collect(UINT64_MAX);
    }

    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
