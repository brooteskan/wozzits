#include <engine/assets/compute_pipeline/compute_pipeline_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>
#include <span>
#include <utility>

namespace wz::engine::assets::internal
{
    namespace
    {
        ComputePipelineDesc compute_pipeline_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode* const> dep_nodes)
        {
            ComputePipelineDesc desc{};
            desc.name = params.get<std::string>("name", {});
            if (!dep_nodes.empty()) {
                desc.compute_shader = dep_nodes[0]->key;
            }
            desc.root_constant_dwords =
                params.get<uint32_t>(
                    "root_constant_dwords",
                    desc.root_constant_dwords);
            desc.thread_group_size_x =
                params.get<uint32_t>(
                    "thread_group_size_x",
                    desc.thread_group_size_x);
            desc.thread_group_size_y =
                params.get<uint32_t>(
                    "thread_group_size_y",
                    desc.thread_group_size_y);
            desc.thread_group_size_z =
                params.get<uint32_t>(
                    "thread_group_size_z",
                    desc.thread_group_size_z);
            return desc;
        }
    }

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
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "root_constant_dwords",
                    .type = wz::asset::ParamType::Int,
                    .label = "Root constant dwords",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 256.0,
                },
                {
                    .name = "thread_group_size_x",
                    .type = wz::asset::ParamType::Int,
                    .label = "Thread group size X",
                    .default_num = 1.0,
                    .min = 1.0,
                    .max = 1024.0,
                },
                {
                    .name = "thread_group_size_y",
                    .type = wz::asset::ParamType::Int,
                    .label = "Thread group size Y",
                    .default_num = 1.0,
                    .min = 1.0,
                    .max = 1024.0,
                },
                {
                    .name = "thread_group_size_z",
                    .type = wz::asset::ParamType::Int,
                    .label = "Thread group size Z",
                    .default_num = 1.0,
                    .min = 1.0,
                    .max = 1024.0,
                },
            },
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                ComputePipelineDesc param_desc{};
                const auto* desc =
                    std::any_cast<ComputePipelineDesc>(&input.meta);

                if (dep_handles.size() != 1) {
                    logger.error("compute pipeline requires exactly one compute shader dependency");
                    return compile_failed_node(input);
                }

                if (!dep_handles[0].valid()) {
                    logger.error("compute pipeline shader dependency did not resolve");
                    return compile_failed_node(input);
                }

                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            compute_pipeline_desc_from_params(
                                *params,
                                dep_nodes);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error(
                        "compute pipeline missing ComputePipelineDesc or "
                        "ParamBlock");
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
