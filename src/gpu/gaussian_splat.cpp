// src/gpu/gaussian_splat.cpp

#include <gpu/gaussian_splat.h>
#include <gpu/gaussian_splat_color_lod_settings.h>
#include <gpu/gaussian_splat_coverage_settings.h>

#include <gpu/dx12/dx12_internal.h>
#include "dx12/dx12_device_internal.h"

#include <cassert>

namespace wz::gpu
{
    GPUHandle upload_gaussian_splat_cloud(
        Device& device,
        const GaussianSplatCloudUploadDesc& desc)
    {
        if (!desc.valid())
            return {};

        return dx12::internal::upload_gaussian_splat_cloud_dx12(
            device,
            *desc.cloud,
            desc.color_lod);
    }

    bool release_gaussian_splat_cloud(
        Device& device,
        GPUHandle handle)
    {
        if (!device.valid() || !handle.valid())
            return false;

        return dx12::internal::release_gaussian_splat_cloud_dx12(
            device, handle);
    }

    void set_splat_color_lod_settings(
        Device& device,
        const SplatColorLODSettings& settings)
    {
        if (!device.impl)
            return;

        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        impl->splat_color_lod_settings = settings;
    }

    void set_splat_coverage_settings(
        Device& device,
        const SplatCoverageSettings& settings)
    {
        if (!device.impl)
            return;

        auto* impl = static_cast<wz::gpu::dx12::DX12Device*>(device.impl);
        impl->splat_coverage_settings = settings;
    }
}