#pragma once

// engine/assets/csv_export/csv_export.h

#include <asset/types.h>
#include <engine/assets/data_table_asset_module.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct CSVExportData
    {
        std::string output_path;
        uint32_t column_count = 0;
        uint32_t row_count = 0;

        bool valid() const noexcept;
    };

    class CSVExportTable
    {
    public:
        CSVExportTable();

        wz::asset::ResourceHandle add(CSVExportData data);
        const CSVExportData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<CSVExportData> exports_;
        std::vector<uint32_t> epochs_;
    };

    struct CSVExportDesc
    {
        std::string name;

        DataTableAsset source{};

        std::string output_path;
        char separator = ',';
        bool include_header = true;
    };

    struct CSVExportCompileDesc
    {
        std::string output_path;
        char separator = ',';
        bool include_header = true;
    };
}
