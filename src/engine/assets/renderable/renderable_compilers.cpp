// src/engine/assets/renderable/renderable_compilers.cpp

#include <engine/assets/renderable/renderable_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cmath>
#include <span>
#include <string>

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

        float logit(float value) noexcept
        {
            const float clamped = std::clamp(value, 0.001f, 0.999f);
            return std::log(clamped / (1.0f - clamped));
        }

        float sh_dc_from_linear(float value) noexcept
        {
            constexpr float kSH_C0 = 0.28209479177387814f;
            return (std::clamp(value, 0.0f, 1.0f) - 0.5f) / kSH_C0;
        }

        void expand_splat_bounds(
            GaussianSplatBounds& bounds,
            const float position[3]) noexcept
        {
            if (!bounds.valid) {
                for (int axis = 0; axis < 3; ++axis) {
                    bounds.min[axis] = position[axis];
                    bounds.max[axis] = position[axis];
                }
                bounds.valid = true;
                return;
            }

            for (int axis = 0; axis < 3; ++axis) {
                bounds.min[axis] = std::min(bounds.min[axis], position[axis]);
                bounds.max[axis] = std::max(bounds.max[axis], position[axis]);
            }
        }

        GaussianSplat splat_from_terrain_surfel(
            const TerrainVisualProxySurfel& surfel)
        {
            GaussianSplat splat{};
            for (int axis = 0; axis < 3; ++axis) {
                splat.position[axis] = surfel.position[axis];
                splat.color_dc[axis] = sh_dc_from_linear(surfel.albedo[axis]);
            }

            const float radius = std::max(0.001f, surfel.radius);
            splat.scale[0] = std::log(radius);
            splat.scale[1] = std::log(std::max(0.001f, radius * 0.1f));
            splat.scale[2] = std::log(radius);
            splat.opacity = logit(0.85f);

            // Identity orientation is sufficient for the initial GPU path:
            // the selected far surfels render as stable coverage discs, while
            // normal-oriented splat frames can land with the dedicated shader
            // work that consumes surfel normals/material data directly.
            splat.rotation[0] = 1.0f;
            splat.rotation[1] = 0.0f;
            splat.rotation[2] = 0.0f;
            splat.rotation[3] = 0.0f;
            return splat;
        }

        void append_surfel_density_cloud(
            GaussianSplatCloudData& cloud,
            const TerrainVisualProxyChunkRecord& chunk,
            const TerrainVisualProxySurfelDensityLevel& level)
        {
            if (!level.valid()
                || level.first_surfel + level.surfel_count
                    > chunk.surfels.size())
            {
                return;
            }

            cloud.splats.reserve(cloud.splats.size() + level.surfel_count);
            for (uint32_t i = 0u; i < level.surfel_count; ++i) {
                const TerrainVisualProxySurfel& surfel =
                    chunk.surfels[level.first_surfel + i];
                if (!surfel.valid()) {
                    continue;
                }
                GaussianSplat splat = splat_from_terrain_surfel(surfel);
                expand_splat_bounds(cloud.bounds, splat.position);
                cloud.splats.push_back(std::move(splat));
            }

            cloud.opacity_min = 0.85f;
            cloud.opacity_max = 0.85f;
            cloud.scale_min = cloud.scale_min <= 0.0f
                ? level.representative_radius
                : std::min(cloud.scale_min, level.representative_radius);
            cloud.scale_max =
                std::max(cloud.scale_max, level.representative_radius);
        }

        std::vector<TerrainLodId> terrain_surfel_density_ids(
            const TerrainVisualProxyData& proxy)
        {
            std::vector<TerrainLodId> density_ids;
            for (const TerrainVisualProxyChunkRecord& chunk : proxy.chunks) {
                for (const TerrainVisualProxySurfelDensityLevel& level :
                     chunk.surfel_density_levels)
                {
                    if (!level.valid()) {
                        continue;
                    }
                    const auto existing = std::find_if(
                        density_ids.begin(),
                        density_ids.end(),
                        [&](TerrainLodId id)
                        {
                            return id == level.density_id;
                        });
                    if (existing == density_ids.end()) {
                        density_ids.push_back(level.density_id);
                    }
                }
            }
            std::sort(
                density_ids.begin(),
                density_ids.end(),
                [](TerrainLodId a, TerrainLodId b)
                {
                    return a.value < b.value;
                });
            return density_ids;
        }

        std::vector<GaussianSplatCloudData> make_terrain_far_splat_clouds(
            const TerrainVisualProxyData& proxy)
        {
            std::vector<GaussianSplatCloudData> clouds;
            const std::vector<TerrainLodId> density_ids =
                terrain_surfel_density_ids(proxy);
            clouds.reserve(density_ids.size());
            for (TerrainLodId density_id : density_ids) {
                GaussianSplatCloudData cloud{};
                for (const TerrainVisualProxyChunkRecord& chunk :
                     proxy.chunks)
                {
                    for (const TerrainVisualProxySurfelDensityLevel& level :
                         chunk.surfel_density_levels)
                    {
                        if (level.density_id == density_id) {
                            append_surfel_density_cloud(cloud, chunk, level);
                            break;
                        }
                    }
                }
                if (cloud.valid()) {
                    clouds.push_back(std::move(cloud));
                }
            }
            return clouds;
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
        auto* terrain_visual_proxy_table = &ctx.terrain_visual_proxy_table;
        auto* scalar_fields_table = &ctx.scalar_fields_table;
        auto* mesh_derived_field_table = &ctx.mesh_derived_field_table;
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

                const auto bounds_started = std::chrono::steady_clock::now();
                copy_mesh_bounds(data.bounds_min, data.bounds_max, *mesh);
                const auto bounds_elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - bounds_started)
                        .count();
                if (bounds_elapsed >= 25) {
                    logger->info(
                        "asset compile: mesh wireframe renderable bounds vertices="
                        + std::to_string(mesh->vertices.size())
                        + " ms="
                        + std::to_string(bounds_elapsed));
                }

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
                         mesh_derived_field_table,
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

                if (dep_handles.size() < 2 || dep_handles.size() > 4) {
                    logger->error(
                        "mesh styled renderable requires mesh and style dependencies, with optional mesh field and render program dependencies");
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

                const bool wants_field_visualization =
                    style->field_visualization.enabled;

                if (wants_field_visualization) {
                    if (desc->mesh_field_visualization_asset
                        == wz::asset::AssetKey{})
                    {
                        logger->error(
                            "mesh styled renderable field visualization has no field asset");
                        return compile_failed_node(input);
                    }
                    if (dep_handles.size() < 3) {
                        logger->error(
                            "mesh styled renderable field visualization requires mesh field dependency");
                        return compile_failed_node(input);
                    }

                    const MeshDerivedFieldData* field =
                        mesh_derived_field_table->get(dep_handles[2]);
                    if (!field || !field->valid()) {
                        logger->error(
                            "mesh styled renderable field visualization data is invalid");
                        return compile_failed_node(input);
                    }
                    if (field->source_mesh_key != desc->mesh_asset) {
                        logger->error(
                            "mesh styled renderable field visualization source mesh mismatch");
                        return compile_failed_node(input);
                    }
                    if (field->domain != MeshDerivedFieldDomain::Vertex) {
                        logger->error(
                            "mesh styled renderable field visualization currently requires vertex-domain field data");
                        return compile_failed_node(input);
                    }
                    if (field->element_count != mesh->vertex_count()) {
                        logger->error(
                            "mesh styled renderable field visualization vertex count mismatch");
                        return compile_failed_node(input);
                    }

                    const auto channel_found = std::find_if(
                        field->channels.begin(),
                        field->channels.end(),
                        [&](const MeshDerivedFieldChannel& channel)
                        {
                            return channel.channel_id
                                == style->field_visualization.channel_id;
                        });

                    if (channel_found == field->channels.end()) {
                        logger->error(
                            "mesh styled renderable field visualization channel not found");
                        return compile_failed_node(input);
                    }
                    if (channel_found->value_type
                        != MeshDerivedFieldValueType::Float1)
                    {
                        logger->error(
                            "mesh styled renderable field visualization currently requires Float1 channel data");
                        return compile_failed_node(input);
                    }
                    const uint32_t expected_bytes =
                        field->element_count
                        * mesh_derived_field_value_stride(
                            channel_found->value_type);
                    if (channel_found->byte_count != expected_bytes) {
                        logger->error(
                            "mesh styled renderable field visualization channel byte count mismatch");
                        return compile_failed_node(input);
                    }
                }
                else if (!(desc->mesh_field_visualization_asset
                         == wz::asset::AssetKey{}))
                {
                    logger->error(
                        "mesh styled renderable has field asset but style field visualization is disabled");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::Mesh;
                data.source_asset = desc->mesh_asset;
                data.companion_asset = desc->style_asset;
                data.mesh_field_visualization_asset =
                    desc->mesh_field_visualization_asset;
                data.program = BuiltinRenderProgram::MeshWireframeDebug;
                data.domain = RenderDomain::Opaque;
                data.policy_flags = RenderPolicy_Wireframe;
                if (!(desc->render_program_asset == wz::asset::AssetKey{})) {
                    const size_t program_dep_index =
                        desc->mesh_field_visualization_asset
                            == wz::asset::AssetKey{}
                            ? 2u
                            : 3u;
                    if (dep_handles.size() <= program_dep_index) {
                        logger->error(
                            "mesh styled renderable render program dependency is missing");
                        return compile_failed_node(input);
                    }
                    data.render_program = dep_handles[program_dep_index];
                    if (!data.render_program.valid()) {
                        logger->error(
                            "mesh styled renderable render program dependency is invalid");
                        return compile_failed_node(input);
                    }
                }
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
                        data.program = wants_field_visualization
                            ? BuiltinRenderProgram::MeshFieldHeatmap
                            : transparent
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
                    data.program = wants_field_visualization
                        ? BuiltinRenderProgram::MeshFieldHeatmap
                        : transparent
                        ? BuiltinRenderProgram::MeshWireframeAlpha
                        : BuiltinRenderProgram::MeshWireframeDepthDebug;
                    data.domain = wants_field_visualization
                        ? RenderDomain::Opaque
                        : transparent
                        ? RenderDomain::Transparent
                        : RenderDomain::Opaque;
                    data.policy_flags = wants_field_visualization
                        ? RenderPolicy_None
                        : transparent
                        ? RenderPolicy_Wireframe | RenderPolicy_AlphaBlend
                        : RenderPolicy_Wireframe;
                    apply_depth_policy();
                }
                data.mesh_style = effective_style;
                const auto bounds_started = std::chrono::steady_clock::now();
                copy_mesh_bounds(data.bounds_min, data.bounds_max, *mesh);
                const auto bounds_elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - bounds_started)
                        .count();
                if (bounds_elapsed >= 25) {
                    logger->info(
                        "asset compile: mesh styled renderable bounds vertices="
                        + std::to_string(mesh->vertices.size())
                        + " ms="
                        + std::to_string(bounds_elapsed));
                }

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
            .compile = [logger, terrain_table, terrain_visual_proxy_table,
                        renderable_table](
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

                if (dep_handles.size() != 2) {
                    logger->error(
                        "terrain surface renderable requires terrain and visual proxy dependencies");
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

                if (desc->visual_proxy_asset == wz::asset::AssetKey{}) {
                    logger->error("terrain surface renderable has no visual proxy");
                    return compile_failed_node(input);
                }

                const TerrainVisualProxyData* visual_proxy =
                    terrain_visual_proxy_table->get(dep_handles[1]);
                if (!visual_proxy || !visual_proxy->valid()) {
                    logger->error("terrain surface renderable visual proxy is invalid");
                    return compile_failed_node(input);
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::Mesh;
                data.source_asset = terrain->mesh;
                data.companion_asset = desc->visual_proxy_asset;
                data.program = desc->mesh_program;
                data.domain = desc->domain;
                data.policy_flags = desc->mesh_policy_flags;
                data.terrain_lighting = desc->lighting;
                data.terrain_target_pixels_per_triangle =
                    (std::max)(0.0f, desc->target_pixels_per_triangle);
                data.terrain_far_splat_chunks =
                    make_terrain_far_splat_clouds(*visual_proxy);

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
