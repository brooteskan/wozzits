#pragma once

// engine/assets/mesh/mesh.h

#include <asset/types.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    enum class MeshIndexFormat : uint8_t
    {
        UInt16,
        UInt32,
    };

    enum class MeshPrimitiveTopology : uint8_t
    {
        TriangleList,
    };

    struct MeshVertex
    {
        float position[3] = {};
        float normal[3] = {};
        float uv[2] = {};
    };

    struct MeshData
    {
        MeshPrimitiveTopology topology = MeshPrimitiveTopology::TriangleList;
        MeshIndexFormat index_format = MeshIndexFormat::UInt32;

        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;

        bool valid() const noexcept;
        uint32_t vertex_count() const noexcept;
        uint32_t index_count() const noexcept;
    };

    class MeshTable
    {
    public:
        MeshTable();

        wz::asset::ResourceHandle add(MeshData mesh);
        const MeshData* get(wz::asset::ResourceHandle handle) const;
        MeshData* get_mutable_for_tests(wz::asset::ResourceHandle handle);

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            MeshData mesh;
        };

        std::vector<Slot> slots_;
    };
}