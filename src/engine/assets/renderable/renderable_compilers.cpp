// src/engine/assets/renderable/renderable_compilers.cpp

#include <engine/assets/renderable/renderable_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
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

        void copy_mesh_bounds(
            float dst_min[3],
            float dst_max[3],
            const MeshData& mesh)
        {
            for (int axis = 0; axis < 3; ++axis) {
                dst_min[axis] = mesh.vertices[0].position[axis];
                dst_max[axis] = mesh.vertices[0].position[axis];
            }

            for (const auto& vertex : mesh.vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    dst_min[axis] =
                        std::min(dst_min[axis], vertex.position[axis]);
                    dst_max[axis] =
                        std::max(dst_max[axis], vertex.position[axis]);
                }
            }
        }
    }

    void register_renderable_compilers(
        wz::asset::CompilerRegistry& registry,
        const EngineAssetContext& ctx)
    {
        auto* logger = &ctx.logger;
        auto* mesh_table = &ctx.mesh_table;
        auto* mesh_render_style_table = &ctx.mesh_render_style_table;
        auto* terrain_table = &ctx.terrain_table;
        auto* scalar_fields_table = &ctx.scalar_fields_table;
        auto* gaussian_splat_cloud_table = &ctx.gaussian_splat_cloud_table;
        auto* renderable_table = &ctx.renderable_table;

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshWireframeRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .compile = [logger, mesh_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<MeshWireframeRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    logger->error("mesh wireframe renderable missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger->error("mesh wireframe renderable requires one mesh dependency");
                    return compile_failed_node(input);
                }

                const MeshData* mesh = mesh_table->get(dep_handles[0]);

                if (!mesh || !mesh->valid()) {
                    logger->error("mesh wireframe renderable source mesh is invalid");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::Mesh;
                data.source_asset = desc->mesh_asset;
                data.program = desc->program;
                data.domain = desc->domain;
                data.policy_flags = desc->policy_flags;

                copy_mesh_bounds(data.bounds_min, data.bounds_max, *mesh);

                wz::asset::ResourceHandle handle =
                    renderable_table->add(std::move(data));

                if (!handle.valid()) {
                    logger->error("failed to store mesh wireframe renderable");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainDebugRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .compile = [logger, terrain_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<TerrainDebugRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    logger->error("terrain debug renderable missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger->error("terrain debug renderable requires one terrain dependency");
                    return compile_failed_node(input);
                }

                const TerrainAssetData* terrain =
                    terrain_table->get(dep_handles[0]);

                if (!terrain || !terrain->valid()) {
                    logger->error("terrain debug renderable source terrain is invalid");
                    return compile_failed_node(input);
                }

                if (!terrain->supports_render_mesh
                    || terrain->render_mode == TerrainRenderMode::None)
                {
                    logger->error("terrain debug renderable source terrain is not renderable");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.companion_asset = desc->terrain_asset;
                copy_bounds(
                    data.bounds_min,
                    data.bounds_max,
                    terrain->bounds_min,
                    terrain->bounds_max);

                switch (terrain->representation)
                {
                case TerrainRepresentationKind::MeshSurface:
                    if (terrain->mesh == wz::asset::AssetKey{}) {
                        logger->error("terrain debug renderable mesh terrain has no mesh");
                        return compile_failed_node(input);
                    }
                    data.kind = RenderableKind::Mesh;
                    data.source_asset = terrain->mesh;
                    data.program = desc->mesh_program;
                    data.domain = desc->domain;
                    data.policy_flags = desc->mesh_policy_flags;
                    break;

                case TerrainRepresentationKind::HeightField:
                    if (terrain->height_field == wz::asset::AssetKey{}) {
                        logger->error("terrain debug renderable heightfield terrain has no height field");
                        return compile_failed_node(input);
                    }
                    data.kind = RenderableKind::ScalarField;
                    data.source_asset = terrain->height_field;
                    data.program = desc->mesh_program;
                    data.domain = desc->domain;
                    data.policy_flags = desc->mesh_policy_flags;
                    break;
                }

                wz::asset::ResourceHandle handle =
                    renderable_table->add(std::move(data));

                if (!handle.valid()) {
                    logger->error("failed to store terrain debug renderable");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshStyledRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .compile = [logger,
                         mesh_table,
                         mesh_render_style_table,
                         renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<MeshStyledRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    logger->error("mesh styled renderable missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 2) {
                    logger->error(
                        "mesh styled renderable requires mesh and style dependencies");
                    return compile_failed_node(input);
                }

                const MeshData* mesh = mesh_table->get(dep_handles[0]);
                if (!mesh || !mesh->valid()) {
                    logger->error("mesh styled renderable source mesh is invalid");
                    return compile_failed_node(input);
                }

                const MeshRenderStyleData* style =
                    mesh_render_style_table->get(dep_handles[1]);
                if (!style || !style->valid()) {
                    logger->error("mesh styled renderable style is invalid");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::Mesh;
                data.source_asset = desc->mesh_asset;
                data.companion_asset = desc->style_asset;
                data.program = BuiltinRenderProgram::MeshWireframeDebug;
                data.domain = RenderDomain::Opaque;
                data.policy_flags = RenderPolicy_Wireframe;
                MeshRenderStyleData effective_style = *style;
                const bool transparent = is_mesh_render_style_transparent(*style);
                if (!transparent) {
                    effective_style.alpha = 1.0f;
                }
                effective_style.hidden_line_prepass = false;

                if (!style->wireframe.enabled && !style->surface.enabled) {
                    logger->warn(
                        "mesh styled renderable has no enabled render layers");
                    return compile_failed_node(input);
                }

                auto apply_depth_policy = [&]()
                {
                    if (effective_style.depth_test) {
                        data.policy_flags |= RenderPolicy_DepthTest;
                    }
                    if (effective_style.depth_write) {
                        data.policy_flags |= RenderPolicy_DepthWrite;
                    }
                };

                if (style->surface.enabled) {
                    if (!mesh->has_normals) {
                        logger->warn(
                            "mesh surface layer requires normals; falling back to wireframe layer");
                        if (!effective_style.wireframe.enabled) {
                            effective_style.wireframe.enabled = true;
                            for (int i = 0; i < 4; ++i) {
                                effective_style.wireframe.color[i] =
                                    style->surface.color[i];
                            }
                            effective_style.wireframe.emissive_strength =
                                style->surface.emissive_strength;
                        }
                        effective_style.surface.enabled = false;
                    }
                    else {
                        if (!style->double_sided) {
                            logger->warn(
                                "mesh surface renderables are currently two-sided; treating style as double-sided");
                            effective_style.double_sided = true;
                        }
                        data.program = transparent
                            ? BuiltinRenderProgram::MeshSurfaceAlpha
                            : BuiltinRenderProgram::MeshSurface;
                        data.domain = transparent
                            ? RenderDomain::Transparent
                            : RenderDomain::Opaque;
                        data.policy_flags = transparent
                            ? RenderPolicy_AlphaBlend
                            : RenderPolicy_None;
                        apply_depth_policy();
                    }
                }

                if (data.program != BuiltinRenderProgram::MeshSurface
                    && data.program != BuiltinRenderProgram::MeshSurfaceAlpha)
                {
                    data.program = transparent
                        ? BuiltinRenderProgram::MeshWireframeAlpha
                        : BuiltinRenderProgram::MeshWireframeDepthDebug;
                    data.domain = transparent
                        ? RenderDomain::Transparent
                        : RenderDomain::Opaque;
                    data.policy_flags = transparent
                        ? RenderPolicy_Wireframe | RenderPolicy_AlphaBlend
                        : RenderPolicy_Wireframe;
                    apply_depth_policy();
                }
                data.mesh_style = effective_style;
                copy_mesh_bounds(data.bounds_min, data.bounds_max, *mesh);

                wz::asset::ResourceHandle handle =
                    renderable_table->add(std::move(data));

                if (!handle.valid()) {
                    logger->error("failed to store mesh styled renderable");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kTerrainSurfaceRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .compile = [logger, terrain_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<TerrainSurfaceRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    logger->error("terrain surface renderable missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger->error("terrain surface renderable requires one terrain dependency");
                    return compile_failed_node(input);
                }

                const TerrainAssetData* terrain =
                    terrain_table->get(dep_handles[0]);

                if (!terrain || !terrain->valid()) {
                    logger->error("terrain surface renderable source terrain is invalid");
                    return compile_failed_node(input);
                }

                if (terrain->representation
                    != TerrainRepresentationKind::MeshSurface)
                {
                    logger->error("terrain surface renderable currently requires mesh terrain");
                    return compile_failed_node(input);
                }

                if (!terrain->supports_render_mesh
                    || terrain->render_mode == TerrainRenderMode::None)
                {
                    logger->error("terrain surface renderable source terrain is not renderable");
                    return compile_failed_node(input);
                }

                if (terrain->mesh == wz::asset::AssetKey{}) {
                    logger->error("terrain surface renderable mesh terrain has no mesh");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::Mesh;
                data.source_asset = terrain->mesh;
                data.companion_asset = desc->terrain_asset;
                data.program = desc->mesh_program;
                data.domain = desc->domain;
                data.policy_flags = desc->mesh_policy_flags;
                data.terrain_lighting = desc->lighting;

                copy_bounds(
                    data.bounds_min,
                    data.bounds_max,
                    terrain->bounds_min,
                    terrain->bounds_max);

                wz::asset::ResourceHandle handle =
                    renderable_table->add(std::move(data));

                if (!handle.valid()) {
                    logger->error("failed to store terrain surface renderable");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldDebugRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .compile = [logger, scalar_fields_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<ScalarFieldDebugRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    logger->error("scalar field debug renderable missing compile desc");
                    return compile_failed_node(input);
                }

                if (dep_handles.size() != 1) {
                    logger->error("scalar field debug renderable requires one scalar field dependency");
                    return compile_failed_node(input);
                }

                const ScalarFieldData* field =
                    scalar_fields_table->get(dep_handles[0]);

                if (!field || !field->valid()) {
                    logger->error("scalar field debug renderable source field is invalid");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::ScalarField;
                data.source_asset = desc->scalar_field_asset;
                data.program = BuiltinRenderProgram::ScalarFieldDebug;
                data.domain = RenderDomain::Debug;
                data.policy_flags = RenderPolicy_None;

                wz::asset::ResourceHandle handle =
                    renderable_table->add(std::move(data));

                if (!handle.valid()) {
                    logger->error("failed to store scalar field debug renderable");
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
            .compile = [logger, gaussian_splat_cloud_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                const auto* desc =
                    std::any_cast<GaussianSplatDebugRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    logger->error("gaussian splat debug renderable missing compile desc");
                    return compile_failed_node(input);
                }

                // Accept 1 (cloud only) or 2 (cloud + optional LOD) deps.
                // dep_handles[0] is always the cloud; dep_handles[1], if
                // present, is the derived color-LOD asset.
                if (dep_handles.size() < 1 || dep_handles.size() > 2) {
                    logger->error(
                        "gaussian splat debug renderable requires 1 or 2 dependencies "
                        "(cloud, optionally + color LOD)");
                    return compile_failed_node(input);
                }

                const GaussianSplatCloudData* cloud =
                    gaussian_splat_cloud_table->get(dep_handles[0]);

                if (!cloud || !cloud->valid()) {
                    logger->error("gaussian splat debug renderable source cloud is invalid");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::GaussianSplatCloud;
                data.source_asset = desc->splat_cloud_asset;
                data.companion_asset = desc->color_lod_asset;
                data.program = BuiltinRenderProgram::GaussianSplatDebug;
                data.domain = RenderDomain::Splat;
                data.policy_flags = RenderPolicy_AlphaBlend;

                copy_bounds(
                    data.bounds_min,
                    data.bounds_max,
                    cloud->bounds.min,
                    cloud->bounds.max);

                wz::asset::ResourceHandle handle =
                    renderable_table->add(std::move(data));

                if (!handle.valid()) {
                    logger->error("failed to store gaussian splat debug renderable");
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
