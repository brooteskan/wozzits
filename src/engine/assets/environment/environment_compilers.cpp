// src/engine/assets/environment/environment_compilers.cpp

#include <engine/assets/environment/environment_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <span>

namespace wz::engine::assets::internal
{
    namespace
    {
        // First dependency of a given asset type, or a zero key when the port is
        // unconnected. FrameEnvironment's ports are all optional and each accepts
        // a DISTINCT asset type, so type is an unambiguous locator — and, unlike
        // positional indexing, it is stable when only some ports are connected
        // (dep ordering is not guaranteed once an optional port exists, the trap
        // issue #218 documents; the renderable compiler uses the same helper).
        wz::asset::AssetKey dep_key_of_type(
            std::span<const wz::asset::AssetNode* const> dep_nodes,
            wz::asset::AssetType type)
        {
            for (const wz::asset::AssetNode* dep : dep_nodes) {
                if (dep->type == type) {
                    return dep->key;
                }
            }
            return {};
        }

        wz::asset::AssetNode compiled_environment_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }

    void register_environment_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        EnvironmentTable& environment_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kFrameEnvironmentSchema,
            .output_type = kAssetTypeFrameEnvironment,
            // Four OPTIONAL ports, each a distinct frame-global environment asset.
            // Optional so a frame can author any subset; the compile fn locates
            // each dep by TYPE, so declaration order here is not a binding
            // contract (an unconnected port simply drops out of dep_nodes). Sky is
            // absent by design — it is inline scene state, not an asset-graph node
            // yet, so there is nothing to wire an edge to.
            .input_ports = {
                { "atmosphere", kAssetTypeAtmosphere,
                  wz::asset::InputPortRequirement::Optional },
                { "ambient_lighting", kAssetTypeAmbientLighting,
                  wz::asset::InputPortRequirement::Optional },
                { "hdri_environment", kAssetTypeEnvironmentMap,
                  wz::asset::InputPortRequirement::Optional },
                { "directional_light", kAssetTypeDirectLight,
                  wz::asset::InputPortRequirement::Optional },
            },
            // Only a name — an identity input (make_environment_key) so two
            // environments referencing the same pieces stay distinct assets. The
            // bundle itself is derived entirely from the connected ports, so the
            // compile fn never reads this.
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
            },
            .compile = [&logger, &environment_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                // Locate each role by asset type. A missing role yields a zero
                // key — a legitimate "no such piece", not an error.
                EnvironmentData data{};
                data.atmosphere =
                    dep_key_of_type(dep_nodes, kAssetTypeAtmosphere);
                data.ambient_lighting =
                    dep_key_of_type(dep_nodes, kAssetTypeAmbientLighting);
                data.hdri_environment =
                    dep_key_of_type(dep_nodes, kAssetTypeEnvironmentMap);
                data.directional_light =
                    dep_key_of_type(dep_nodes, kAssetTypeDirectLight);

                if (!data.valid()) {
                    logger.error("compiled frame environment is invalid");
                    return compile_failed_node(
                        input, "compiled frame environment is invalid");
                }

                wz::asset::ResourceHandle handle =
                    environment_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store frame environment");
                    return compile_failed_node(
                        input, "failed to store frame environment");
                }

                return compiled_environment_node(input, handle);
            }
        });
    }
}
