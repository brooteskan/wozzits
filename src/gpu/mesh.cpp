// src/gpu/mesh.cpp

#include <gpu/mesh.h>

#include <gpu/dx12/dx12_internal.h>

namespace wz::gpu
{
    GPUHandle upload_mesh(
        Device& device,
        const MeshUploadDesc& desc)
    {
        if (!desc.valid())
            return {};

        return dx12::internal::upload_mesh_dx12(device, *desc.mesh);
    }

    bool release_mesh(
        Device& device,
        GPUHandle handle)
    {
        if (!device.valid() || !handle.valid())
            return false;

        return dx12::internal::release_mesh_dx12(device, handle);
    }
}
