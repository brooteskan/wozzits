// src/engine/assets/json/json_compilers.cpp

#include <engine/assets/json/json_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <external/json/json_parser.h>

#include <vector>

namespace wz::engine::assets::internal
{
    void register_json_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        JSONTable& json_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kJSONDocumentSchema,
            .output_type = kAssetTypeJSONDocument,
            .compile = [&logger, &json_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                if (dep_nodes.empty()) {
                    logger.error("JSON document node has no file dependency");
                    return compile_failed_node(input);
                }

                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

                if (!bytes) {
                    logger.error("JSON file dep node has no byte payload");
                    return compile_failed_node(input);
                }

                wz::json::JSONParseResult parsed =
                    wz::json::parse_json_bytes(*bytes);

                if (!parsed.ok) {
                    logger.error("failed to parse JSON: " + parsed.error.message);
                    return compile_failed_node(input);
                }

                JSONData data;
                data.document = std::move(parsed.document);

                wz::asset::ResourceHandle handle =
                    json_table.add(std::move(data));

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

} // namespace wz::engine::assets::internal
