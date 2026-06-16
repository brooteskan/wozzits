#pragma once

// wozzits/rhi/geometry_view.h
//
// Packet-shared geometry plus per-item stream selection. A renderable packet
// can own one GeometryView, while each pass item chooses the streams it needs.

#include <wozzits/rhi/gpu_resource.h>

#include <cstdint>
#include <vector>

namespace wz::rhi
{
    struct StreamBufferView
    {
        GpuResourceHandle buffer{};
        uint32_t offset = 0;
        uint32_t stride = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return buffer.valid() && stride != 0;
        }
    };

    struct GeometryView
    {
        std::vector<StreamBufferView> streams;

        GpuResourceHandle index_buffer{};
        uint32_t index_offset = 0;
        uint32_t index_count = 0;
        uint32_t first_index = 0;
        int32_t  vertex_offset = 0;
        uint32_t vertex_count = 0;

        [[nodiscard]] bool indexed() const noexcept
        {
            return index_buffer.valid() && index_count != 0;
        }

        [[nodiscard]] bool valid_stream(uint32_t stream_index) const noexcept
        {
            return stream_index < streams.size()
                && streams[stream_index].valid();
        }
    };

    struct StreamBufferIndices
    {
        std::vector<uint32_t> indices;

        bool add(uint32_t stream_index)
        {
            if (contains(stream_index)) {
                return false;
            }
            indices.push_back(stream_index);
            return true;
        }

        [[nodiscard]] bool contains(uint32_t stream_index) const noexcept
        {
            for (const uint32_t index : indices) {
                if (index == stream_index) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool valid_for(const GeometryView& geometry) const noexcept
        {
            for (const uint32_t index : indices) {
                if (!geometry.valid_stream(index)) {
                    return false;
                }
            }
            return true;
        }
    };
}
