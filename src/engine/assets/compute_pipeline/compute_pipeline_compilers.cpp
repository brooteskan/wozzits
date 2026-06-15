#include <engine/assets/compute_pipeline/compute_pipeline_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>
#include <span>
#include <utility>

namespace wz::engine::assets::internal
{
    void register_compute_pipeline_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ComputePipelineTable& table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kComputePipelineSchema,
            .output_type = kAssetTypeComputePipeline,
            .input_ports = {
                { "compute_shader", wz::asset::AssetType::Shader },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<ComputePipelineDesc>(&input.meta);

                if (!desc) {
                    logger.error("compute pipeline missing ComputePipelineDesc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger.error("compute pipeline requires exactly one compute shader dependency");
                    return compile_failed_node(input);
                }

                if (!dep_handles[0].valid()) {
                    logger.error("compute pipeline shader dependency did not resolve");
                    return compile_failed_node(input);
                }

                ComputePipelineData data{};
                data.name = desc->name;
                data.bindings = desc->bindings;
                data.root_constant_dwords = desc->root_constant_dwords;
                data.thread_group_size_x = desc->thread_group_size_x;
                data.thread_group_size_y = desc->thread_group_size_y;
                data.thread_group_size_z = desc->thread_group_size_z;
                data.compute_shader = dep_handles[0];

                wz::asset::ResourceHandle handle = table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store compute pipeline");
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
