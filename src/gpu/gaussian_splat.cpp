// src/gpu/gaussian_splat.cpp

#include <gpu/gaussian_splat.h>

#include <gpu/dx12/dx12_internal.h>

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
            *desc.cloud);
    }
}