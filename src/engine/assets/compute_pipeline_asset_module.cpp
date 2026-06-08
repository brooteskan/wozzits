#include <engine/assets/compute_pipeline_asset_module.h>
#include <engine/assets/key_factories/compute_pipeline.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <asset/system.h>

#include <any>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    ComputePipelineAssetModule::ComputePipelineAssetModule(
        wz::asset::AssetSystem& system,
        ComputePipelineTable& table)
        : system_(&system)
        , table_(&table)
    {
    }

    ComputePipelineAsset ComputePipelineAssetModule::create_compute_pipeline(
        ComputePipelineDesc desc)
    {
        if (!system_)
            return {};

        if (desc.name.empty()
            || desc.compute_shader == wz::asset::AssetKey{}
            || desc.thread_group_size_x == 0u
            || desc.thread_group_size_y == 0u
            || desc.thread_group_size_z == 0u)
        {
            return {};
        }

        const wz::asset::AssetKey key = make_compute_pipeline_key(desc);

        wz::asset::AssetNode node{};
        node.key = key;
        node.type = kAssetTypeComputePipeline;
        node.schema = kComputePipelineSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = std::move(desc);

        const auto& d = std::any_cast<const ComputePipelineDesc&>(node.meta);
        if (!system_->register_asset(std::move(node), { d.compute_shader }))
            return ComputePipelineAsset{ .key = key };

        return ComputePipelineAsset{ .key = key };
    }

    wz::asset::ResourceHandle ComputePipelineAssetModule::get_compute_pipeline(
        ComputePipelineAsset asset) const
    {
        if (!system_ || !asset.valid())
            return {};

        const auto* compiled = system_->find_compiled(asset.key);
        if (!compiled)
            return {};

        return compiled->handle;
    }

    const ComputePipelineData*
    ComputePipelineAssetModule::get_compute_pipeline_data(
        wz::asset::ResourceHandle handle) const
    {
        return table_ ? table_->get(handle) : nullptr;
    }

    ComputePipelineTable& ComputePipelineAssetModule::table()
    {
        return *table_;
    }

    const ComputePipelineTable& ComputePipelineAssetModule::table() const
    {
        return *table_;
    }
}
