// src/engine/assets/environment/environment.cpp

#include <engine/assets/environment/environment.h>

#include <utility>

namespace wz::engine::assets
{
    EnvironmentTable::EnvironmentTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle EnvironmentTable::add(EnvironmentData environment)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.environment = std::move(environment);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        wz::asset::ResourceHandle handle{};
        handle.id = id;
        handle.epoch = 1;
        return handle;
    }

    const EnvironmentData* EnvironmentTable::get(
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

        return &slot.environment;
    }

    void EnvironmentTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
