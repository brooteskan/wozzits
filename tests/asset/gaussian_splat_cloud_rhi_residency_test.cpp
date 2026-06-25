#include <gtest/gtest.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/rendering/engine_gpu_context.h>
#include <gpu/gpu.h>
#include <gpu/gpu_resource_types.h>
#include <window/window2.h>

#include <wozzits/rhi/gpu_resource_registry.h>

#include <string>

// On-device coverage for #208: a scalar-field-derived gaussian splat cloud,
// resolved through an EngineGpuContext-backed library, publishes its GPU
// residency onto the wozzits-rhi GpuResourceRegistry as a decoded splat
// StructuredBuffer (64-byte stride, ResourceUsage_Sampled, WriteOnce). Because
// publish_resident_gaussian_splat_cloud releases the handle when the upload
// fails, a resolvable resource here proves the whole buffer path
// (EngineGpuBackend::create -> create_buffer, then update -> the upload) ran
// end-to-end on the GPU. It also asserts the buffer's identity matches the one
// RhiSceneRenderer::ensure_renderable looks it up by — the discriminator
// "splat_cloud" against the splat-cloud asset key — so residency and rendering
// agree.

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
                std::string("wozzits_gaussian_splat_cloud_rhi_residency_")
                    + suffix);
        EXPECT_EQ(
            wz::fs::create_directories(root),
            wz::fs::FileError::None);
        return ea::EngineAssetLibrary(gpu, logger, root);
    }
}

TEST(GaussianSplatCloudRhiResidency, ResolvePublishesSplatBufferIntoRhiRegistry)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "gaussian_splat_cloud_rhi_residency_test";
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

        // A small procedural field, then the field -> splat-cloud converter
        // with a subsample step so the emitted count is exactly known: a 16x8
        // field at subsample_step=2 emits ceil(16/2) x ceil(8/2) = 8 x 4 = 32
        // splats.
        const ea::ScalarFieldAsset field =
            assets.scalar_fields().create_procedural_scalar_field({
                .name = "splat/height",
                .width = 16,
                .height = 8,
                .generator = ea::ScalarFieldGenerator::RadialGradient,
            });
        ASSERT_TRUE(field.valid());

        ea::GaussianSplatFromScalarFieldDesc cloud_desc{};
        cloud_desc.name = "splat/cloud";
        cloud_desc.scalar_field_key = field.output;
        cloud_desc.height_scale = 4.0f;
        cloud_desc.splat_scale = 0.1f;
        cloud_desc.opacity = 0.9f;
        cloud_desc.subsample_step = 2;
        const ea::GaussianSplatCloudAsset cloud =
            assets.gaussian_splats().create_from_scalar_field(cloud_desc);
        ASSERT_TRUE(cloud.valid());

        ASSERT_TRUE(assets.commit());
        ASSERT_TRUE(assets.resolve_all().ok());

        const wz::rhi::ResourceIdentity identity{
            ea::rhi_asset_identity(cloud.output, "splat_cloud"),
            {},
        };

        const wz::rhi::GpuResourceHandle handle = gpu.resources.find(identity);
        ASSERT_TRUE(handle.valid())
            << "splat cloud did not become resident as a structured buffer";

        const wz::rhi::GpuResource* resource = gpu.resources.get(handle);
        ASSERT_NE(resource, nullptr);
        EXPECT_EQ(resource->desc.identity, identity);
        EXPECT_EQ(resource->desc.dimension,
            wz::rhi::ResourceDimension::Buffer);
        EXPECT_EQ(resource->desc.usage, wz::rhi::ResourceUsage_Sampled);
        EXPECT_EQ(resource->desc.cpu_access,
            wz::rhi::ResourceCpuAccess::WriteOnce);
        // 64-byte structured stride (the decoded splat record) — must match the
        // HLSL Splat struct the SplatPull VS reads.
        EXPECT_EQ(resource->desc.stride_bytes, 64u);
        // 8 x 4 = 32 splats * 64 bytes.
        EXPECT_EQ(resource->desc.size_bytes, 32u * 64u);

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
