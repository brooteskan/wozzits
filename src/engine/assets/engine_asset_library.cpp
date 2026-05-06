#include <engine/assets/engine_asset_library.h>

#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/key_factories/hlsl_shader.h>

#include <engine/assets/shader/shader_types.h>
#include <gpu/shader.h>

#include <cassert>
#include <span>
#include <vector>
#include <any>

namespace
{
    
}

namespace wz::engine::assets
{
    namespace
    {

        struct FileSourceDesc
        {
            wz::fs::Path full_path;
            std::string canonical_path;
        };

        static wz::asset::AssetNode compile_failed_node(
            const wz::asset::AssetNode& input)
        {
            return input;
        }

        wz::asset::CompilerRegistry make_engine_compiler_registry(
            wz::gpu::Device& device,
            wz::Logger& logger)
        {
            wz::asset::CompilerRegistry registry;

            // File carrier compiler.
            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kHLSLFileSchema,
                .output_type = wz::asset::AssetType::ShaderSource,
                .compile = [&logger](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    const auto* file =
                        std::any_cast<FileSourceDesc>(&input.meta);

                    if (!file) {
                        logger.error("file node missing FileSourceDesc");
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
                });

            // HLSL compiler.
            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kHLSLShaderSchema,
                .output_type = wz::asset::AssetType::Shader,
                .compile = [&logger, &device](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    const auto* desc =
                        std::any_cast<wz::gpu::HLSLCompileDesc>(&input.meta);

                    if (!desc) {
                        logger.error("shader node missing HLSLCompileDesc");
                        return compile_failed_node(input);
                    }

                    if (dep_nodes.empty()) {
                        logger.error("shader node has no source dependencies");
                        return compile_failed_node(input);
                    }

                    if (desc->primary_source_index >= dep_nodes.size()) {
                        logger.error("shader primary_source_index out of range");
                        return compile_failed_node(input);
                    }

                    std::vector<std::span<const uint8_t>> sources;
                    sources.reserve(dep_nodes.size());

                    for (const wz::asset::AssetNode& dep : dep_nodes) {
                        const auto* bytes =
                            std::get_if<std::vector<uint8_t>>(&dep.payload);

                        if (!bytes) {
                            logger.error("file dep node has no byte payload");
                            return compile_failed_node(input);
                        }

                        sources.push_back({ bytes->data(), bytes->size() });
                    }

                    wz::gpu::GPUHandle gpu_handle =
                        wz::gpu::compile_hlsl(device, sources, *desc);

                    if (!gpu_handle.valid()) {
                        logger.error("gpu.compile_hlsl failed");
                        return compile_failed_node(input);
                    }

                    wz::asset::AssetNode out = input;
                    out.stage = wz::asset::AssetStage::Compiled;
                    out.payload = wz::asset::ResourceHandle{
                        .id = gpu_handle.id,
                        .epoch = gpu_handle.epoch,
                        .type = wz::asset::AssetType::Shader,
                    };

                    return out;
                }
                });

            return registry;
        }
    }

    EngineAssetLibrary::EngineAssetLibrary(
        wz::gpu::Device& device,
        wz::Logger& logger,
        wz::fs::Path resource_root)
        : device_(device)
        , logger_(logger)
        , resource_root_(std::move(resource_root))
        , system_(make_engine_compiler_registry(device, logger))
    {
    }

    wz::asset::AssetKey EngineAssetLibrary::register_file_node(
        const wz::fs::Path& relative_path,
        wz::asset::SchemaID schema,
        wz::asset::AssetType type)
    {
        const std::string canonical =
            detail::canonical_asset_path(relative_path);

        const wz::asset::AssetKey key =
            make_file_key(canonical, schema);

        const wz::fs::Path full_path =
            wz::fs::join(resource_root_, relative_path);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = type;
        node.schema = schema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = FileSourceDesc{
            .full_path = full_path,
            .canonical_path = canonical,
        };

        if (!system_.register_asset(std::move(node))) {
            logger_.error("failed to register file node: " + canonical);
            return {};
        }

        return key;
    }

    wz::asset::AssetKey EngineAssetLibrary::register_hlsl_shader_node(
        std::string_view name,
        wz::asset::AssetKey source_file,
        const wz::gpu::HLSLCompileDesc& desc)
    {
        const wz::asset::AssetKey key =
            make_hlsl_shader_key(
                source_file,
                static_cast<uint8_t>(desc.stage),
                desc.entry,
                desc.target
            );

        wz::asset::AssetNode node;
        node.key = key;
        node.type = wz::asset::AssetType::Shader;
        node.schema = kHLSLShaderSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = desc;

        if (!system_.register_asset(
            std::move(node),
            std::vector<wz::asset::AssetKey>{ source_file }))
        {
            logger_.error("failed to register HLSL shader node");
            return {};
        }

        return key;
    }

    ShaderPairAsset EngineAssetLibrary::create_shader_pair(
        const ShaderPairDesc& desc)
    {
        ShaderPairAsset out{};

        const wz::asset::AssetKey vs_file =
            register_file_node(
                desc.vertex_path,
                kHLSLFileSchema,
                wz::asset::AssetType::ShaderSource
            );

        if (vs_file == wz::asset::AssetKey{}) {
            logger_.error("failed to register vertex shader file: " + desc.vertex_path);
            return out;
        }

        const wz::asset::AssetKey ps_file =
            register_file_node(
                desc.pixel_path,
                kHLSLFileSchema,
                wz::asset::AssetType::ShaderSource
            );

        if (ps_file == wz::asset::AssetKey{}) {
            logger_.error("failed to register pixel shader file: " + desc.pixel_path);
            return out;
        }

        wz::gpu::HLSLCompileDesc vs_desc{
            .stage = wz::gpu::ShaderStage::Vertex,
            .entry = desc.vertex_entry,
            .target = desc.vertex_target,
            .primary_source_index = 0,
        };

        wz::gpu::HLSLCompileDesc ps_desc{
            .stage = wz::gpu::ShaderStage::Pixel,
            .entry = desc.pixel_entry,
            .target = desc.pixel_target,
            .primary_source_index = 0,
        };

        out.vertex_shader =
            register_hlsl_shader_node(
                desc.name + ":vs",
                vs_file,
                vs_desc
            );

        out.pixel_shader =
            register_hlsl_shader_node(
                desc.name + ":ps",
                ps_file,
                ps_desc
            );

        return out;
    }

    bool EngineAssetLibrary::commit()
    {
        if (!system_.commit()) {
            logger_.error("asset graph rejected — cycle or missing dependency");
            return false;
        }

        return true;
    }

    uint32_t EngineAssetLibrary::resolve_all()
    {
        std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>> errors;

        const uint32_t count = system_.resolve_all(&errors);

        if (!errors.empty()) {
            logger_.error("asset resolve_all failed");
        }

        return count;
    }

    ShaderPairHandles EngineAssetLibrary::get_shader_pair(
        const ShaderPairAsset& asset) const
    {
        ShaderPairHandles out{};

        if (!asset.valid())
            return out;

        if (const auto* vs = system_.find_compiled(asset.vertex_shader)) {
            out.vertex = vs->handle;
        }

        if (const auto* ps = system_.find_compiled(asset.pixel_shader)) {
            out.pixel = ps->handle;
        }

        if (!out.vertex.valid())
            logger_.error("vertex shader handle not found");

        if (!out.pixel.valid())
            logger_.error("pixel shader handle not found");

        return out;
    }


}