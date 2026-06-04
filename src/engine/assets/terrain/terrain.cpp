// src/engine/assets/terrain/terrain.cpp

#include <engine/assets/terrain/terrain.h>
#include <engine/assets/type_extensions.h>

#include <utility>

namespace wz::engine::assets
{
    bool TerrainAssetData::valid() const noexcept
    {
        if (source_asset == wz::asset::AssetKey{}
            || bounds_min[0] > bounds_max[0]
            || bounds_min[1] > bounds_max[1]
            || bounds_min[2] > bounds_max[2])
        {
            return false;
        }

        if (representation == TerrainRepresentationKind::HeightField) {
            return height_samples.size()
                == static_cast<size_t>(resolution_x) * resolution_y;
        }

        return true;
    }

    TerrainAssetTable::TerrainAssetTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle TerrainAssetTable::add(TerrainAssetData terrain)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.terrain = std::move(terrain);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = 1,
            .type = kAssetTypeTerrain,
        };
    }

    const TerrainAssetData* TerrainAssetTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.id >= slots_.size())
            return nullptr;

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch)
            return nullptr;

        return &slot.terrain;
    }

    void TerrainAssetTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
