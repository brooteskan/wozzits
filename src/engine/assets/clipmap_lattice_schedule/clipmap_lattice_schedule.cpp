// src/engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule.cpp

#include <engine/assets/clipmap_lattice_schedule/clipmap_lattice_schedule.h>

#include <utility>

namespace wz::engine::assets
{
    ClipmapLatticeScheduleTable::ClipmapLatticeScheduleTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle ClipmapLatticeScheduleTable::add(
        ClipmapLatticeScheduleData schedule)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.schedule = std::move(schedule);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        wz::asset::ResourceHandle handle{};
        handle.id = id;
        handle.epoch = 1;
        return handle;
    }

    const ClipmapLatticeScheduleData* ClipmapLatticeScheduleTable::get(
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

        return &slot.schedule;
    }

    void ClipmapLatticeScheduleTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
