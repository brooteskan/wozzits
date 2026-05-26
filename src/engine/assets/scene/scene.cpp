// src/engine/assets/scene/scene.cpp

#include <engine/assets/scene/scene.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    SceneAssetTable::SceneAssetTable()
    {
        slots_.emplace_back();
    }

    wz::asset::ResourceHandle SceneAssetTable::add(SceneAssetData data)
    {
        const uint32_t id = static_cast<uint32_t>(slots_.size());

        Slot& slot = slots_.emplace_back();
        slot.epoch = 1;
        slot.occupied = true;
        slot.data = std::move(data);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = slot.epoch,
            .type = kAssetTypeScene,
        };
    }

    const SceneAssetData* SceneAssetTable::get(wz::asset::ResourceHandle handle) const
    {
        if (handle.id >= static_cast<uint32_t>(slots_.size()))
            return nullptr;

        const Slot& slot = slots_[handle.id];

        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        if (handle.type != kAssetTypeScene)
            return nullptr;

        return &slot.data;
    }

    void SceneAssetTable::destroy()
    {
        slots_.clear();
        slots_.emplace_back();
    }

} // namespace wz::engine::assets
