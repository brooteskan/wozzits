// src/engine/assets/placement/placement.cpp

#include <engine/assets/placement/placement.h>

#include <utility>

namespace wz::engine::assets
{
    PlacementTable::PlacementTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle PlacementTable::add(PlacementData placement)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.placement = std::move(placement);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        wz::asset::ResourceHandle handle{};
        handle.id = id;
        handle.epoch = 1;
        return handle;
    }

    const PlacementData* PlacementTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        if (handle.id >= slots_.size()) {
            return nullptr;
        }

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }

        return &slot.placement;
    }

    void PlacementTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
