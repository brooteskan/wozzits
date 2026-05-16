// src/engine/assets/renderable/renderable.cpp

#include <engine/assets/renderable/renderable.h>
#include <engine/assets/type_extensions.h>

#include <utility>

namespace wz::engine::assets
{
    bool RenderableAssetData::valid() const noexcept
    {
        if (source_asset == wz::asset::AssetKey{})
            return false;

        return true;
    }

    RenderableAssetTable::RenderableAssetTable()
    {
        renderables_.emplace_back();
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle RenderableAssetTable::add(
        RenderableAssetData data)
    {
        if (!data.valid())
            return {};

        const uint32_t id = static_cast<uint32_t>(renderables_.size());

        renderables_.push_back(std::move(data));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = epochs_[id],
            .type = kAssetTypeRenderable,
        };
    }

    const RenderableAssetData* RenderableAssetTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeRenderable)
            return nullptr;

        if (handle.id >= renderables_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &renderables_[handle.id];
    }

    void RenderableAssetTable::destroy()
    {
        renderables_.clear();
        epochs_.clear();

        renderables_.emplace_back();
        epochs_.push_back(0);
    }
}