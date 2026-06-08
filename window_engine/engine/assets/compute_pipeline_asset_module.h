#pragma once

// engine/assets/compute_pipeline_asset_module.h

#include <asset/types.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>

namespace wz::asset
{
    class AssetSystem;
}

namespace wz::engine::assets
{
    struct ComputePipelineAsset
    {
        wz::asset::AssetKey key{};

        bool valid() const noexcept
        {
            return !(key == wz::asset::AssetKey{});
        }
    };

    class ComputePipelineAssetModule
    {
    public:
        ComputePipelineAssetModule(
            wz::asset::AssetSystem& system,
            ComputePipelineTable& table);

        ComputePipelineAsset create_compute_pipeline(
            ComputePipelineDesc desc);

        wz::asset::ResourceHandle get_compute_pipeline(
            ComputePipelineAsset asset) const;

        const ComputePipelineData* get_compute_pipeline_data(
            wz::asset::ResourceHandle handle) const;

        ComputePipelineTable& table();
        const ComputePipelineTable& table() const;

    private:
        wz::asset::AssetSystem* system_ = nullptr;
        ComputePipelineTable* table_ = nullptr;
    };
}
