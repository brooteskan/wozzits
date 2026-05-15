// src/engine/assets/csv_export/csv_export_compilers.cpp

#include <engine/assets/csv_export/csv_export_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>

#include <any>
#include <span>
#include <string>

namespace wz::engine::assets::internal
{
    namespace
    {
        std::string escape_csv_field(const std::string& field, char separator)
        {
            const bool needs_quoting =
                field.find(separator) != std::string::npos ||
                field.find('"')       != std::string::npos ||
                field.find('\n')      != std::string::npos ||
                field.find('\r')      != std::string::npos;

            if (!needs_quoting)
                return field;

            std::string out;
            out.reserve(field.size() + 2);
            out += '"';

            for (const char c : field) {
                if (c == '"')
                    out += '"';
                out += c;
            }

            out += '"';
            return out;
        }

        std::string build_csv(
            const DataTableData& table,
            char separator,
            bool include_header)
        {
            std::string out;

            if (include_header && !table.columns.empty()) {
                for (uint32_t i = 0; i < table.column_count(); ++i) {
                    if (i > 0)
                        out += separator;
                    out += escape_csv_field(table.columns[i].name, separator);
                }
                out += "\r\n";
            }

            for (const DataTableRow& row : table.rows) {
                for (uint32_t i = 0;
                    i < static_cast<uint32_t>(row.cells.size());
                    ++i)
                {
                    if (i > 0)
                        out += separator;
                    out += escape_csv_field(row.cells[i], separator);
                }
                out += "\r\n";
            }

            return out;
        }
    }

    void register_csv_export_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DataTable& data_table,
        CSVExportTable& csv_export_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCSVExportSchema,
            .output_type  = kAssetTypeCSVExport,
            .compile = [&logger, &data_table, &csv_export_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<CSVExportCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("csv export missing compile desc");
                    return compile_failed_node(input);
                }

                if (desc->output_path.empty()) {
                    logger.error("csv export has empty output path");
                    return compile_failed_node(input);
                }

                if (dep_nodes.size() != 1 || dep_handles.size() != 1) {
                    logger.error(
                        "csv export requires exactly one source table dependency");
                    return compile_failed_node(input);
                }

                const DataTableData* source =
                    data_table.get(dep_handles[0]);

                if (!source || !source->valid()) {
                    logger.error("csv export source table is invalid");
                    return compile_failed_node(input);
                }

                const std::string csv_text =
                    build_csv(*source, desc->separator, desc->include_header);

                const wz::fs::FileError write_err =
                    wz::fs::write_file_text(desc->output_path, csv_text);

                if (write_err != wz::fs::FileError::None) {
                    logger.error(
                        "csv export failed to write file: " + desc->output_path);
                    return compile_failed_node(input);
                }

                CSVExportData export_data{
                    .output_path   = desc->output_path,
                    .column_count  = source->column_count(),
                    .row_count     = source->row_count(),
                };

                wz::asset::ResourceHandle handle =
                    csv_export_table.add(std::move(export_data));

                if (!handle.valid()) {
                    logger.error("failed to store csv export data");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage   = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });
    }
}
