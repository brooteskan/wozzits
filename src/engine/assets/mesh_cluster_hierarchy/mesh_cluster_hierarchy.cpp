#include <engine/assets/mesh_cluster_hierarchy/mesh_cluster_hierarchy.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/type_extensions.h>

#include <cstddef>
#include <utility>

namespace wz::engine::assets
{
    bool MeshClusterHierarchyLevel::valid() const noexcept
    {
        return vertex_count == preview_mesh.vertex_count()
            && triangle_count == preview_mesh.index_count() / 3u
            && preview_mesh.valid();
    }

    bool MeshClusterHierarchyData::valid() const noexcept
    {
        if (source_mesh_key == wz::asset::AssetKey{}
            || source_topology_hash == wz::asset::Hash{}
            || levels.empty())
        {
            return false;
        }

        for (size_t i = 0; i < levels.size(); ++i) {
            if (levels[i].level_index != i || !levels[i].valid()) {
                return false;
            }
        }
        return true;
    }

    uint32_t MeshClusterHierarchyData::level_count() const noexcept
    {
        return static_cast<uint32_t>(levels.size());
    }

    MeshClusterHierarchyTable::MeshClusterHierarchyTable()
    {
        slots_.emplace_back();
    }

    wz::asset::ResourceHandle MeshClusterHierarchyTable::add(
        MeshClusterHierarchyData data)
    {
        const uint32_t id = static_cast<uint32_t>(slots_.size());
        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.data = std::move(data);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeMeshClusterHierarchy,
        };
    }

    const MeshClusterHierarchyData* MeshClusterHierarchyTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size())) {
            return nullptr;
        }
        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }
        return &slot.data;
    }

    void MeshClusterHierarchyTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back();
    }

    GpuResidentMeshClusterHierarchyEntry*
    GpuResidentMeshClusterHierarchyTable::find_or_add(
        wz::asset::AssetKey hierarchy_key)
    {
        if (hierarchy_key == wz::asset::AssetKey{}) {
            return nullptr;
        }
        for (GpuResidentMeshClusterHierarchyEntry& entry : entries_) {
            if (entry.hierarchy_key == hierarchy_key) {
                return &entry;
            }
        }
        entries_.push_back(GpuResidentMeshClusterHierarchyEntry{
            .hierarchy_key = hierarchy_key,
        });
        return &entries_.back();
    }

    const GpuResidentMeshClusterHierarchyEntry*
    GpuResidentMeshClusterHierarchyTable::find(
        wz::asset::AssetKey hierarchy_key) const
    {
        if (hierarchy_key == wz::asset::AssetKey{}) {
            return nullptr;
        }
        for (const GpuResidentMeshClusterHierarchyEntry& entry : entries_) {
            if (entry.hierarchy_key == hierarchy_key) {
                return &entry;
            }
        }
        return nullptr;
    }

    GpuResidentMeshClusterHierarchyLevel*
    GpuResidentMeshClusterHierarchyTable::find_or_add_level(
        wz::asset::AssetKey hierarchy_key,
        uint32_t level_index)
    {
        GpuResidentMeshClusterHierarchyEntry* entry =
            find_or_add(hierarchy_key);
        if (!entry) {
            return nullptr;
        }
        for (GpuResidentMeshClusterHierarchyLevel& level : entry->levels) {
            if (level.level_index == level_index) {
                return &level;
            }
        }
        entry->levels.push_back(GpuResidentMeshClusterHierarchyLevel{
            .level_index = level_index,
        });
        return &entry->levels.back();
    }

    void GpuResidentMeshClusterHierarchyTable::destroy(
        MeshFieldComputeBackend& compute)
    {
        for (GpuResidentMeshClusterHierarchyEntry& entry : entries_) {
            for (GpuResidentMeshClusterHierarchyLevel& level : entry.levels) {
                for (wz::asset::ResourceHandle* handle : {
                    &level.positions,
                    &level.indices,
                    &level.source_vertices })
                {
                    if (handle->valid()) {
                        compute.release_buffer(*handle);
                        *handle = {};
                    }
                }
            }
        }
        entries_.clear();
    }

    size_t GpuResidentMeshClusterHierarchyTable::size() const noexcept
    {
        return entries_.size();
    }
}
