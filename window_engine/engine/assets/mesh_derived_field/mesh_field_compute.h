#pragma once

// engine/assets/mesh_derived_field/mesh_field_compute.h
//
// GPU compute service used by mesh-derived-field compilation and the
// GPU-resident field table. The asset layer owns only this interface; the
// device-backed implementation lives in the gpu layer
// (src/gpu/mesh_field_compute_backend.cpp) and is injected when compilers
// are registered, so asset code never includes gpu headers.

#include <asset/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace wz::gpu
{
    struct Device;
}

namespace wz::engine::assets
{
    struct ComputePipelineData;

    class MeshFieldComputeBackend
    {
    public:
        enum class BindingKind : uint8_t
        {
            StructuredBufferSRV,
            StructuredBufferUAV,
            ByteAddressBufferSRV,
            ByteAddressBufferUAV,
        };

        struct BufferDesc
        {
            uint32_t element_count = 0;
            uint32_t stride_bytes = 0;
            const void* initial_data = nullptr;
            uint64_t initial_data_bytes = 0;
        };

        struct DispatchBinding
        {
            BindingKind kind{};
            uint32_t shader_register = 0;
            uint32_t register_space = 0;
            wz::asset::ResourceHandle buffer{};
        };

        struct DispatchDesc
        {
            wz::asset::ResourceHandle pipeline{};
            std::span<const DispatchBinding> bindings{};
            std::span<const uint32_t> root_constants{};
            uint32_t group_count_x = 1;
            uint32_t group_count_y = 1;
            uint32_t group_count_z = 1;
        };

        virtual ~MeshFieldComputeBackend() = default;

        // False when no usable GPU device is bound; callers fall back to the
        // CPU reference path.
        virtual bool available() const noexcept = 0;

        [[nodiscard]] virtual wz::asset::ResourceHandle create_compute_pipeline(
            const ComputePipelineData& data,
            wz::asset::ResourceHandle compute_shader) = 0;

        [[nodiscard]] virtual wz::asset::ResourceHandle create_structured_buffer(
            const BufferDesc& desc) = 0;

        [[nodiscard]] virtual wz::asset::ResourceHandle create_rw_structured_buffer(
            const BufferDesc& desc) = 0;

        virtual bool dispatch(const DispatchDesc& desc) = 0;

        [[nodiscard]] virtual std::vector<std::byte> readback_buffer(
            wz::asset::ResourceHandle buffer) = 0;

        virtual bool release_buffer(wz::asset::ResourceHandle handle) = 0;
        virtual bool release_pipeline(wz::asset::ResourceHandle handle) = 0;

        [[nodiscard]] virtual wz::asset::ResourceHandle
        create_field_visualization_from_gpu_source(
            wz::asset::ResourceHandle source_buffer,
            uint64_t byte_offset,
            uint32_t element_count,
            uint32_t stride_bytes) = 0;

        virtual bool release_field_visualization(
            wz::asset::ResourceHandle handle) = 0;
    };

    // Defined in the gpu layer (src/gpu/mesh_field_compute_backend.cpp).
    // Declared here so EngineAssetLibrary can construct the device-backed
    // implementation without including gpu headers.
    [[nodiscard]] std::unique_ptr<MeshFieldComputeBackend>
    make_gpu_mesh_field_compute_backend(wz::gpu::Device& device);
}
