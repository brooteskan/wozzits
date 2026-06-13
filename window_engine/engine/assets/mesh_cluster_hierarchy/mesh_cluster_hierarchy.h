#pragma once

// engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h
//
// CPU-side mesh simplification hierarchy. V0 is an identity scaffold: it
// stores one preview level mirroring the source mesh so the asset graph,
// editor, and future graph-coarsening algorithms have a concrete target.

#include <asset/types.h>
#include <engine/assets/mesh/mesh.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    enum class MeshClusterHierarchyBuildMethod : uint8_t
    {
        Identity = 0,
        GraphCoarsen = 1,
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
}
