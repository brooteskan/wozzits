#pragma once

// engine/assets/data_table_asset_module.h

#include <asset/system.h>
#include <engine/assets/data_table/data_table.h>
#include <logging/logger.h>

namespace wz::engine::assets
{
    struct DataTableAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct DataTableHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class DataTableAssetModule
    {
    public:
        DataTableAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            DataTable& table);

        DataTableAsset create_inline_table(
            const InlineDataTableDesc& desc);

        DataTableHandle get_table(
            const DataTableAsset& asset) const;

        const DataTableData* get_table_data(
            DataTableHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        DataTable& table_;
    };
}
