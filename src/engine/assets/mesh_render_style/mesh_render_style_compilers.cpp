#include <engine/assets/mesh_render_style/mesh_render_style_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>
#include <span>

namespace wz::engine::assets::internal
{
    void register_mesh_render_style_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshRenderStyleTable& table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshRenderStyleSchema,
            .output_type = kAssetTypeMeshRenderStyle,
            .compile = [&logger, &table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<MeshRenderStyleCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("mesh render style missing compile desc");
                    return compile_failed_node(input);
                }

                if (!dep_handles.empty()) {
                    logger.error("mesh render style must not have dependencies");
                    return compile_failed_node(input);
                }

                if (!desc->style.valid()) {
                    logger.error("mesh render style is invalid");
                    return compile_failed_node(input);
                }

                const wz::asset::ResourceHandle handle =
                    table.add(desc->style);
                if (!handle.valid()) {
                    logger.error("failed to store mesh render style");
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
