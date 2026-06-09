#pragma once

// gpu/compute.h

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wz::engine::assets
{
    struct ComputePipelineData;
}

namespace wz::gpu
{
    enum class ComputeBindingKind : uint8_t
    {
        StructuredBufferSRV,
        StructuredBufferUAV,
        ByteAddressBufferSRV,
        ByteAddressBufferUAV,
    };

    struct ComputeBufferDesc
    {
        uint32_t element_count = 0;
        uint32_t stride_bytes = 0;
        const void* initial_data = nullptr;
        uint64_t initial_data_bytes = 0;

        bool valid() const noexcept
        {
            return element_count > 0u && stride_bytes > 0u;
        }
    };

    struct ComputeDispatchBinding
    {
        ComputeBindingKind kind{};
        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        GPUHandle buffer{};
    };

    struct ComputeDispatchDesc
    {
        GPUHandle pipeline{};
        std::span<const ComputeDispatchBinding> bindings{};
        std::span<const uint32_t> root_constants{};
        uint32_t group_count_x = 1;
        uint32_t group_count_y = 1;
        uint32_t group_count_z = 1;

        bool valid() const noexcept
        {
            return pipeline.valid()
                && group_count_x > 0u
                && group_count_y > 0u
                && group_count_z > 0u;
        }
    };

    [[nodiscard]] GPUHandle create_structured_buffer(
        Device& device,
        const ComputeBufferDesc& desc);

    [[nodiscard]] GPUHandle create_rw_structured_buffer(
        Device& device,
        const ComputeBufferDesc& desc);

    [[nodiscard]] GPUHandle create_compute_pipeline(
        Device& device,
        const wz::engine::assets::ComputePipelineData& data,
        GPUHandle compute_shader);

    bool dispatch_compute(Device& device, const ComputeDispatchDesc& desc);

    [[nodiscard]] std::vector<std::byte> readback_buffer(
        Device& device,
        GPUHandle buffer);

    bool release_compute_buffer(Device& device, GPUHandle handle);
    bool release_compute_pipeline(Device& device, GPUHandle handle);
}
