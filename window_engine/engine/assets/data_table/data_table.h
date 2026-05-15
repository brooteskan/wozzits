#pragma once

// engine/assets/data_table/data_table.h

#include <asset/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct DataTableColumn
    {
        std::string name;
    };

    struct DataTableRow
    {
        std::vector<std::string> cells;
    };

    struct DataTableData
    {
        uint32_t schema_version = 1;

        std::vector<DataTableColumn> columns;
        std::vector<DataTableRow> rows;

        bool valid() const noexcept;
        uint32_t column_count() const noexcept;
        uint32_t row_count() const noexcept;
    };

    class DataTable
    {
    public:
        DataTable();

        wz::asset::ResourceHandle add(DataTableData table);
        const DataTableData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<DataTableData> tables_;
        std::vector<uint32_t> epochs_;
    };

    struct InlineDataTableDesc
    {
        std::string name;
        DataTableData table;
    };

    struct InlineDataTableCompileDesc
    {
        DataTableData table;
    };
}
