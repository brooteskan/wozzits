#pragma once

// engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h
//
// CPU-side mesh simplification hierarchy. Each level stores a preview mesh so
// the asset graph and editor can inspect hierarchy generation before it drives
// renderer-side LOD selection.

#include <asset/types.h>
#include <engine/assets/mesh/mesh.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    class MeshFieldComputeBackend;

    enum class MeshClusterHierarchyBuildMethod : uint8_t
    {
        Identity = 0,
        GraphCoarsen = 1,
        LaplacianGraphCells = 2,
    };

    struct MeshClusterHierarchyLevel
    {
        uint32_t level_index = 0;
        uint32_t cluster_count = 0;
        uint32_t vertex_count = 0;
        uint32_t triangle_count = 0;
        float conservative_error = 0.0f;
        MeshData preview_mesh{};

        bool valid() const noexcept;
    };

    struct MeshClusterHierarchyData
    {
        wz::asset::AssetKey source_mesh_key{};
        wz::asset::Hash source_topology_hash{};
        MeshClusterHierarchyBuildMethod method =
            MeshClusterHierarchyBuildMethod::Identity;
        std::vector<MeshClusterHierarchyLevel> levels;

        bool valid() const noexcept;
        uint32_t level_count() const noexcept;
    };

    class MeshClusterHierarchyTable
    {
    public:
        MeshClusterHierarchyTable();

        wz::asset::ResourceHandle add(MeshClusterHierarchyData data);
        const MeshClusterHierarchyData* get(
            wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            MeshClusterHierarchyData data;
        };

        std::vector<Slot> slots_;
    };

    struct GpuResidentMeshClusterHierarchyLevel
    {
        uint32_t level_index = 0;
        wz::asset::ResourceHandle positions{};      // float3
        wz::asset::ResourceHandle indices{};        // uint
        wz::asset::ResourceHandle source_vertices{}; // uint, one per level vertex
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        uint32_t triangle_count = 0;

        bool valid() const noexcept
        {
            return positions.valid()
                && indices.valid()
                && source_vertices.valid()
                && vertex_count > 0u
                && index_count > 0u
                && triangle_count == index_count / 3u;
        }
    };

    struct GpuResidentMeshClusterHierarchyEntry
    {
        wz::asset::AssetKey hierarchy_key{};
        wz::asset::AssetKey source_mesh_key{};
        MeshClusterHierarchyBuildMethod method =
            MeshClusterHierarchyBuildMethod::Identity;
        std::vector<GpuResidentMeshClusterHierarchyLevel> levels;
    };

    class GpuResidentMeshClusterHierarchyTable
    {
    public:
        GpuResidentMeshClusterHierarchyEntry* find_or_add(
            wz::asset::AssetKey hierarchy_key);
        const GpuResidentMeshClusterHierarchyEntry* find(
            wz::asset::AssetKey hierarchy_key) const;
        GpuResidentMeshClusterHierarchyLevel* find_or_add_level(
            wz::asset::AssetKey hierarchy_key,
            uint32_t level_index);
        void destroy(MeshFieldComputeBackend& compute);
        size_t size() const noexcept;

    private:
        std::vector<GpuResidentMeshClusterHierarchyEntry> entries_;
    };
}
