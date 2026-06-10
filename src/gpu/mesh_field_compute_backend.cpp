// src/gpu/mesh_field_compute_backend.cpp
//
// Device-backed implementation of the asset layer's MeshFieldComputeBackend
// interface. The asset layer declares the factory; this translation unit
// defines it, keeping the asset→gpu dependency inverted.

#include <engine/assets/mesh_derived_field/mesh_field_compute.h>

#include <gpu/compute.h>
#include <gpu/gpu.h>
#include <gpu/mesh_field_visualization.h>

#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        wz::gpu::ComputeBindingKind to_gpu_binding_kind(
            MeshFieldComputeBackend::BindingKind kind) noexcept
        {
            switch (kind) {
            case MeshFieldComputeBackend::BindingKind::StructuredBufferSRV:
                return wz::gpu::ComputeBindingKind::StructuredBufferSRV;
            case MeshFieldComputeBackend::BindingKind::StructuredBufferUAV:
                return wz::gpu::ComputeBindingKind::StructuredBufferUAV;
            case MeshFieldComputeBackend::BindingKind::ByteAddressBufferSRV:
                return wz::gpu::ComputeBindingKind::ByteAddressBufferSRV;
            case MeshFieldComputeBackend::BindingKind::ByteAddressBufferUAV:
                break;
            }
            return wz::gpu::ComputeBindingKind::ByteAddressBufferUAV;
        }

        class GpuMeshFieldComputeBackend final : public MeshFieldComputeBackend
        {
        public:
            explicit GpuMeshFieldComputeBackend(wz::gpu::Device& device)
                : device_(device)
            {
            }

            bool available() const noexcept override
            {
                return device_.valid();
            }

            wz::asset::ResourceHandle create_compute_pipeline(
                const ComputePipelineData& data,
                wz::asset::ResourceHandle compute_shader) override
            {
                return wz::gpu::create_compute_pipeline(
                    device_, data, compute_shader);
            }

            wz::asset::ResourceHandle create_structured_buffer(
                const BufferDesc& desc) override
            {
                return wz::gpu::create_structured_buffer(
                    device_, to_gpu_buffer_desc(desc));
            }

            wz::asset::ResourceHandle create_rw_structured_buffer(
                const BufferDesc& desc) override
            {
                return wz::gpu::create_rw_structured_buffer(
                    device_, to_gpu_buffer_desc(desc));
            }

            bool dispatch(const DispatchDesc& desc) override
            {
                std::vector<wz::gpu::ComputeDispatchBinding> bindings;
                bindings.reserve(desc.bindings.size());
                for (const DispatchBinding& binding : desc.bindings) {
                    bindings.push_back(wz::gpu::ComputeDispatchBinding{
                        .kind = to_gpu_binding_kind(binding.kind),
                        .shader_register = binding.shader_register,
                        .register_space = binding.register_space,
                        .buffer = binding.buffer,
                    });
                }
                return wz::gpu::dispatch_compute(device_, {
                    .pipeline = desc.pipeline,
                    .bindings = bindings,
                    .root_constants = desc.root_constants,
                    .group_count_x = desc.group_count_x,
                    .group_count_y = desc.group_count_y,
                    .group_count_z = desc.group_count_z,
                });
            }

            std::vector<std::byte> readback_buffer(
                wz::asset::ResourceHandle buffer) override
            {
                return wz::gpu::readback_buffer(device_, buffer);
            }

            bool release_buffer(wz::asset::ResourceHandle handle) override
            {
                return wz::gpu::release_compute_buffer(device_, handle);
            }

            bool release_pipeline(wz::asset::ResourceHandle handle) override
            {
                return wz::gpu::release_compute_pipeline(device_, handle);
            }

            wz::asset::ResourceHandle
            create_field_visualization_from_gpu_source(
                wz::asset::ResourceHandle source_buffer,
                uint64_t byte_offset,
                uint32_t element_count,
                uint32_t stride_bytes) override
            {
                return wz::gpu::create_mesh_field_visualization_from_gpu_source(
                    device_,
                    source_buffer,
                    byte_offset,
                    element_count,
                    stride_bytes);
            }

            bool release_field_visualization(
                wz::asset::ResourceHandle handle) override
            {
                return wz::gpu::release_mesh_field_visualization(
                    device_, handle);
            }

        private:
            static wz::gpu::ComputeBufferDesc to_gpu_buffer_desc(
                const BufferDesc& desc) noexcept
            {
                return wz::gpu::ComputeBufferDesc{
                    .element_count = desc.element_count,
                    .stride_bytes = desc.stride_bytes,
                    .initial_data = desc.initial_data,
                    .initial_data_bytes = desc.initial_data_bytes,
                };
            }

            wz::gpu::Device& device_;
        };
    }

    std::unique_ptr<MeshFieldComputeBackend>
    make_gpu_mesh_field_compute_backend(wz::gpu::Device& device)
    {
        return std::make_unique<GpuMeshFieldComputeBackend>(device);
    }
}
