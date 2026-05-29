#include <engine/assets/vector_field/vector_field.h>

namespace wz::engine::assets
{
    VectorFieldTable::VectorFieldTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle VectorFieldTable::add(VectorFieldData field)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.field = std::move(field);

        slots_.push_back(std::move(slot));

        return wz::asset::ResourceHandle{
            .id = static_cast<uint32_t>(slots_.size() - 1),
            .epoch = slots_.back().epoch,
        };
    }

    const VectorFieldData* VectorFieldTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.field;
    }

    VectorFieldData* VectorFieldTable::get_mutable_for_tests(
        wz::asset::ResourceHandle handle)
    {
        if (!handle.valid())
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.field;
    }

    void VectorFieldTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }

} // namespace wz::engine::assets
