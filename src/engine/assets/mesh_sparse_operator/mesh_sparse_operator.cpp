// src/engine/assets/mesh_sparse_operator/mesh_sparse_operator.cpp

#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/type_extensions.h>

#include <utility>

namespace wz::engine::assets
{
    GpuResidentSparseOperatorEntry*
    GpuResidentSparseOperatorTable::find_or_add(
        wz::asset::AssetKey operator_key)
    {
        if (operator_key == wz::asset::AssetKey{}) {
            return nullptr;
        }
        for (GpuResidentSparseOperatorEntry& entry : entries_) {
            if (entry.operator_key == operator_key) {
                return &entry;
            }
        }
        entries_.push_back(GpuResidentSparseOperatorEntry{
            .operator_key = operator_key,
        });
        return &entries_.back();
    }

    const GpuResidentSparseOperatorEntry*
    GpuResidentSparseOperatorTable::find(
        wz::asset::AssetKey operator_key) const
    {
        if (operator_key == wz::asset::AssetKey{}) {
            return nullptr;
        }
        for (const GpuResidentSparseOperatorEntry& entry : entries_) {
            if (entry.operator_key == operator_key) {
                return &entry;
            }
        }
        return nullptr;
    }

    void GpuResidentSparseOperatorTable::destroy(
        MeshFieldComputeBackend& compute)
    {
        for (GpuResidentSparseOperatorEntry& entry : entries_) {
            for (wz::asset::ResourceHandle* handle : {
                &entry.row_offsets,
                &entry.col_indices,
                &entry.weights,
                &entry.vertex_mass })
            {
                if (handle->valid()) {
                    compute.release_buffer(*handle);
                    *handle = {};
                }
            }
        }
        entries_.clear();
    }

    size_t GpuResidentSparseOperatorTable::size() const noexcept
    {
        return entries_.size();
    }

    bool MeshSparseOperatorData::valid() const noexcept
    {
        if (source_mesh_key == wz::asset::AssetKey{}
            || source_topology_hash == wz::asset::Hash{}
            || row_count == 0u
            || row_offsets.size()
                != static_cast<size_t>(row_count) + 1u
            || row_offsets.front() != 0u
            || row_offsets.back() != nonzero_count
            || col_indices.size() != nonzero_count
            || weights.size() != nonzero_count
            || (!vertex_mass.empty()
                && vertex_mass.size() != row_count))
        {
            return false;
        }

        for (uint32_t row = 0; row < row_count; ++row) {
            if (row_offsets[row] > row_offsets[row + 1u]) {
                return false;
            }
            // Square over the row domain, and strictly ascending
            // col_indices per row: deterministic CSR bytes for hashing,
            // disk cache, and tests; also rules out duplicate entries.
            for (uint32_t e = row_offsets[row];
                e < row_offsets[row + 1u];
                ++e)
            {
                if (col_indices[e] >= row_count) {
                    return false;
                }
                if (e + 1u < row_offsets[row + 1u]
                    && col_indices[e] >= col_indices[e + 1u])
                {
                    return false;
                }
            }
        }
        return true;
    }

    MeshSparseOperatorTable::MeshSparseOperatorTable()
    {
        slots_.emplace_back();
    }

    wz::asset::ResourceHandle MeshSparseOperatorTable::add(
        MeshSparseOperatorData data)
    {
        const uint32_t id = static_cast<uint32_t>(slots_.size());
        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.data = std::move(data);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeMeshSparseOperator,
        };
    }

    const MeshSparseOperatorData* MeshSparseOperatorTable::get(
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

    void MeshSparseOperatorTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back();
    }
}
