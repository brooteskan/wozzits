// src/engine/assets/shader/shader_compilers.cpp

#include <engine/assets/shader/shader_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <file/filesystem.h>
#include <gpu/shader.h>

namespace wz::engine::assets::internal
{
    void register_shader_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        wz::gpu::Device& device)
    {
        // ── HLSL file carrier compiler ────────────────────────────────────────
        //
        // Separate from the raw file carrier so HLSL files are not confused
        // with raw binary assets at the schema dispatch level.

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
                    logger.error("HLSL file carrier missing FileSourceDesc");
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


        // ── HLSL shader compiler ──────────────────────────────────────────────

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
    }

} // namespace wz::engine::assets::internal
