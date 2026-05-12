// src/engine/assets/mesh/mesh.cpp

#include <engine/assets/mesh/mesh.h>

#include <utility>

namespace wz::engine::assets
{
    bool MeshData::valid() const noexcept
    {
        return !vertices.empty()
            && !indices.empty()
            && (indices.size() % 3u) == 0u;
    }

    uint32_t MeshData::vertex_count() const noexcept
    {
        return static_cast<uint32_t>(vertices.size());
    }

    uint32_t MeshData::index_count() const noexcept
    {
        return static_cast<uint32_t>(indices.size());
    }

    MeshTable::MeshTable()
    {
        // Slot 0 is reserved as the invalid/null ResourceHandle sentinel.
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle MeshTable::add(MeshData mesh)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.mesh = std::move(mesh);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        wz::asset::ResourceHandle handle{};
        handle.id = id;
        handle.epoch = 1;
        return handle;
    }

    const MeshData* MeshTable::get(wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied)
            return nullptr;

        if (slot.epoch != handle.epoch)
            return nullptr;

        return &slot.mesh;
    }

    MeshData* MeshTable::get_mutable_for_tests(wz::asset::ResourceHandle handle)
    {
        if (!handle.valid())
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        Slot& slot = slots_[handle.id];

        if (!slot.occupied)
            return nullptr;

        if (slot.epoch != handle.epoch)
            return nullptr;

        return &slot.mesh;
    }

    void MeshTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}