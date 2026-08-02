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

    // ─── Descriptor range checks ──────────────────────────────────────────────────
    //
    // Both descriptors are stored in the GLB mesh disk cache as raw uint8 and
    // were static_cast straight back, so a flipped byte produced an
    // out-of-range enum that every other check in the loader missed -- magic,
    // format version, compiler version and the stored key all cover different
    // byte regions. index_format then selects the index stride, which is read
    // by consumers. Same shape as the scalar-field descriptors (B1-C4).
    //
    // WHEN YOU APPEND AN ENUMERATOR, UPDATE THE MATCHING PREDICATE. They live
    // beside the enums so the pairing is visible at the point of change.
    // Under-accepting is the safe direction: a cache entry carrying an
    // enumerator this build does not know is rejected, and the asset
    // recompiles.

    [[nodiscard]] constexpr bool valid_mesh_index_format(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(MeshIndexFormat::UInt32);
    }

    [[nodiscard]] constexpr bool valid_mesh_primitive_topology(uint8_t v) noexcept
    {
        return v <= static_cast<uint8_t>(MeshPrimitiveTopology::TriangleList);
    }

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

        bool has_normals = false;
        bool has_uv0 = false;

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
