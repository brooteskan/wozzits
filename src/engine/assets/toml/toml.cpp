// src/engine/assets/toml/toml.cpp

#include <engine/assets/toml/toml.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    TOMLTable::TOMLTable()
    {
        slots_.emplace_back(); // sentinel: id 0 is invalid
    }

    wz::asset::ResourceHandle TOMLTable::add(TOMLData data)
    {
        const uint32_t id = static_cast<uint32_t>(slots_.size());

        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.data = std::move(data);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeTOMLDocument,
        };
    }

    const TOMLData* TOMLTable::get(wz::asset::ResourceHandle handle) const
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size()))
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        if (handle.type != kAssetTypeTOMLDocument)
            return nullptr;

        return &slot.data;
    }

    void TOMLTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back(); // restore sentinel
    }

} // namespace wz::engine::assets