// src/engine/assets/data_table/data_table_compilers.cpp

#include <engine/assets/data_table/data_table_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>
#include <string>
#include <span>
#include <utility>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        std::vector<std::string> split_cells(
            const std::string& text,
            char separator)
        {
            std::vector<std::string> out;
            std::string cell;
            for (char ch : text) {
                if (ch == separator) {
                    out.push_back(std::move(cell));
                    cell.clear();
                }
                else {
                    cell.push_back(ch);
                }
            }
            out.push_back(std::move(cell));
            return out;
        }

        InlineDataTableCompileDesc inline_data_table_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            InlineDataTableCompileDesc desc{};
            desc.table.schema_version =
                params.get<uint32_t>("schema_version", 1u);

            const std::string columns =
                params.get<std::string>("columns", "value");
            for (std::string& column : split_cells(columns, ',')) {
                if (!column.empty()) {
                    desc.table.columns.push_back(
                        DataTableColumn{ .name = std::move(column) });
                }
            }

            const std::string rows = params.get<std::string>("rows", {});
            std::string row_text;
            for (char ch : rows) {
                if (ch == '\n') {
                    if (!row_text.empty()) {
                        desc.table.rows.push_back(
                            DataTableRow{
                                .cells = split_cells(row_text, ','),
                            });
                    }
                    row_text.clear();
                }
                else if (ch != '\r') {
                    row_text.push_back(ch);
                }
            }
            if (!row_text.empty()) {
                desc.table.rows.push_back(
                    DataTableRow{ .cells = split_cells(row_text, ',') });
            }

            return desc;
        }
    }

    void register_data_table_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DataTable& table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kInlineDataTableSchema,
            .output_type = kAssetTypeDataTable,
            .parameters = {
                {
                    .name = "schema_version",
                    .type = wz::asset::ParamType::Int,
                    .label = "Schema version",
                    .default_num = 1.0,
                    .min = 1.0,
                    .max = 1024.0,
                },
                {
                    .name = "columns",
                    .type = wz::asset::ParamType::String,
                    .label = "Columns",
                    .default_str = "value",
                },
                {
                    .name = "rows",
                    .type = wz::asset::ParamType::String,
                    .label = "Rows",
                },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                InlineDataTableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<InlineDataTableCompileDesc>(&input.meta);

                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        editor_desc =
                            inline_data_table_desc_from_params(*params);
                        desc = &editor_desc;
                    }
                    else {
                        logger.error("inline data table missing compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (!dep_nodes.empty()) {
                    logger.error("inline data table should not have dependencies");
                    return compile_failed_node(input);
                }

                if (!desc->table.valid()) {
                    logger.error("inline data table data is invalid");
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    table.add(desc->table);

                if (!handle.valid()) {
                    logger.error("failed to store data table");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }
}
