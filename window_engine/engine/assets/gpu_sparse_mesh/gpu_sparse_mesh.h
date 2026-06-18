#pragma once

// engine/assets/gpu_sparse_mesh/gpu_sparse_mesh.h
//
// Logical GPU-resident mesh view produced by the asset graph. V0 is a source
// mesh upload: positions/indices stay renderable as a pulled mesh, while the
// sparse operator key identifies the CSR topology compute consumers should use.

#include <asset/types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    class MeshFieldComputeBackend;

    struct GpuSparseMeshData
    {
        wz::asset::AssetKey source_mesh_key{};
        wz::asset::Hash source_topology_hash{};
        wz::asset::AssetKey sparse_operator_key{};
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        uint32_t source_triangle_count = 0;

        bool valid() const noexcept;
    };

    class GpuSparseMeshTable
    {
    public:
        GpuSparseMeshTable();

        wz::asset::ResourceHandle add(GpuSparseMeshData data);
        const GpuSparseMeshData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            GpuSparseMeshData data;
        };

        std::vector<Slot> slots_;
    };

    struct GpuResidentSparseMeshEntry
    {
        wz::asset::AssetKey sparse_mesh_key{};
        wz::asset::AssetKey source_mesh_key{};
        wz::asset::AssetKey sparse_operator_key{};
        wz::asset::ResourceHandle positions{};       // float3
        wz::asset::ResourceHandle indices{};         // uint
        wz::asset::ResourceHandle source_vertices{}; // uint, one per vertex
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        uint32_t source_triangle_count = 0;

        bool valid() const noexcept;
    };

    class GpuResidentSparseMeshTable
    {
    public:
        GpuResidentSparseMeshEntry* find_or_add(
            wz::asset::AssetKey sparse_mesh_key);
        const GpuResidentSparseMeshEntry* find(
            wz::asset::AssetKey sparse_mesh_key) const;
        void destroy(MeshFieldComputeBackend& compute);
        size_t size() const noexcept;

    private:
        std::vector<GpuResidentSparseMeshEntry> entries_;
    };
}
