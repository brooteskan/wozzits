#include <engine/assets/terrain/terrain_visual_proxy.h>

#include <engine/assets/type_extensions.h>

#include <utility>

namespace wz::engine::assets
{
    TerrainVisualProxyTable::TerrainVisualProxyTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle TerrainVisualProxyTable::add(
        TerrainVisualProxyData proxy)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.proxy = std::move(proxy);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = 1,
            .type = kAssetTypeTerrainVisualProxy,
        };
    }

    const TerrainVisualProxyData* TerrainVisualProxyTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid() || handle.id >= slots_.size()) {
            return nullptr;
        }

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }

        return &slot.proxy;
    }

    void TerrainVisualProxyTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
