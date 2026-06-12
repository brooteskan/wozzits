// src/engine/assets/mesh_derived_field/mesh_derived_field.cpp

#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace wz::engine::assets
{
    namespace
    {
        constexpr uint64_t kFnvOffset = 14695981039346656037ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;

        void fnv_mix_u32(uint64_t& hash, uint32_t value) noexcept
        {
            for (uint32_t i = 0; i < 4; ++i) {
                hash ^= static_cast<uint8_t>((value >> (i * 8u)) & 0xffu);
                hash *= kFnvPrime;
            }
        }

        uint64_t edge_key(uint32_t a, uint32_t b) noexcept
        {
            const uint32_t lo = (std::min)(a, b);
            const uint32_t hi = (std::max)(a, b);
            return (static_cast<uint64_t>(lo) << 32u)
                | static_cast<uint64_t>(hi);
        }

        bool channel_less(
            const MeshDerivedFieldChannel& a,
            const MeshDerivedFieldChannel& b) noexcept
        {
            return a.byte_offset < b.byte_offset;
        }

        bool has_duplicate_channel_ids(
            const std::vector<MeshDerivedFieldChannel>& channels)
        {
            std::vector<uint32_t> ids;
            ids.reserve(channels.size());
            for (const MeshDerivedFieldChannel& channel : channels) {
                ids.push_back(channel.channel_id);
            }
            std::sort(ids.begin(), ids.end());
            return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
        }
    }

    uint32_t mesh_derived_field_value_stride(
        MeshDerivedFieldValueType value_type) noexcept
    {
        switch (value_type) {
        case MeshDerivedFieldValueType::Float1:
        case MeshDerivedFieldValueType::UInt1:
            return 4u;
        case MeshDerivedFieldValueType::Float2:
            return 8u;
        case MeshDerivedFieldValueType::Float3:
            return 12u;
        case MeshDerivedFieldValueType::Float4:
            return 16u;
        }
        return 0u;
    }

    wz::asset::Hash compute_mesh_topology_hash(const MeshData& mesh) noexcept
    {
        uint64_t hash = kFnvOffset;
        fnv_mix_u32(hash, mesh.index_count());
        fnv_mix_u32(hash, mesh.vertex_count());
        for (const uint32_t index : mesh.indices) {
            fnv_mix_u32(hash, index);
        }
        return wz::asset::Hash{ .lo = hash, .hi = 0u };
    }

    uint32_t mesh_domain_element_count(
        const MeshData& mesh,
        MeshDerivedFieldDomain domain)
    {
        switch (domain) {
        case MeshDerivedFieldDomain::Vertex:
            return mesh.vertex_count();
        case MeshDerivedFieldDomain::Face:
            return mesh.index_count() / 3u;
        case MeshDerivedFieldDomain::Corner:
            return mesh.index_count();
        case MeshDerivedFieldDomain::Edge:
            break;
        }

        std::vector<uint64_t> edges;
        edges.reserve(mesh.indices.size());
        for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
            const uint32_t a = mesh.indices[i + 0u];
            const uint32_t b = mesh.indices[i + 1u];
            const uint32_t c = mesh.indices[i + 2u];
            edges.push_back(edge_key(a, b));
            edges.push_back(edge_key(b, c));
            edges.push_back(edge_key(c, a));
        }
        std::sort(edges.begin(), edges.end());
        const auto unique_end = std::unique(edges.begin(), edges.end());
        return static_cast<uint32_t>(
            std::distance(edges.begin(), unique_end));
    }

    bool MeshDerivedFieldData::valid() const noexcept
    {
        if (source_mesh_key == wz::asset::AssetKey{}
            || source_topology_hash == wz::asset::Hash{}
            || element_count == 0u
            || channels.empty())
        {
            return false;
        }

        uint64_t expected_total = 0;
        std::vector<MeshDerivedFieldChannel> sorted = channels;
        std::sort(sorted.begin(), sorted.end(), channel_less);
        if (has_duplicate_channel_ids(sorted)) {
            return false;
        }

        for (const MeshDerivedFieldChannel& channel : sorted) {
            const uint32_t stride =
                mesh_derived_field_value_stride(channel.value_type);
            if (channel.channel_id == 0u || stride == 0u) {
                return false;
            }

            const uint64_t expected_bytes =
                static_cast<uint64_t>(element_count) * stride;
            if (expected_bytes > std::numeric_limits<uint32_t>::max()
                || channel.byte_count != expected_bytes
                || channel.byte_offset != expected_total)
            {
                return false;
            }
            expected_total += channel.byte_count;
        }

        return expected_total == values.size();
    }

    MeshDerivedFieldTable::MeshDerivedFieldTable()
    {
        slots_.emplace_back();
    }

    wz::asset::ResourceHandle MeshDerivedFieldTable::add(
        MeshDerivedFieldData field)
    {
        const uint32_t id = static_cast<uint32_t>(slots_.size());
        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.field = std::move(field);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeMeshDerivedField,
        };
    }

    const MeshDerivedFieldData* MeshDerivedFieldTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size())) {
            return nullptr;
        }
        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }
        return &slot.field;
    }

    MeshDerivedFieldData* MeshDerivedFieldTable::get_mutable_for_tests(
        wz::asset::ResourceHandle handle)
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size())) {
            return nullptr;
        }
        Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }
        return &slot.field;
    }

    void MeshDerivedFieldTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back();
    }

    bool GpuResidentFieldEntry::valid() const noexcept
    {
        return field_key != wz::asset::AssetKey{}
            && channel_id != 0u
            && gpu_resource.valid();
    }

    GpuResidentMeshDataEntry* GpuResidentMeshDataTable::find_or_add(
        wz::asset::AssetKey mesh_key)
    {
        if (mesh_key == wz::asset::AssetKey{}) {
            return nullptr;
        }
        for (GpuResidentMeshDataEntry& entry : entries_) {
            if (entry.mesh_key == mesh_key) {
                return &entry;
            }
        }
        entries_.push_back(GpuResidentMeshDataEntry{
            .mesh_key = mesh_key,
        });
        return &entries_.back();
    }

    const GpuResidentMeshDataEntry* GpuResidentMeshDataTable::find(
        wz::asset::AssetKey mesh_key) const
    {
        if (mesh_key == wz::asset::AssetKey{}) {
            return nullptr;
        }
        for (const GpuResidentMeshDataEntry& entry : entries_) {
            if (entry.mesh_key == mesh_key) {
                return &entry;
            }
        }
        return nullptr;
    }

    void GpuResidentMeshDataTable::destroy(MeshFieldComputeBackend& compute)
    {
        for (GpuResidentMeshDataEntry& entry : entries_) {
            if (entry.positions.valid()) {
                compute.release_buffer(entry.positions);
                entry.positions = {};
            }
            if (entry.indices.valid()) {
                compute.release_buffer(entry.indices);
                entry.indices = {};
            }
        }
        entries_.clear();
    }

    size_t GpuResidentMeshDataTable::size() const noexcept
    {
        return entries_.size();
    }

    bool GpuResidentFieldTable::add(GpuResidentFieldEntry entry)
    {
        if (!entry.valid()) {
            return false;
        }

        for (GpuResidentFieldEntry& existing : entries_) {
            if (existing.field_key == entry.field_key
                && existing.channel_id == entry.channel_id
                && existing.layout == entry.layout)
            {
                return false;
            }
        }

        entries_.push_back(entry);
        return true;
    }

    bool GpuResidentFieldTable::replace(
        MeshFieldComputeBackend& compute,
        GpuResidentFieldEntry entry)
    {
        if (!entry.valid()) {
            return false;
        }

        for (GpuResidentFieldEntry& existing : entries_) {
            if (existing.field_key == entry.field_key
                && existing.channel_id == entry.channel_id
                && existing.layout == entry.layout)
            {
                if (existing.gpu_resource.valid()) {
                    compute.release_field_visualization(
                        existing.gpu_resource);
                }
                existing.gpu_resource = entry.gpu_resource;
                return true;
            }
        }

        entries_.push_back(entry);
        return true;
    }

    wz::asset::ResourceHandle GpuResidentFieldTable::find(
        wz::asset::AssetKey field_key,
        uint32_t channel_id,
        GpuResidentFieldLayout layout) const
    {
        if (field_key == wz::asset::AssetKey{} || channel_id == 0u) {
            return {};
        }

        for (const GpuResidentFieldEntry& entry : entries_) {
            if (entry.field_key == field_key
                && entry.channel_id == channel_id
                && entry.layout == layout
                && entry.gpu_resource.valid())
            {
                return entry.gpu_resource;
            }
        }
        return {};
    }

    void GpuResidentFieldTable::clear()
    {
        entries_.clear();
    }

    void GpuResidentFieldTable::destroy(MeshFieldComputeBackend& compute)
    {
        for (GpuResidentFieldEntry& entry : entries_) {
            if (entry.gpu_resource.valid()) {
                compute.release_field_visualization(entry.gpu_resource);
                entry.gpu_resource = {};
            }
        }
        entries_.clear();
    }

    size_t GpuResidentFieldTable::size() const noexcept
    {
        return entries_.size();
    }
}
