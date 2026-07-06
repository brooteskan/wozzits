// src/engine/assets/placed_field/placed_field.cpp

#include <engine/assets/placed_field/placed_field.h>

#include <utility>

namespace wz::engine::assets
{
    PlacedFieldTable::PlacedFieldTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle PlacedFieldTable::add(PlacedFieldData placed_field)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.placed_field = std::move(placed_field);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        wz::asset::ResourceHandle handle{};
        handle.id = id;
        handle.epoch = 1;
        return handle;
    }

    const PlacedFieldData* PlacedFieldTable::get(
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

        return &slot.placed_field;
    }

    void PlacedFieldTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
