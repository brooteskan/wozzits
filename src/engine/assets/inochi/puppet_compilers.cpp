// src/engine/assets/inochi/puppet_compilers.cpp
//
// The puppet-from-file compiler: a RawFile .inp/.inx dependency -> an in-memory
// Puppet (inochi::load_puppet) -> GPU residency (inochi::publish_resident_puppet,
// when a shared registry is present) -> a PuppetTable entry. Mirrors the PLY /
// scalar-field gaussian-splat compilers. See puppet_compilers.h.

#include <engine/assets/inochi/puppet_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>  // compile_failed_node
#include <engine/assets/inochi/inochi_puppet.h>           // load_puppet
#include <engine/assets/inochi/puppet_gpu.h>              // publish_resident_puppet
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <wozzits/rhi/gpu_resource_registry.h>

#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        wz::asset::AssetNode compile_puppet_from_file_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode* const> dep_nodes,
            PuppetTable& table,
            wz::rhi::GpuResourceRegistry* gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            if (dep_nodes.size() != 1) {
                logger.error("puppet from file expects exactly one file dependency");
                return compile_failed_node(input);
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0]->payload);
            if (!bytes || bytes->empty()) {
                logger.error("puppet source file dependency has no bytes");
                return compile_failed_node(input);
            }

            inochi::Puppet puppet;
            std::string error;
            if (!inochi::load_puppet(
                    bytes->data(), bytes->size(), puppet, &error)) {
                logger.error("failed to load puppet: " + error);
                return compile_failed_node(input);
            }

            PuppetData data{};
            if (gpu_resources) {
                // Best-effort: a residency failure is logged and leaves
                // data.resident empty (the renderable stays unrealizable), but
                // the source is still stored so the graph resolves.
                if (!inochi::publish_resident_puppet(
                        input.key, puppet, *gpu_resources,
                        rhi_resource_tracker, logger, data.resident))
                {
                    logger.warn(
                        "puppet RHI residency failed; puppet not yet renderable");
                }
            }
            data.source = std::move(puppet);

            const wz::asset::ResourceHandle handle = table.add(std::move(data));
            if (!handle.valid()) {
                logger.error("failed to store puppet (no nodes?)");
                return compile_failed_node(input);
            }

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    } // namespace

    void register_puppet_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        PuppetTable& table,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kPuppetFromFileSchema,
            .output_type = kAssetTypePuppet,
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            .compile = [&logger, &table, gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_puppet_from_file_node(
                    input, dep_nodes, table, gpu_resources,
                    rhi_resource_tracker, logger);
            }
            });
    }
}
