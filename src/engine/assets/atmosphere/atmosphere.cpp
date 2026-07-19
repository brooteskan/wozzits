// src/engine/assets/atmosphere/atmosphere.cpp

#include <engine/assets/atmosphere/atmosphere.h>

#include <utility>

namespace wz::engine::assets
{
    AtmosphereTable::AtmosphereTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle AtmosphereTable::add(AtmosphereData atmosphere)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.atmosphere = std::move(atmosphere);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        wz::asset::ResourceHandle handle{};
        handle.id = id;
        handle.epoch = 1;
        return handle;
    }

    const AtmosphereData* AtmosphereTable::get(
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

        return &slot.atmosphere;
    }

    void AtmosphereTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
