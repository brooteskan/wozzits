// src/engine/assets/gaussian_splat/gaussian_splat_color_lod.cpp

#include <engine/assets/gaussian_splat/gaussian_splat_color_lod.h>
#include <engine/assets/type_extensions.h>

namespace wz::engine::assets
{
    GaussianSplatColorLODTable::GaussianSplatColorLODTable()
    {
        // Slot 0 reserved as the "invalid" entry, mirroring
        // GaussianSplatCloudTable's convention.
        entries_.emplace_back();
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle GaussianSplatColorLODTable::add(
        GaussianSplatColorLODData data)
    {
        if (!data.valid())
            return {};

        const uint32_t id = static_cast<uint32_t>(entries_.size());

        entries_.push_back(std::move(data));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = epochs_[id],
            .type = kAssetTypeGaussianSplatColorLOD,
        };
    }

    const GaussianSplatColorLODData* GaussianSplatColorLODTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeGaussianSplatColorLOD)
            return nullptr;

        if (handle.id >= entries_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &entries_[handle.id];
    }

    void GaussianSplatColorLODTable::destroy()
    {
        entries_.clear();
        epochs_.clear();

        entries_.emplace_back();
        epochs_.push_back(0);
    }
}
