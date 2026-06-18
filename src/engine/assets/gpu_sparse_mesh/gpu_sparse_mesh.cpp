// src/engine/assets/gpu_sparse_mesh/gpu_sparse_mesh.cpp

#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/type_extensions.h>

#include <utility>

namespace wz::engine::assets
{
    bool GpuSparseMeshData::valid() const noexcept
    {
        return source_mesh_key != wz::asset::AssetKey{}
            && source_topology_hash != wz::asset::Hash{}
            && sparse_operator_key != wz::asset::AssetKey{}
            && vertex_count > 0u
            && index_count > 0u
            && source_triangle_count == index_count / 3u;
    }

    GpuSparseMeshTable::GpuSparseMeshTable()
    {
        slots_.emplace_back();
    }

    wz::asset::ResourceHandle GpuSparseMeshTable::add(
        GpuSparseMeshData data)
    {
        const uint32_t id = static_cast<uint32_t>(slots_.size());
        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.data = std::move(data);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeGpuSparseMesh,
        };
    }

    const GpuSparseMeshData* GpuSparseMeshTable::get(
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

    void GpuSparseMeshTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back();
    }

    bool GpuResidentSparseMeshEntry::valid() const noexcept
    {
        return sparse_mesh_key != wz::asset::AssetKey{}
            && source_mesh_key != wz::asset::AssetKey{}
            && sparse_operator_key != wz::asset::AssetKey{}
            && positions.valid()
            && indices.valid()
            && source_vertices.valid()
            && vertex_count > 0u
            && index_count > 0u
            && source_triangle_count == index_count / 3u;
    }

    GpuResidentSparseMeshEntry* GpuResidentSparseMeshTable::find_or_add(
        wz::asset::AssetKey sparse_mesh_key)
    {
        if (sparse_mesh_key == wz::asset::AssetKey{}) {
            return nullptr;
        }
        for (GpuResidentSparseMeshEntry& entry : entries_) {
            if (entry.sparse_mesh_key == sparse_mesh_key) {
                return &entry;
            }
        }
        entries_.push_back(GpuResidentSparseMeshEntry{
            .sparse_mesh_key = sparse_mesh_key,
        });
        return &entries_.back();
    }

    const GpuResidentSparseMeshEntry* GpuResidentSparseMeshTable::find(
        wz::asset::AssetKey sparse_mesh_key) const
    {
        if (sparse_mesh_key == wz::asset::AssetKey{}) {
            return nullptr;
        }
        for (const GpuResidentSparseMeshEntry& entry : entries_) {
            if (entry.sparse_mesh_key == sparse_mesh_key) {
                return &entry;
            }
        }
        return nullptr;
    }

    void GpuResidentSparseMeshTable::destroy(
        MeshFieldComputeBackend& compute)
    {
        for (GpuResidentSparseMeshEntry& entry : entries_) {
            for (wz::asset::ResourceHandle* handle : {
                &entry.positions,
                &entry.indices,
                &entry.source_vertices })
            {
                if (handle->valid()) {
                    compute.release_buffer(*handle);
                    *handle = {};
                }
            }
        }
        entries_.clear();
    }

    size_t GpuResidentSparseMeshTable::size() const noexcept
    {
        return entries_.size();
    }
}
