#pragma once

// engine/assets/inochi/puppet_table.h
//
// Runtime table of compiled Inochi2D puppets, keyed by ResourceHandle. Mirrors
// GaussianSplatCloudTable / SkyGaussianTable: slot 0 is a sentinel so a default
// (id=0, epoch=0) handle never resolves. A PuppetData holds the puppet's source
// (CPU node/mesh/param data, which the deform seam will read) plus its GPU-
// resident draw metadata (inochi::ResidentPuppet, empty in a device-only
// library). The GPU buffers/textures live in the shared wozzits-rhi registry.

#include <engine/assets/inochi/inochi_puppet.h>
#include <engine/assets/inochi/puppet_gpu.h>
#include <engine/assets/type_extensions.h>

#include <asset/types.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    struct PuppetData
    {
        inochi::Puppet source;            // CPU data (deform / later seams read this)
        inochi::ResidentPuppet resident;  // GPU residency; empty in a device-only library
    };

    class PuppetTable
    {
    public:
        PuppetTable()
        {
            // Slot 0 is a sentinel so a default (id=0, epoch=0) handle never
            // resolves -- matches GaussianSplatCloudTable / SkyGaussianTable.
            puppets_.emplace_back();
            epochs_.push_back(0);
        }

        wz::asset::ResourceHandle add(PuppetData puppet)
        {
            if (puppet.source.nodes.empty())
                return {};

            const uint32_t id = static_cast<uint32_t>(puppets_.size());

            puppets_.push_back(std::move(puppet));
            epochs_.push_back(1);

            return wz::asset::ResourceHandle{
                .id = id,
                .epoch = epochs_[id],
                .type = kAssetTypePuppet,
            };
        }

        const PuppetData* get(wz::asset::ResourceHandle handle) const
        {
            if (!handle.valid())
                return nullptr;

            if (handle.type != kAssetTypePuppet)
                return nullptr;

            if (handle.id >= puppets_.size())
                return nullptr;

            if (epochs_[handle.id] != handle.epoch)
                return nullptr;

            return &puppets_[handle.id];
        }

        void destroy()
        {
            puppets_.clear();
            epochs_.clear();

            puppets_.emplace_back();
            epochs_.push_back(0);
        }

    private:
        std::vector<PuppetData> puppets_;
        std::vector<uint32_t>   epochs_;
    };
}
