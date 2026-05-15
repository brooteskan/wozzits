// src/engine/assets/gaussian_splat/gaussian_splat.cpp

#include <engine/assets/gaussian_splat/gaussian_splat.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::assets
{
    bool GaussianSplatCloudData::valid() const noexcept
    {
        return !splats.empty();
    }

    uint32_t GaussianSplatCloudData::splat_count() const noexcept
    {
        return static_cast<uint32_t>(splats.size());
    }

    GaussianSplatCloudTable::GaussianSplatCloudTable()
    {
        clouds_.emplace_back();
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle GaussianSplatCloudTable::add(
        GaussianSplatCloudData cloud)
    {
        if (!cloud.valid())
            return {};

        const uint32_t id = static_cast<uint32_t>(clouds_.size());

        clouds_.push_back(std::move(cloud));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = epochs_[id],
            .type = kAssetTypeGaussianSplatCloud,
        };
    }

    const GaussianSplatCloudData* GaussianSplatCloudTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeGaussianSplatCloud)
            return nullptr;

        if (handle.id >= clouds_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &clouds_[handle.id];
    }

    void GaussianSplatCloudTable::destroy()
    {
        clouds_.clear();
        epochs_.clear();

        clouds_.emplace_back();
        epochs_.push_back(0);
    }
}