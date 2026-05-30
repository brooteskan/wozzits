// file: src/gpu/hdr_image_texture.cpp

#include <gpu/hdr_image_texture.h>
#include <gpu/dx12/dx12_internal.h>
#include <engine/assets/hdri/hdri_image_loader.h>

namespace wz::gpu
{
    GPUHandle upload_hdr_image_texture(
        Device& device,
        const wz::engine::assets::HDRImageData& image)
    {
        if (!device.valid())
            return INVALID_GPU_HANDLE;

        if (!image.valid())
            return INVALID_GPU_HANDLE;

        return wz::gpu::dx12::internal::upload_hdr_image_texture_dx12(
            device,
            image
        );
    }
}
