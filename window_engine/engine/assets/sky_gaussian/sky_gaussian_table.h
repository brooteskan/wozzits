#pragma once

// engine/assets/sky_gaussian/sky_gaussian_table.h
//
// Runtime table of decoded SkyGaussianSet records, keyed by ResourceHandle.
// Byte-for-byte mirror of GaussianSplatCloudTable (gaussian_splat.h) for the
// sky-gaussian engine AssetType (Seam B1c, issue #260 / umbrella #259): the
// baked sky-gaussian lobes become an asset whose compiled payload is a handle
// into this table (and, when a shared rhi registry is present, a resident
// StructuredBuffer -- see sky_gaussian_compilers.cpp).
//
// This is the asset-system-facing companion to the pure-CPU Seam-A header
// sky_gaussian.h, which intentionally carries no asset-system types.

#include <engine/assets/sky_gaussian/sky_gaussian.h>
#include <engine/assets/type_extensions.h>

#include <asset/types.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    class SkyGaussianTable
    {
    public:
        SkyGaussianTable()
        {
            // Slot 0 is a sentinel so a default (id=0, epoch=0) handle never
            // resolves — matches GaussianSplatCloudTable.
            sets_.emplace_back();
            epochs_.push_back(0);
        }

        wz::asset::ResourceHandle add(sky::SkyGaussianSet set)
        {
            if (set.lobes.empty())
                return {};

            const uint32_t id = static_cast<uint32_t>(sets_.size());

            sets_.push_back(std::move(set));
            epochs_.push_back(1);

            return wz::asset::ResourceHandle{
                .id = id,
                .epoch = epochs_[id],
                .type = kAssetTypeSkyGaussian,
            };
        }

        const sky::SkyGaussianSet* get(wz::asset::ResourceHandle handle) const
        {
            if (!handle.valid())
                return nullptr;

            if (handle.type != kAssetTypeSkyGaussian)
                return nullptr;

            if (handle.id >= sets_.size())
                return nullptr;

            if (epochs_[handle.id] != handle.epoch)
                return nullptr;

            return &sets_[handle.id];
        }

        void destroy()
        {
            sets_.clear();
            epochs_.clear();

            sets_.emplace_back();
            epochs_.push_back(0);
        }

    private:
        std::vector<sky::SkyGaussianSet> sets_;
        std::vector<uint32_t>            epochs_;
    };
}
