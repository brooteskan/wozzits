#include <gpu/mesh_field_visualization.h>

#include <gpu/dx12/dx12_internal.h>

#include <algorithm>

namespace wz::gpu
{
    namespace
    {
        const wz::engine::assets::MeshDerivedFieldChannel* find_float1_channel(
            const MeshFieldVisualizationUploadDesc& desc) noexcept
        {
            if (!desc.field) {
                return nullptr;
            }

            const auto found = std::find_if(
                desc.field->channels.begin(),
                desc.field->channels.end(),
                [&](const wz::engine::assets::MeshDerivedFieldChannel& channel)
                {
                    return channel.channel_id == desc.channel_id;
                });

            if (found == desc.field->channels.end()
                || found->value_type
                    != wz::engine::assets::MeshDerivedFieldValueType::Float1)
            {
                return nullptr;
            }
            return &*found;
        }
    }

    bool MeshFieldVisualizationUploadDesc::valid() const noexcept
    {
        if (!mesh || !mesh->valid() || !field || !field->valid()) {
            return false;
        }
        if (channel_id == 0u
            || field->domain
                != wz::engine::assets::MeshDerivedFieldDomain::Vertex
            || field->element_count != mesh->vertex_count())
        {
            return false;
        }

        const auto* channel = find_float1_channel(*this);
        if (!channel) {
            return false;
        }

        const uint32_t expected_bytes =
            field->element_count
            * wz::engine::assets::mesh_derived_field_value_stride(
                channel->value_type);
        return channel->byte_count == expected_bytes
            && channel->byte_offset <= field->values.size()
            && channel->byte_count <= field->values.size()
                - channel->byte_offset;
    }

    const std::byte* MeshFieldVisualizationUploadDesc::values_begin()
        const noexcept
    {
        if (!valid()) {
            return nullptr;
        }
        const auto* channel = find_float1_channel(*this);
        return channel ? field->values.data() + channel->byte_offset : nullptr;
    }

    uint64_t MeshFieldVisualizationUploadDesc::value_byte_count()
        const noexcept
    {
        if (!valid()) {
            return 0u;
        }
        const auto* channel = find_float1_channel(*this);
        return channel ? channel->byte_count : 0u;
    }

    uint32_t MeshFieldVisualizationUploadDesc::element_count() const noexcept
    {
        return valid() && field ? field->element_count : 0u;
    }

    uint32_t MeshFieldVisualizationUploadDesc::stride_bytes() const noexcept
    {
        return valid()
            ? wz::engine::assets::mesh_derived_field_value_stride(
                wz::engine::assets::MeshDerivedFieldValueType::Float1)
            : 0u;
    }

    GPUHandle upload_mesh_field_visualization(
        Device& device,
        const MeshFieldVisualizationUploadDesc& desc)
    {
        if (!device.valid() || !desc.valid()) {
            return {};
        }

        return dx12::internal::upload_mesh_field_visualization_dx12(
            device,
            desc);
    }

    GPUHandle create_mesh_field_visualization_from_gpu_source(
        Device& device,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        if (!device.valid()
            || !source_buffer.valid()
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return {};
        }

        return dx12::internal
            ::create_mesh_field_visualization_from_gpu_source_dx12(
                device,
                source_buffer,
                byte_offset,
                element_count,
                stride_bytes);
    }

    bool update_mesh_field_visualization_from_gpu_source(
        Device& device,
        GPUHandle destination,
        GPUHandle source_buffer,
        uint64_t byte_offset,
        uint32_t element_count,
        uint32_t stride_bytes)
    {
        if (!device.valid()
            || !destination.valid()
            || !source_buffer.valid()
            || element_count == 0u
            || stride_bytes == 0u)
        {
            return false;
        }

        return dx12::internal
            ::update_mesh_field_visualization_from_gpu_source_dx12(
                device,
                destination,
                source_buffer,
                byte_offset,
                element_count,
                stride_bytes);
    }

    bool release_mesh_field_visualization(
        Device& device,
        GPUHandle handle)
    {
        if (!device.valid() || !handle.valid()) {
            return false;
        }

        return dx12::internal::release_mesh_field_visualization_dx12(
            device,
            handle);
    }
}
