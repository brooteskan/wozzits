// src/engine/assets/engine_asset_library_file_carriers.cpp

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>

#include <any>
#include <cctype>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        std::string trim_quoted_path(std::string_view raw)
        {
            while (!raw.empty()
                && std::isspace(static_cast<unsigned char>(raw.front())))
            {
                raw.remove_prefix(1);
            }
            while (!raw.empty()
                && std::isspace(static_cast<unsigned char>(raw.back())))
            {
                raw.remove_suffix(1);
            }
            if (raw.size() >= 2
                && ((raw.front() == '"' && raw.back() == '"')
                    || (raw.front() == '\'' && raw.back() == '\'')))
            {
                raw.remove_prefix(1);
                raw.remove_suffix(1);
            }
            else {
                const size_t first_quote = raw.find('"');
                const size_t last_quote = raw.rfind('"');
                if (first_quote != std::string_view::npos
                    && last_quote != first_quote)
                {
                    const std::string embedded{
                        raw.substr(first_quote + 1u, last_quote - first_quote - 1u) };
                    if (wz::fs::is_absolute(embedded)) {
                        return embedded;
                    }
                }
            }
            return std::string(raw);
        }

        wz::asset::AssetNode compile_file_byte_carrier(
            const wz::asset::AssetNode& input,
            wz::Logger& logger,
            std::string_view label,
            const wz::fs::Path& resource_root)
        {
            std::string path;
            const auto* file =
                std::any_cast<FileSourceDesc>(&input.meta);
            if (file) {
                path = file->full_path;
            }
            else if (const auto* params =
                         std::any_cast<wz::asset::ParamBlock>(&input.meta))
            {
                path = params->get<std::string>("source_path", {});
            }
            path = trim_quoted_path(path);

            if (path.empty()) {
                logger.error(
                    std::string(label)
                    + " missing FileSourceDesc or source_path parameter");
                return compile_failed_node(input);
            }

            // Resolve a RELATIVE carrier path against the library resource_root
            // (the project directory at runtime), so a relocated project's graph
            // can store project-relative source paths. Absolute paths are used
            // unchanged; an empty resource_root (schema-only registries, unit
            // tests) leaves the path as-is, preserving the historical
            // read-relative-to-CWD behavior.
            if (!resource_root.empty() && !wz::fs::is_absolute(path)) {
                path = wz::fs::join(resource_root, path);
            }

            auto file_result = wz::fs::read_file(path);
            if (!file_result) {
                logger.error("failed to read file: " + path);
                return compile_failed_node(input);
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = std::move(file_result.value);
            return out;
        }

        void register_byte_file_carrier(
            wz::asset::CompilerRegistry& registry,
            wz::Logger& logger,
            wz::asset::SchemaID schema,
            wz::asset::AssetType type,
            std::string_view label,
            const wz::fs::Path& resource_root)
        {
            // resource_root is captured BY COPY: the EngineAssetContext that
            // supplies it is a temporary at the registration call site, so a
            // by-reference capture would dangle when the compile fn runs later.
            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = schema,
                .output_type = type,
                .parameters = {
                    {
                        .name = "source_path",
                        .type = wz::asset::ParamType::FilePath,
                        .label = "Path",
                    },
                },
                .compile = [&logger, label, resource_root](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode* const>,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    return compile_file_byte_carrier(
                        input, logger, label, resource_root);
                }
                });
        }
    }

    void register_file_carrier_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        wz::fs::Path resource_root)
    {
        register_byte_file_carrier(
            registry,
            logger,
            kRawFileSchema,
            kAssetTypeRawFile,
            "raw file carrier",
            resource_root
        );

        register_byte_file_carrier(
            registry,
            logger,
            kTextFileSchema,
            kAssetTypeTextFile,
            "text file carrier",
            resource_root
        );

        register_byte_file_carrier(
            registry,
            logger,
            kHLSLFileSchema,
            wz::asset::AssetType::ShaderSource,
            "HLSL file carrier",
            resource_root
        );

        register_byte_file_carrier(
            registry,
            logger,
            kBinaryBlobSchema,
            kAssetTypeBinaryBlob,
            "binary blob carrier",
            resource_root
        );

        register_byte_file_carrier(
            registry,
            logger,
            kImportedSourceFileSchema,
            kAssetTypeImportedSourceFile,
            "imported source file carrier",
            resource_root
        );

        register_byte_file_carrier(
            registry,
            logger,
            kCustomBinaryFileSchema,
            kAssetTypeBinaryBlob,
            "custom binary file carrier",
            resource_root
        );

        register_byte_file_carrier(
            registry,
            logger,
            kCSVFileSchema,
            kAssetTypeRawFile,
            "CSV file carrier",
            resource_root
        );
    }

} // namespace wz::engine::assets::internal
