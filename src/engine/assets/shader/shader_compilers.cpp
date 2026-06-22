// src/engine/assets/shader/shader_compilers.cpp

#include <engine/assets/shader/shader_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/shader/hlsl_compile_desc.h>
#include <engine/rendering/rhi_render_program_bridge.h>
#include <file/filesystem.h>
#include <gpu/shader.h>

#include <wozzits/rhi/shader_module.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <cctype>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr std::array<std::string_view, 3> kShaderStageOptions = {
            "Vertex",
            "Pixel",
            "Compute",
        };

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

        wz::rhi::ShaderStage rhi_shader_stage(wz::gpu::ShaderStage stage)
        {
            switch (stage) {
            case wz::gpu::ShaderStage::Vertex:
                return wz::rhi::ShaderStage::Vertex;
            case wz::gpu::ShaderStage::Pixel:
                return wz::rhi::ShaderStage::Pixel;
            case wz::gpu::ShaderStage::Compute:
                return wz::rhi::ShaderStage::Compute;
            }
            return wz::rhi::ShaderStage::Vertex;
        }
    }

    void register_shader_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        wz::gpu::Device& device,
        wz::rhi::ShaderModuleRegistry* shaders)
    {
        // ── HLSL file carrier compiler ────────────────────────────────────────
        //
        // Separate from the raw file carrier so HLSL files are not confused
        // with raw binary assets at the schema dispatch level.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kHLSLFileSchema,
            .output_type = wz::asset::AssetType::ShaderSource,
            .parameters = {
                {
                    .name = "source_path",
                    .type = wz::asset::ParamType::FilePath,
                    .label = "Path",
                },
            },
            .compile = [&logger](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                std::string path;
                const auto* file =
                    std::any_cast<FileSourceDesc>(&input.meta);
                if (file) {
                    path = file->full_path;
                }
                else if (const auto* params =
                             std::any_cast<wz::asset::ParamBlock>(
                                 &input.meta))
                {
                    path = params->get<std::string>("source_path", {});
                }
                path = trim_quoted_path(path);

                if (path.empty()) {
                    logger.error(
                        "HLSL file carrier missing FileSourceDesc or "
                        "source_path parameter");
                    return compile_failed_node(input);
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
            });


        // ── HLSL shader compiler ──────────────────────────────────────────────

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kHLSLShaderSchema,
            .output_type = wz::asset::AssetType::Shader,
            .input_ports = {
                { "source_file", wz::asset::AssetType::ShaderSource },
            },
            .parameters = {
                {
                    .name = "stage",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Stage",
                    .default_num = 0.0,
                    .options = kShaderStageOptions,
                },
                {
                    .name = "entry",
                    .type = wz::asset::ParamType::String,
                    .label = "Entry",
                    .default_str = "main",
                },
                {
                    .name = "target",
                    .type = wz::asset::ParamType::String,
                    .label = "Target",
                    .default_str = "vs_5_1",
                },
                {
                    .name = "primary_source_index",
                    .type = wz::asset::ParamType::Int,
                    .label = "Primary source index",
                    .default_num = 0.0,
                },
            },
            .compile = [&logger, &device, shaders](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                wz::gpu::HLSLCompileDesc param_desc{};
                auto* desc =
                    std::any_cast<wz::gpu::HLSLCompileDesc>(&input.meta);

                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc = hlsl_compile_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error(
                        "shader node missing HLSLCompileDesc or ParamBlock");
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

                // Dual-output: also D3DCompile to bytecode and register an rhi
                // ShaderModule under shader_ref(key) so the render-program
                // compiler references it instead of re-compiling (#193). Best-
                // effort; the legacy GPUHandle above is what the legacy renderer
                // still consumes until it retires (#179).
                if (shaders) {
                    // Compile the SAME combined source the legacy GPUHandle above
                    // used (compile_hlsl concatenates every dep in order) so the
                    // rhi ShaderModule and the legacy handle stay byte-identical
                    // for multi-source shaders, not only single-source ones.
                    // primary_source_index is a bounds-checked guard, not a
                    // selector — both paths compile every dep.
                    const std::string combined =
                        wz::gpu::combine_hlsl_sources(sources);
                    const std::optional<std::vector<uint8_t>> bytecode =
                        wz::engine::rendering::compile_hlsl_bytecode(
                            std::span<const uint8_t>{
                                reinterpret_cast<const uint8_t*>(combined.data()),
                                combined.size() },
                            desc->entry, desc->target.c_str(), logger);
                    if (bytecode) {
                        shaders->register_program(wz::rhi::ShaderModuleDesc{
                            wz::engine::rendering::shader_ref(input.key),
                            rhi_shader_stage(desc->stage),
                            *bytecode });
                    }
                    else {
                        logger.warn(
                            "shader: rhi ShaderModule compile failed entry="
                            + desc->entry);
                    }
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
