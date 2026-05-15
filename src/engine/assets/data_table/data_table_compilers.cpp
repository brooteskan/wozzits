// src/engine/assets/data_table/data_table_compilers.cpp

#include <engine/assets/data_table/data_table_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>
#include <span>

namespace wz::engine::assets::internal
{
    void register_data_table_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        DataTable& table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kInlineDataTableSchema,
            .output_type = kAssetTypeDataTable,
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<InlineDataTableCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("inline data table missing compile desc");
                    return compile_failed_node(input);
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
