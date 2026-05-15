// src/engine/assets/data_table/data_table.cpp

#include <engine/assets/data_table/data_table.h>
#include <engine/assets/type_extensions.h>

#include <utility>

namespace wz::engine::assets
{
    bool DataTableData::valid() const noexcept
    {
        if (schema_version == 0)
            return false;

        if (columns.empty())
            return false;

        for (const DataTableColumn& column : columns) {
            if (column.name.empty())
                return false;
        }

        const size_t expected = columns.size();

        for (const DataTableRow& row : rows) {
            if (row.cells.size() != expected)
                return false;
        }

        return true;
    }

    uint32_t DataTableData::column_count() const noexcept
    {
        return static_cast<uint32_t>(columns.size());
    }

    uint32_t DataTableData::row_count() const noexcept
    {
        return static_cast<uint32_t>(rows.size());
    }

    DataTable::DataTable()
    {
        tables_.emplace_back();
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle DataTable::add(DataTableData table)
    {
        if (!table.valid())
            return {};

        const uint32_t id = static_cast<uint32_t>(tables_.size());

        tables_.push_back(std::move(table));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = epochs_[id],
            .type = kAssetTypeDataTable,
        };
    }

    const DataTableData* DataTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeDataTable)
            return nullptr;

        if (handle.id >= tables_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &tables_[handle.id];
    }

    void DataTable::destroy()
    {
        tables_.clear();
        epochs_.clear();

        tables_.emplace_back();
        epochs_.push_back(0);
    }
}
