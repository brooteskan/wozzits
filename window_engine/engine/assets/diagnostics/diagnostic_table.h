#pragma once

// engine/assets/diagnostics/diagnostic_table.h

#include <asset/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct DiagnosticColumn
    {
        std::string name;
    };

    struct DiagnosticRow
    {
        std::vector<std::string> cells;
    };

    struct DiagnosticTableData
    {
        uint32_t schema_version = 1;

        std::vector<DiagnosticColumn> columns;
        std::vector<DiagnosticRow> rows;

        bool valid() const noexcept;
        uint32_t column_count() const noexcept;
        uint32_t row_count() const noexcept;
    };

    class DiagnosticTable
    {
    public:
        DiagnosticTable();

        wz::asset::ResourceHandle add(DiagnosticTableData table);
        const DiagnosticTableData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        std::vector<DiagnosticTableData> tables_;
        std::vector<uint32_t> epochs_;
    };

    struct InlineDiagnosticTableDesc
    {
        std::string name;
        DiagnosticTableData table;
    };

    struct InlineDiagnosticTableCompileDesc
    {
        DiagnosticTableData table;
    };
}