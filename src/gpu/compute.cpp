#include <gpu/compute.h>
#include <gpu/dx12/dx12_internal.h>

namespace wz::gpu
{
    GPUHandle create_structured_buffer(
        Device& device,
        const ComputeBufferDesc& desc)
    {
        return dx12::internal::create_structured_buffer_dx12(
            device,
            desc,
            false);
    }

    GPUHandle create_rw_structured_buffer(
        Device& device,
        const ComputeBufferDesc& desc)
    {
        return dx12::internal::create_structured_buffer_dx12(
            device,
            desc,
            true);
    }

    GPUHandle create_compute_pipeline(
        Device& device,
        const wz::engine::assets::ComputePipelineData& data,
        GPUHandle compute_shader)
    {
        return dx12::internal::create_compute_pipeline_dx12(
            device,
            data,
            compute_shader);
    }

    bool dispatch_compute(Device& device, const ComputeDispatchDesc& desc)
    {
        return dx12::internal::dispatch_compute_dx12(device, desc);
    }

    std::vector<std::byte> readback_buffer(Device& device, GPUHandle buffer)
    {
        return dx12::internal::readback_buffer_dx12(device, buffer);
    }

    bool release_compute_buffer(Device& device, GPUHandle handle)
    {
        return dx12::internal::release_compute_buffer_dx12(device, handle);
    }

    bool release_compute_pipeline(Device& device, GPUHandle handle)
    {
        return dx12::internal::release_compute_pipeline_dx12(device, handle);
    }
}
