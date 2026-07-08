#pragma once

// engine/starfield/star_catalog_table.h
//
// Runtime table of built StarCatalog records, keyed by ResourceHandle (Seam C-2,
// issue #266). Mirror of SkyGaussianTable: a baked .star_catalog.json becomes an
// asset whose compiled payload is a handle into this table, and -- when a shared
// wozzits-rhi registry is present -- a resident StructuredBuffer of point-source
// records (variant "star_catalog"; see star_catalog_compilers.cpp).
//
// This is the asset-system-facing companion to the pure-CPU kernel header
// star_catalog.h, which carries no asset-system types. The stars are appearance-
// only (excluded from the lighting bake by default), so nothing reads this table
// on the CPU today -- it exists to mint a valid compiled handle and to keep the
// built set available for tests / introspection.

#include <engine/starfield/star_catalog.h>
#include <engine/assets/type_extensions.h>

#include <asset/types.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace wz::engine::starfield
{
    class StarCatalogTable
    {
    public:
        StarCatalogTable()
        {
            // Slot 0 is a sentinel so a default (id=0, epoch=0) handle never
            // resolves -- matches SkyGaussianTable / GaussianSplatCloudTable.
            catalogs_.emplace_back();
            epochs_.push_back(0);
        }

        wz::asset::ResourceHandle add(StarCatalog catalog)
        {
            if (catalog.stars.empty())
                return {};

            const uint32_t id = static_cast<uint32_t>(catalogs_.size());

            catalogs_.push_back(std::move(catalog));
            epochs_.push_back(1);

            return wz::asset::ResourceHandle{
                .id = id,
                .epoch = epochs_[id],
                .type = wz::engine::assets::kAssetTypeStarCatalog,
            };
        }

        const StarCatalog* get(wz::asset::ResourceHandle handle) const
        {
            if (!handle.valid())
                return nullptr;

            if (handle.type != wz::engine::assets::kAssetTypeStarCatalog)
                return nullptr;

            if (handle.id >= catalogs_.size())
                return nullptr;

            if (epochs_[handle.id] != handle.epoch)
                return nullptr;

            return &catalogs_[handle.id];
        }

        void destroy()
        {
            catalogs_.clear();
            epochs_.clear();

            catalogs_.emplace_back();
            epochs_.push_back(0);
        }

    private:
        std::vector<StarCatalog> catalogs_;
        std::vector<uint32_t>    epochs_;
    };
}
