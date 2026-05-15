// src/engine/assets/data_table_asset_module.cpp

#include <engine/assets/data_table_asset_module.h>

#include <engine/assets/key_factories/data_table.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    DataTableAssetModule::DataTableAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        DataTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    DataTableAsset DataTableAssetModule::create_inline_table(
        const InlineDataTableDesc& desc)
    {
        DataTableAsset out{};

        if (desc.name.empty()) {
            logger_.error("data table has empty name");
            return out;
        }

        if (!desc.table.valid()) {
            logger_.error("data table is invalid: " + desc.name);
            return out;
        }

        const wz::asset::AssetKey key =
            make_inline_data_table_key(desc.name, desc.table);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeDataTable;
        node.schema = kInlineDataTableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = InlineDataTableCompileDesc{
            .table = desc.table,
        };

        if (!system_.register_asset(std::move(node), {})) {
            logger_.error("failed to register data table: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    DataTableHandle DataTableAssetModule::get_table(
        const DataTableAsset& asset) const
    {
        if (!asset.valid())
            return {};

        DataTableHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("data table handle not found");

        return out;
    }

    const DataTableData* DataTableAssetModule::get_table_data(
        DataTableHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}
