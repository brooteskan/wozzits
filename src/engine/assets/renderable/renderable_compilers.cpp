// src/engine/assets/renderable/renderable_compilers.cpp

#include <engine/assets/renderable/renderable_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <any>
#include <cmath>
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

        float clamp01(float v) noexcept
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        void expand_splat_bounds(
            GaussianSplatCloudData& cloud,
            const GaussianSplat& splat)
        {
            if (!cloud.bounds.valid) {
                for (int axis = 0; axis < 3; ++axis) {
                    cloud.bounds.min[axis] = splat.position[axis];
                    cloud.bounds.max[axis] = splat.position[axis];
                }
                cloud.bounds.valid = true;
            }
            else {
                for (int axis = 0; axis < 3; ++axis) {
                    cloud.bounds.min[axis] =
                        std::min(cloud.bounds.min[axis], splat.position[axis]);
                    cloud.bounds.max[axis] =
                        std::max(cloud.bounds.max[axis], splat.position[axis]);
                }
            }
        }

        float logit(float v) noexcept
        {
            const float x = std::clamp(v, 0.001f, 0.999f);
            return std::log(x / (1.0f - x));
        }

        void triangle_normal(
            const TerrainAssetData& terrain,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic,
            float normal[3]) noexcept
        {
            const float* points = terrain.mesh_surface_points.data();
            const float* a = points + static_cast<size_t>(ia) * 3u;
            const float* b = points + static_cast<size_t>(ib) * 3u;
            const float* c = points + static_cast<size_t>(ic) * 3u;

            const float abx = b[0] - a[0];
            const float aby = b[1] - a[1];
            const float abz = b[2] - a[2];
            const float acx = c[0] - a[0];
            const float acy = c[1] - a[1];
            const float acz = c[2] - a[2];
            float nx = aby * acz - abz * acy;
            float ny = abz * acx - abx * acz;
            float nz = abx * acy - aby * acx;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len <= 1e-6f) {
                normal[0] = 0.0f;
                normal[1] = 1.0f;
                normal[2] = 0.0f;
                return;
            }
            const float inv_len = 1.0f / len;
            normal[0] = nx * inv_len;
            normal[1] = ny * inv_len;
            normal[2] = nz * inv_len;
        }

        void y_axis_to_normal_quat(
            const float normal[3],
            float rotation_wxyz[4]) noexcept
        {
            const float dot_y = std::clamp(normal[1], -1.0f, 1.0f);
            if (dot_y > 0.999f) {
                rotation_wxyz[0] = 1.0f;
                rotation_wxyz[1] = 0.0f;
                rotation_wxyz[2] = 0.0f;
                rotation_wxyz[3] = 0.0f;
                return;
            }
            if (dot_y < -0.999f) {
                rotation_wxyz[0] = 0.0f;
                rotation_wxyz[1] = 1.0f;
                rotation_wxyz[2] = 0.0f;
                rotation_wxyz[3] = 0.0f;
                return;
            }

            const float axis_x = normal[2];
            const float axis_y = 0.0f;
            const float axis_z = -normal[0];
            const float s = std::sqrt((1.0f + dot_y) * 2.0f);
            const float inv_s = 1.0f / s;
            rotation_wxyz[0] = 0.5f * s;
            rotation_wxyz[1] = axis_x * inv_s;
            rotation_wxyz[2] = axis_y * inv_s;
            rotation_wxyz[3] = axis_z * inv_s;
        }

        GaussianSplat make_terrain_far_splat(
            const TerrainAssetData& terrain,
            const TerrainVisualChunk& chunk,
            uint32_t ia,
            uint32_t ib,
            uint32_t ic,
            float tangent_scale)
        {
            constexpr float SH_C0 = 0.28209479177387814f;
            const float* points = terrain.mesh_surface_points.data();
            const float* a = points + static_cast<size_t>(ia) * 3u;
            const float* b = points + static_cast<size_t>(ib) * 3u;
            const float* c = points + static_cast<size_t>(ic) * 3u;

            GaussianSplat out{};
            for (int axis = 0; axis < 3; ++axis) {
                out.position[axis] = (a[axis] + b[axis] + c[axis]) / 3.0f;
            }

            float normal[3]{};
            triangle_normal(terrain, ia, ib, ic, normal);
            y_axis_to_normal_quat(normal, out.rotation);

            const float thickness =
                std::max(0.0005f, tangent_scale * 0.15f);
            out.scale[0] = std::log(std::max(0.0005f, tangent_scale));
            out.scale[1] = std::log(thickness);
            out.scale[2] = std::log(std::max(0.0005f, tangent_scale));
            out.opacity = logit(0.88f);

            const float height_range =
                std::max(terrain.max_height - terrain.min_height, 1e-5f);
            const float height_t =
                clamp01((out.position[1] - terrain.min_height) / height_range);
            const float low[3]{ 0.20f, 0.34f, 0.18f };
            const float high[3]{ 0.54f, 0.50f, 0.36f };
            for (int axis = 0; axis < 3; ++axis) {
                const float base =
                    low[axis] + (high[axis] - low[axis]) * height_t;
                out.color_dc[axis] = (base - 0.5f) / SH_C0;
            }

            (void)chunk;
            return out;
        }

        std::vector<GaussianSplatCloudData> make_terrain_far_splat_chunks(
            const TerrainAssetData& terrain)
        {
            std::vector<GaussianSplatCloudData> clouds;
            if (terrain.mesh_visual_chunks.empty()
                || terrain.mesh_visual_indices.empty()
                || terrain.mesh_surface_points.empty())
            {
                return clouds;
            }

            clouds.reserve(terrain.mesh_visual_chunks.size());
            constexpr uint32_t kMaxSplatsPerChunk = 1024u;

            for (const TerrainVisualChunk& chunk : terrain.mesh_visual_chunks) {
                GaussianSplatCloudData cloud{};
                const uint32_t triangle_count = chunk.triangle_count();
                if (triangle_count == 0) {
                    clouds.push_back(std::move(cloud));
                    continue;
                }

                const uint32_t splat_count =
                    std::min(triangle_count, kMaxSplatsPerChunk);
                const float bounds_x =
                    std::max(chunk.bounds_max[0] - chunk.bounds_min[0], 0.001f);
                const float bounds_z =
                    std::max(chunk.bounds_max[2] - chunk.bounds_min[2], 0.001f);
                const float spacing =
                    std::sqrt(
                        std::max(
                            (bounds_x * bounds_z)
                                / static_cast<float>(splat_count),
                            1e-6f));
                const float tangent_scale = spacing * 0.35f;

                cloud.splats.reserve(splat_count);
                cloud.opacity_min = logit(0.88f);
                cloud.opacity_max = logit(0.88f);
                cloud.scale_min = std::log(std::max(0.0005f, tangent_scale));
                cloud.scale_max = cloud.scale_min;

                for (uint32_t s = 0; s < splat_count; ++s) {
                    const uint32_t tri =
                        static_cast<uint32_t>(
                            (static_cast<uint64_t>(s) * triangle_count)
                            / splat_count);
                    const uint32_t base =
                        chunk.first_index + tri * 3u;
                    if (base + 2u >= terrain.mesh_visual_indices.size()) {
                        continue;
                    }

                    const uint32_t ia = terrain.mesh_visual_indices[base + 0u];
                    const uint32_t ib = terrain.mesh_visual_indices[base + 1u];
                    const uint32_t ic = terrain.mesh_visual_indices[base + 2u];
                    const uint32_t point_count = static_cast<uint32_t>(
                        terrain.mesh_surface_points.size() / 3u);
                    if (ia >= point_count || ib >= point_count || ic >= point_count) {
                        continue;
                    }

                    GaussianSplat splat = make_terrain_far_splat(
                        terrain,
                        chunk,
                        ia,
                        ib,
                        ic,
                        tangent_scale);
                    expand_splat_bounds(cloud, splat);
                    cloud.splats.push_back(std::move(splat));
                }
                clouds.push_back(std::move(cloud));
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
                data.terrain_target_pixels_per_triangle =
                    (std::max)(0.0f, desc->target_pixels_per_triangle);
                data.terrain_far_splat_chunks =
                    make_terrain_far_splat_chunks(*terrain);

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
