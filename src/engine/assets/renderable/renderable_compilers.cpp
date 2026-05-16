// src/engine/assets/renderable/renderable_compilers.cpp

#include <engine/assets/renderable/renderable_compilers.h>

#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <any>
#include <span>

namespace wz::engine::assets::internal
{
    namespace
    {
        void copy_bounds(
            float dst_min[3],
            float dst_max[3],
            const float src_min[3],
            const float src_max[3])
        {
            for (int i = 0; i < 3; ++i) {
                dst_min[i] = src_min[i];
                dst_max[i] = src_max[i];
            }
        }
    }

    void register_renderable_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        GaussianSplatCloudTable& gaussian_splat_cloud_table,
        RenderableAssetTable& renderable_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshWireframeRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .compile = [&logger, &mesh_table, &renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<MeshWireframeRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("mesh wireframe renderable missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger.error("mesh wireframe renderable requires one mesh dependency");
                    return compile_failed_node(input);
                }

                const MeshData* mesh = mesh_table.get(dep_handles[0]);

                if (!mesh || !mesh->valid()) {
                    logger.error("mesh wireframe renderable source mesh is invalid");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::Mesh;
                data.source_asset = desc->mesh_asset;
                data.program = BuiltinRenderProgram::MeshWireframeDebug;
                data.domain = RenderDomain::Debug;
                data.policy_flags = RenderPolicy_Wireframe;

                // If MeshData already has bounds, use them here.
                // Placeholder conservative bounds for v0 if mesh bounds are not yet stored.
                data.bounds_min[0] = -1.0f;
                data.bounds_min[1] = -1.0f;
                data.bounds_min[2] = -1.0f;
                data.bounds_max[0] = 1.0f;
                data.bounds_max[1] = 1.0f;
                data.bounds_max[2] = 1.0f;

                wz::asset::ResourceHandle handle =
                    renderable_table.add(std::move(data));

                if (!handle.valid()) {
                    logger.error("failed to store mesh wireframe renderable");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGaussianSplatDebugRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .compile = [&logger, &gaussian_splat_cloud_table, &renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<GaussianSplatDebugRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("gaussian splat debug renderable missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger.error("gaussian splat debug renderable requires one splat cloud dependency");
                    return compile_failed_node(input);
                }

                const GaussianSplatCloudData* cloud =
                    gaussian_splat_cloud_table.get(dep_handles[0]);

                if (!cloud || !cloud->valid()) {
                    logger.error("gaussian splat debug renderable source cloud is invalid");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::GaussianSplatCloud;
                data.source_asset = desc->splat_cloud_asset;
                data.program = BuiltinRenderProgram::GaussianSplatDebug;
                data.domain = RenderDomain::Splat;
                data.policy_flags = RenderPolicy_AlphaBlend;

                copy_bounds(
                    data.bounds_min,
                    data.bounds_max,
                    cloud->bounds_min,
                    cloud->bounds_max);

                wz::asset::ResourceHandle handle =
                    renderable_table.add(std::move(data));

                if (!handle.valid()) {
                    logger.error("failed to store gaussian splat debug renderable");
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