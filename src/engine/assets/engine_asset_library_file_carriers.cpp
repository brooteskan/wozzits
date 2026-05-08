// src/engine/assets/engine_asset_library_file_carriers.cpp

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>

#include <any>
#include <span>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        wz::asset::AssetNode compile_file_byte_carrier(
            const wz::asset::AssetNode& input,
            wz::Logger& logger,
            std::string_view label)
        {
            const auto* file =
                std::any_cast<FileSourceDesc>(&input.meta);

            if (!file) {
                logger.error(std::string(label) + " missing FileSourceDesc");
                return compile_failed_node(input);
            }

            auto file_result = wz::fs::read_file(file->full_path);
            if (!file_result) {
                logger.error("failed to read file: " + file->full_path);
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
            std::string_view label)
        {
            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = schema,
                .output_type = type,
                .compile = [&logger, label](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    return compile_file_byte_carrier(input, logger, label);
                }
                });
        }
    }

    void register_file_carrier_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger)
    {
        register_byte_file_carrier(
            registry,
            logger,
            kRawFileSchema,
            kAssetTypeRawFile,
            "raw file carrier"
        );

        register_byte_file_carrier(
            registry,
            logger,
            kTextFileSchema,
            kAssetTypeTextFile,
            "text file carrier"
        );

        register_byte_file_carrier(
            registry,
            logger,
            kHLSLFileSchema,
            wz::asset::AssetType::ShaderSource,
            "HLSL file carrier"
        );

        register_byte_file_carrier(
            registry,
            logger,
            kBinaryBlobSchema,
            kAssetTypeBinaryBlob,
            "binary blob carrier"
        );

        register_byte_file_carrier(
            registry,
            logger,
            kImportedSourceFileSchema,
            kAssetTypeImportedSourceFile,
            "imported source file carrier"
        );

        register_byte_file_carrier(
            registry,
            logger,
            kCustomBinaryFileSchema,
            kAssetTypeBinaryBlob,
            "custom binary file carrier"
        );

        register_byte_file_carrier(
            registry,
            logger,
            kCSVFileSchema,
            kAssetTypeRawFile,
            "CSV file carrier"
        );
    }

} // namespace wz::engine::assets::internal