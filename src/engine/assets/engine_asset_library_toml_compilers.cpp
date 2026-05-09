// src/engine/assets/engine_asset_library_toml_compilers.cpp

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/toml/toml.h>

#include <external/toml/toml_parser.h>

#include <vector>

namespace wz::engine::assets::internal
{
    void register_toml_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        TOMLTable& toml_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTOMLDocumentSchema,
            .output_type = kAssetTypeTOMLDocument,
            .compile = [&logger, &toml_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                if (dep_nodes.empty()) {
                    logger.error("TOML document node has no file dependency");
                    return compile_failed_node(input);
                }

                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

                if (!bytes) {
                    logger.error("TOML file dep node has no byte payload");
                    return compile_failed_node(input);
                }

                wz::toml::TOMLParseResult parsed =
                    wz::toml::parse_toml_bytes(*bytes);

                if (!parsed.ok) {
                    logger.error("failed to parse TOML: " + parsed.error.message);
                    return compile_failed_node(input);
                }

                TOMLData data;
                data.document = std::move(parsed.document);

                wz::asset::ResourceHandle handle =
                    toml_table.add(std::move(data));

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

} // namespace wz::engine::assets::internal