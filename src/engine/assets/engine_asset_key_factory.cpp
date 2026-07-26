#include <engine/assets/engine_asset_key_factory.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/key_factories/hlsl_shader.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/shader/hlsl_compile_desc.h>
#include <engine/assets/type_extensions.h>

#include <file/filesystem.h>
#include <gpu/shader_types.h>

#include <any>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace wz::engine::assets
{
    namespace
    {
        bool is_file_carrier_schema(wz::asset::SchemaID schema) noexcept
        {
            return schema == kRawFileSchema
                || schema == kHLSLFileSchema
                || schema == kTextFileSchema
                || schema == kBinaryBlobSchema
                || schema == kImportedSourceFileSchema
                || schema == kCustomBinaryFileSchema
                || schema == kJSONFileSchema
                || schema == kYAMLFileSchema
                || schema == kTOMLFileSchema
                || schema == kXMLFileSchema
                || schema == kCSVFileSchema;
        }


        std::optional<wz::asset::AssetKey> file_carrier_key(
            const wz::asset::AssetNode& node)
        {
            if (!is_file_carrier_schema(node.schema)) {
                return std::nullopt;
            }

            std::string canonical_path;
            std::string full_path;
            if (const auto* file =
                    std::any_cast<internal::FileSourceDesc>(&node.meta))
            {
                canonical_path = file->canonical_path;
                full_path = file->full_path;
            }
            else if (const auto* params =
                         std::any_cast<wz::asset::ParamBlock>(&node.meta))
            {
                const std::string source_path =
                    params->get<std::string>("source_path", {});
                canonical_path =
                    detail::canonical_asset_path(source_path);
                if (wz::fs::is_absolute(source_path)
                    || wz::fs::exists(source_path))
                {
                    full_path = source_path;
                }
            }

            if (canonical_path.empty()) {
                return std::nullopt;
            }

            if (!full_path.empty()) {
                const wz::fs::FileResult<wz::fs::Buffer> file =
                    wz::fs::read_file(full_path);
                if (file) {
                    return make_file_content_key(
                        canonical_path,
                        file.value,
                        node.schema);
                }
            }

            return make_file_key(canonical_path, node.schema);
        }

        std::optional<wz::gpu::HLSLCompileDesc> hlsl_desc_from_node(
            const wz::asset::AssetNode& node)
        {
            if (const auto* desc =
                    std::any_cast<wz::gpu::HLSLCompileDesc>(&node.meta))
            {
                return *desc;
            }

            const auto* params =
                std::any_cast<wz::asset::ParamBlock>(&node.meta);
            if (!params) {
                return std::nullopt;
            }

            return internal::hlsl_compile_desc_from_params(*params);
        }

        std::optional<wz::asset::AssetKey> hlsl_shader_key(
            const wz::asset::CompilerRegistry& registry,
            const wz::asset::AssetNode& node,
            std::span<const wz::asset::AssetKey> deps)
        {
            if (node.schema != kHLSLShaderSchema
                || node.type != wz::asset::AssetType::Shader)
            {
                return std::nullopt;
            }

            // EVERY source, in order. A shader may declare several (#289: the
            // shared BRDF wired ahead of the entry body), and the port is Many,
            // so its deps ARE the source list. Taking only the first would give
            // two variants sharing a common source -- and agreeing on
            // stage/entry/target -- the same key.
            if (deps.empty() || deps[0] == wz::asset::AssetKey{}) {
                return std::nullopt;
            }

            const std::optional<wz::gpu::HLSLCompileDesc> desc =
                hlsl_desc_from_node(node);
            if (!desc) {
                return std::nullopt;
            }

            return make_hlsl_shader_key(
                deps,
                static_cast<uint8_t>(desc->stage),
                desc->entry,
                desc->target);
        }
    }

    bool engine_key_factory_handles(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type) noexcept
    {
        return is_file_carrier_schema(schema)
            || (schema == kHLSLShaderSchema
                && type == wz::asset::AssetType::Shader);
    }

    wz::asset::AssetKeyFactoryFn make_engine_asset_key_factory(
        const wz::asset::CompilerRegistry& registry)
    {
        return [&registry](
            const wz::asset::AssetNode& node,
            std::span<const wz::asset::AssetKey> deps)
            -> std::optional<wz::asset::AssetKey>
        {
            if (std::optional<wz::asset::AssetKey> key =
                    file_carrier_key(node))
            {
                return key;
            }
            if (std::optional<wz::asset::AssetKey> key =
                    hlsl_shader_key(registry, node, deps))
            {
                return key;
            }
            return std::nullopt;
        };
    }
}
