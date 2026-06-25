// src/engine/assets/renderable/renderable_compilers.cpp

#include <engine/assets/renderable/renderable_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <array>
#include <any>
#include <chrono>
#include <cmath>
#include <span>
#include <string>
#include <string_view>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr std::array<std::string_view, 16>
            kBuiltinRenderProgramOptions = {
                "Mesh wireframe debug",
                "Mesh wireframe depth debug",
                "Mesh depth prepass debug",
                "Mesh wireframe alpha",
                "Mesh surface",
                "Mesh surface alpha",
                "Mesh field heatmap",
                "Mesh mask style",
                "Terrain mesh surface",
                "Gaussian splat debug",
                "Terrain surfel surface",
                "Scalar field debug",
                "Gaussian splat pull debug",
                "Gaussian splat neighborhood color blend",
                "Gaussian splat terrain coverage debug",
                "Sky surface",
            };

        constexpr std::array<std::string_view, 5> kRenderDomainOptions = {
            "Debug",
            "Sky",
            "Opaque",
            "Transparent",
            "Splat",
        };

        constexpr std::array<std::string_view, 2>
            kTerrainLightingModeOptions = {
                "Scene lights",
                "HDRI environment",
            };

        template<class Enum, std::size_t Count>
        Enum enum_param(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            Enum fallback,
            const std::array<std::string_view, Count>&)
        {
            const int64_t value =
                params.get<int64_t>(name, static_cast<int64_t>(fallback));
            if (value >= 0 && value < static_cast<int64_t>(Count)) {
                return static_cast<Enum>(value);
            }
            return fallback;
        }

        wz::asset::AssetKey dep_key(
            std::span<const wz::asset::AssetNode> dep_nodes,
            size_t index)
        {
            if (index < dep_nodes.size()) {
                return dep_nodes[index].key;
            }
            return {};
        }

        void assign_float3(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            float values[3])
        {
            const auto xyz =
                params.get<std::array<float, 3>>(
                    name,
                    { values[0], values[1], values[2] });
            values[0] = xyz[0];
            values[1] = xyz[1];
            values[2] = xyz[2];
        }

        MeshWireframeRenderableCompileDesc
        mesh_wireframe_renderable_desc_from_editor(
            const wz::asset::ParamBlock* params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshWireframeRenderableCompileDesc desc{};
            desc.mesh_asset = dep_key(dep_nodes, 0);
            if (params) {
                desc.program =
                    enum_param(
                        *params,
                        "program",
                        desc.program,
                        kBuiltinRenderProgramOptions);
                desc.domain =
                    enum_param(
                        *params,
                        "domain",
                        desc.domain,
                        kRenderDomainOptions);
                desc.policy_flags =
                    params->get<uint32_t>(
                        "policy_flags",
                        desc.policy_flags);
            }
            return desc;
        }

        MeshStyledRenderableCompileDesc mesh_styled_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshStyledRenderableCompileDesc desc{};
            desc.mesh_asset = dep_key(dep_nodes, 0);
            desc.style_asset = dep_key(dep_nodes, 1);
            for (size_t i = 2u; i < dep_nodes.size(); ++i) {
                if (dep_nodes[i].type == kAssetTypeMeshDerivedField
                    && desc.mesh_field_visualization_asset
                        == wz::asset::AssetKey{})
                {
                    desc.mesh_field_visualization_asset = dep_nodes[i].key;
                }
                else if (dep_nodes[i].type == kAssetTypeRenderProgram
                    && desc.render_program_asset == wz::asset::AssetKey{})
                {
                    desc.render_program_asset = dep_nodes[i].key;
                }
            }
            return desc;
        }

        RhiPullMeshRenderableCompileDesc rhi_pull_mesh_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            RhiPullMeshRenderableCompileDesc desc{};
            desc.mesh_asset = dep_key(dep_nodes, 0);
            desc.render_program_asset = dep_key(dep_nodes, 1);
            return desc;
        }

        GpuSparseMeshRenderableCompileDesc
        gpu_sparse_mesh_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            GpuSparseMeshRenderableCompileDesc desc{};
            desc.sparse_mesh_asset = dep_key(dep_nodes, 0);
            desc.render_program_asset = dep_key(dep_nodes, 1);
            return desc;
        }

        GaussianSplatCloudRhiRenderableCompileDesc
        gaussian_splat_cloud_rhi_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            // Dependency order matches the create API + compiler input ports:
            // splat cloud, render program. The splat size is not recoverable
            // from dependencies; an editor/JSON-authored path supplies it via a
            // ParamBlock, the create-API path via the typed compile desc.
            GaussianSplatCloudRhiRenderableCompileDesc desc{};
            desc.splat_cloud_asset = dep_key(dep_nodes, 0);
            desc.render_program_asset = dep_key(dep_nodes, 1);
            return desc;
        }

        GaussianSplatCloudRenderSettings
        gaussian_splat_cloud_render_settings_from_params(
            const wz::asset::ParamBlock& params)
        {
            GaussianSplatCloudRenderSettings s{};
            s.splat_size = params.get<float>("splat_size", s.splat_size);
            return s;
        }

        ClipmapLandscapeRenderableCompileDesc
        clipmap_landscape_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            // Dependency order matches the create API + compiler input ports:
            // lattice mesh, height scalar field, render program. The
            // world-space settings are not recoverable from dependencies, so
            // an editor-authored path supplies them through the typed compile
            // desc (input.meta) instead of this fallback.
            ClipmapLandscapeRenderableCompileDesc desc{};
            desc.lattice_mesh_asset = dep_key(dep_nodes, 0);
            desc.height_field_asset = dep_key(dep_nodes, 1);
            desc.render_program_asset = dep_key(dep_nodes, 2);
            return desc;
        }

        // Graph/editor authoring carries the clipmap's world-space settings as a
        // ParamBlock (the create-API path supplies them through the typed
        // ClipmapLandscapeRenderableCompileDesc instead). Read them here so a
        // JSON-authored clipmap landscape renders at the authored world scale
        // rather than the 1x1 defaults.
        ClipmapLandscapeRenderSettings
        clipmap_landscape_render_settings_from_params(
            const wz::asset::ParamBlock& params)
        {
            ClipmapLandscapeRenderSettings s{};
            s.world_origin[0] =
                params.get<float>("world_origin_x", s.world_origin[0]);
            s.world_origin[1] =
                params.get<float>("world_origin_z", s.world_origin[1]);
            s.world_size[0] = params.get<float>("world_size_x", s.world_size[0]);
            s.world_size[1] = params.get<float>("world_size_z", s.world_size[1]);
            s.vertical_scale =
                params.get<float>("vertical_scale", s.vertical_scale);
            s.base_height = params.get<float>("base_height", s.base_height);
            s.lattice_world_cell_size = params.get<float>(
                "lattice_world_cell_size", s.lattice_world_cell_size);
            s.view_snapped =
                params.get<bool>("view_snapped", s.view_snapped);
            return s;
        }

        GaussianSplatDebugRenderableCompileDesc
        gaussian_splat_debug_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            GaussianSplatDebugRenderableCompileDesc desc{};
            desc.splat_cloud_asset = dep_key(dep_nodes, 0);
            for (size_t i = 1u; i < dep_nodes.size(); ++i) {
                if (dep_nodes[i].type == kAssetTypeGaussianSplatColorLOD) {
                    desc.color_lod_asset = dep_nodes[i].key;
                    break;
                }
            }
            return desc;
        }

        ScalarFieldDebugRenderableCompileDesc
        scalar_field_debug_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            ScalarFieldDebugRenderableCompileDesc desc{};
            desc.scalar_field_asset = dep_key(dep_nodes, 0);
            return desc;
        }

        TerrainDebugRenderableCompileDesc
        terrain_debug_renderable_desc_from_editor(
            const wz::asset::ParamBlock* params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            TerrainDebugRenderableCompileDesc desc{};
            desc.terrain_asset = dep_key(dep_nodes, 0);
            if (params) {
                desc.mesh_program =
                    enum_param(
                        *params,
                        "mesh_program",
                        desc.mesh_program,
                        kBuiltinRenderProgramOptions);
                desc.domain =
                    enum_param(
                        *params,
                        "domain",
                        desc.domain,
                        kRenderDomainOptions);
                desc.mesh_policy_flags =
                    params->get<uint32_t>(
                        "mesh_policy_flags",
                        desc.mesh_policy_flags);
            }
            return desc;
        }

        TerrainSurfaceRenderableCompileDesc
        terrain_surface_renderable_desc_from_editor(
            const wz::asset::ParamBlock* params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            TerrainSurfaceRenderableCompileDesc desc{};
            desc.terrain_asset = dep_key(dep_nodes, 0);
            desc.visual_proxy_asset = dep_key(dep_nodes, 1);
            if (params) {
                desc.mesh_program =
                    enum_param(
                        *params,
                        "mesh_program",
                        desc.mesh_program,
                        kBuiltinRenderProgramOptions);
                desc.domain =
                    enum_param(
                        *params,
                        "domain",
                        desc.domain,
                        kRenderDomainOptions);
                desc.mesh_policy_flags =
                    params->get<uint32_t>(
                        "mesh_policy_flags",
                        desc.mesh_policy_flags);
                desc.lighting.mode =
                    enum_param(
                        *params,
                        "lighting_mode",
                        desc.lighting.mode,
                        kTerrainLightingModeOptions);
                assign_float3(
                    *params,
                    "environment_color",
                    desc.lighting.environment_color);
                desc.lighting.environment_intensity =
                    params->get<float>(
                        "environment_intensity",
                        desc.lighting.environment_intensity);
                assign_float3(
                    *params,
                    "dominant_light_direction",
                    desc.lighting.dominant_light_direction);
                assign_float3(
                    *params,
                    "dominant_light_color",
                    desc.lighting.dominant_light_color);
                desc.lighting.dominant_light_intensity =
                    params->get<float>(
                        "dominant_light_intensity",
                        desc.lighting.dominant_light_intensity);
                desc.lighting.sky_visibility_strength =
                    params->get<float>(
                        "sky_visibility_strength",
                        desc.lighting.sky_visibility_strength);
                desc.lighting.normal_lighting_strength =
                    params->get<float>(
                        "normal_lighting_strength",
                        desc.lighting.normal_lighting_strength);
                desc.lighting.terrain_bounce_strength =
                    params->get<float>(
                        "terrain_bounce_strength",
                        desc.lighting.terrain_bounce_strength);
                desc.target_pixels_per_triangle =
                    params->get<float>(
                        "target_pixels_per_triangle",
                        desc.target_pixels_per_triangle);
            }
            return desc;
        }

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
        auto* rhi_renderable_table = &ctx.rhi_renderable_table;
        auto* render_program_table = &ctx.render_program_table;
        auto* gpu_sparse_mesh_table = &ctx.gpu_sparse_mesh_table;

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshWireframeRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "program",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Program",
                    .default_num = static_cast<double>(
                        BuiltinRenderProgram::MeshWireframeDebug),
                    .options = kBuiltinRenderProgramOptions,
                },
                {
                    .name = "domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num = static_cast<double>(RenderDomain::Debug),
                    .options = kRenderDomainOptions,
                },
                {
                    .name = "policy_flags",
                    .type = wz::asset::ParamType::Int,
                    .label = "Policy flags",
                    .default_num = RenderPolicy_Wireframe,
                },
            },
            .compile = [logger, mesh_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                MeshWireframeRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<MeshWireframeRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta);
                    editor_desc =
                        mesh_wireframe_renderable_desc_from_editor(
                            params,
                            dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() != 1) {
                        logger->error("mesh wireframe renderable missing compile desc");
                        return compile_failed_node(input);
                    }
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
            .input_ports = {
                { "terrain", kAssetTypeTerrain },
            },
            .parameters = {
                {
                    .name = "mesh_program",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Mesh program",
                    .default_num = static_cast<double>(
                        BuiltinRenderProgram::MeshWireframeDepthDebug),
                    .options = kBuiltinRenderProgramOptions,
                },
                {
                    .name = "domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num = static_cast<double>(RenderDomain::Debug),
                    .options = kRenderDomainOptions,
                },
                {
                    .name = "mesh_policy_flags",
                    .type = wz::asset::ParamType::Int,
                    .label = "Mesh policy flags",
                    .default_num = RenderPolicy_Wireframe
                        | RenderPolicy_DepthTest
                        | RenderPolicy_DepthWrite,
                },
            },
            .compile = [logger, terrain_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                TerrainDebugRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<TerrainDebugRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta);
                    editor_desc =
                        terrain_debug_renderable_desc_from_editor(
                            params,
                            dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() != 1) {
                        logger->error("terrain debug renderable missing compile desc");
                        return compile_failed_node(input);
                    }
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
            .input_schema = kRhiPullMeshRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "geometry", kAssetTypeMesh },
                { "program", kAssetTypeRenderProgram },
            },
            .compile = [logger, mesh_table, render_program_table,
                        rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                RhiPullMeshRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<RhiPullMeshRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    editor_desc =
                        rhi_pull_mesh_renderable_desc_from_deps(dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() != 2) {
                        logger->error(
                            "RHI pull mesh renderable missing compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() != 2) {
                    logger->error(
                        "RHI pull mesh renderable requires geometry and program dependencies");
                    return compile_failed_node(input);
                }

                const MeshData* mesh = mesh_table->get(dep_handles[0]);
                if (!mesh || !mesh->valid()) {
                    logger->error(
                        "RHI pull mesh renderable source mesh is invalid");
                    return compile_failed_node(input);
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[1]);
                if (!program) {
                    logger->error(
                        "RHI pull mesh renderable program is invalid");
                    return compile_failed_node(input);
                }

                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .mesh_key = desc->mesh_asset,
                        .program_key = desc->render_program_asset,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store RHI pull mesh renderable recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGpuSparseMeshRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "gpu_sparse_mesh", kAssetTypeGpuSparseMesh },
                { "program", kAssetTypeRenderProgram },
            },
            .compile = [logger, gpu_sparse_mesh_table, render_program_table,
                        rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                GpuSparseMeshRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<GpuSparseMeshRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    editor_desc =
                        gpu_sparse_mesh_renderable_desc_from_deps(dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() != 2) {
                        logger->error(
                            "GPU sparse mesh renderable missing compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() != 2) {
                    logger->error(
                        "GPU sparse mesh renderable requires sparse mesh and program dependencies");
                    return compile_failed_node(input);
                }
                if (dep_nodes.empty()
                    || dep_nodes[0].key != desc->sparse_mesh_asset)
                {
                    logger->error(
                        "GPU sparse mesh renderable source asset key does not match sparse mesh dependency");
                    return compile_failed_node(input);
                }

                const GpuSparseMeshData* sparse_mesh =
                    gpu_sparse_mesh_table->get(dep_handles[0]);
                if (!sparse_mesh || !sparse_mesh->valid()) {
                    logger->error(
                        "GPU sparse mesh renderable source mesh is invalid");
                    return compile_failed_node(input);
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[1]);
                if (!program || !program->valid()) {
                    logger->error(
                        "GPU sparse mesh renderable program is invalid");
                    return compile_failed_node(input);
                }
                if (program->binding_model != RenderBindingModel::MeshVertexPull) {
                    logger->error(
                        "GPU sparse mesh renderable program must use MeshVertexPull");
                    return compile_failed_node(input);
                }

                // The geometry (gpu_sparse_mesh, #190) and program (#192/#193)
                // are already rhi-resident; emit an rhi renderable recipe so the
                // renderer binds them by identity instead of reconstructing the
                // draw from a legacy RenderableAssetData. The resident pull
                // buffers stay owned by the gpu_sparse_mesh asset.
                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .gpu_sparse_mesh_key = desc->sparse_mesh_asset,
                        .program_key = desc->render_program_asset,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store GPU sparse mesh renderable recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGaussianSplatCloudRhiRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "splat_cloud", kAssetTypeGaussianSplatCloud },
                { "program", kAssetTypeRenderProgram },
            },
            .parameters = {
                { .name = "splat_size", .type = wz::asset::ParamType::Float,
                  .label = "Splat size", .default_num = 1.0,
                  .min = 0.0, .max = 100000.0 },
            },
            .compile = [logger, gaussian_splat_cloud_table,
                        render_program_table, rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                GaussianSplatCloudRhiRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<GaussianSplatCloudRhiRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    editor_desc =
                        gaussian_splat_cloud_rhi_renderable_desc_from_deps(
                            dep_nodes);
                    // Graph/editor authoring supplies the splat size via a
                    // ParamBlock; the deps fallback only recovers the two keys.
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        editor_desc.settings =
                            gaussian_splat_cloud_render_settings_from_params(
                                *params);
                    }
                    desc = &editor_desc;

                    if (dep_handles.size() != 2) {
                        logger->error(
                            "gaussian splat cloud RHI renderable missing "
                            "compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() != 2) {
                    logger->error(
                        "gaussian splat cloud RHI renderable requires splat "
                        "cloud and program dependencies");
                    return compile_failed_node(input);
                }

                const GaussianSplatCloudData* cloud =
                    gaussian_splat_cloud_table->get(dep_handles[0]);
                if (!cloud || !cloud->valid()) {
                    logger->error(
                        "gaussian splat cloud RHI renderable source cloud is "
                        "invalid");
                    return compile_failed_node(input);
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[1]);
                if (!program || !program->valid()) {
                    logger->error(
                        "gaussian splat cloud RHI renderable program is "
                        "invalid");
                    return compile_failed_node(input);
                }
                if (program->binding_model != RenderBindingModel::SplatPull) {
                    logger->error(
                        "gaussian splat cloud RHI renderable program must use "
                        "SplatPull");
                    return compile_failed_node(input);
                }

                // The splat cloud is published resident as a decoded splat
                // StructuredBuffer (#208) and the program (#192/#193) is rhi-
                // resident; emit an rhi renderable recipe so the renderer binds
                // the cloud by identity and records the splat draw. The resident
                // buffer stays owned by the splat-cloud asset.
                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .program_key = desc->render_program_asset,
                        .gaussian_splat_cloud_key = desc->splat_cloud_asset,
                        .splat = desc->settings,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store gaussian splat cloud RHI renderable "
                        "recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kClipmapLandscapeRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "lattice", kAssetTypeMesh },
                { "height_field", kAssetTypeScalarField },
                { "program", kAssetTypeRenderProgram },
            },
            .parameters = {
                { .name = "world_size_x", .type = wz::asset::ParamType::Float,
                  .label = "World size X", .default_num = 256.0,
                  .min = 0.0001, .max = 1000000.0 },
                { .name = "world_size_z", .type = wz::asset::ParamType::Float,
                  .label = "World size Z", .default_num = 256.0,
                  .min = 0.0001, .max = 1000000.0 },
                { .name = "world_origin_x", .type = wz::asset::ParamType::Float,
                  .label = "World origin X", .default_num = -128.0,
                  .min = -1000000.0, .max = 1000000.0 },
                { .name = "world_origin_z", .type = wz::asset::ParamType::Float,
                  .label = "World origin Z", .default_num = -128.0,
                  .min = -1000000.0, .max = 1000000.0 },
                { .name = "vertical_scale", .type = wz::asset::ParamType::Float,
                  .label = "Vertical scale", .default_num = 64.0,
                  .min = 0.0, .max = 1000000.0 },
                { .name = "base_height", .type = wz::asset::ParamType::Float,
                  .label = "Base height", .default_num = 0.0,
                  .min = -1000000.0, .max = 1000000.0 },
                { .name = "lattice_world_cell_size",
                  .type = wz::asset::ParamType::Float,
                  .label = "Lattice cell size", .default_num = 1.0,
                  .min = 0.0001, .max = 100000.0 },
                { .name = "view_snapped",
                  .type = wz::asset::ParamType::Bool,
                  .label = "View-snapped lattice", .default_num = 1.0 },
            },
            .compile = [logger, mesh_table, scalar_fields_table,
                        render_program_table, rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                ClipmapLandscapeRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<ClipmapLandscapeRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    editor_desc =
                        clipmap_landscape_renderable_desc_from_deps(dep_nodes);
                    // Graph/editor authoring supplies the world settings via a
                    // ParamBlock; the deps fallback only recovers the three keys.
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        editor_desc.settings =
                            clipmap_landscape_render_settings_from_params(
                                *params);
                    }
                    desc = &editor_desc;

                    if (dep_handles.size() != 3) {
                        logger->error(
                            "clipmap landscape renderable missing compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() != 3) {
                    logger->error(
                        "clipmap landscape renderable requires lattice mesh, "
                        "height field and program dependencies");
                    return compile_failed_node(input);
                }

                const MeshData* lattice = mesh_table->get(dep_handles[0]);
                if (!lattice || !lattice->valid()) {
                    logger->error(
                        "clipmap landscape renderable lattice mesh is invalid");
                    return compile_failed_node(input);
                }

                const ScalarFieldData* height =
                    scalar_fields_table->get(dep_handles[1]);
                if (!height || !height->valid()) {
                    logger->error(
                        "clipmap landscape renderable height field is invalid");
                    return compile_failed_node(input);
                }

                const RenderProgramData* program =
                    render_program_table->get(dep_handles[2]);
                if (!program || !program->valid()) {
                    logger->error(
                        "clipmap landscape renderable program is invalid");
                    return compile_failed_node(input);
                }
                if (program->binding_model
                    != RenderBindingModel::MeshVertexPull)
                {
                    logger->error(
                        "clipmap landscape renderable program must use "
                        "MeshVertexPull");
                    return compile_failed_node(input);
                }

                // The lattice mesh (#198 step 2), height ScalarField (#197
                // R32 texture), and program are already rhi-resident; emit an
                // rhi renderable recipe binding them by identity plus the
                // world-space settings the clipmap shader (slice 3b) packs.
                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .mesh_key = desc->lattice_mesh_asset,
                        .program_key = desc->render_program_asset,
                        .height_texture_key = desc->height_field_asset,
                        .clipmap = desc->settings,
                    });
                if (!handle.valid()) {
                    logger->error(
                        "failed to store clipmap landscape renderable recipe");
                    return compile_failed_node(input);
                }

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            },
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshStyledRenderableSchema,
            .output_type = kAssetTypeRenderable,
            .input_ports = {
                { "mesh", kAssetTypeMesh },
                { "style", kAssetTypeMeshRenderStyle },
                {
                    "mesh_field",
                    kAssetTypeMeshDerivedField,
                    wz::asset::InputPortRequirement::Optional,
                },
                {
                    "render_program",
                    kAssetTypeRenderProgram,
                    wz::asset::InputPortRequirement::Optional,
                },
            },
            .compile = [logger,
                         mesh_table,
                         mesh_render_style_table,
                         mesh_derived_field_table,
                         renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                MeshStyledRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<MeshStyledRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    editor_desc =
                        mesh_styled_renderable_desc_from_deps(dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() < 2 || dep_handles.size() > 4) {
                        logger->error("mesh styled renderable missing compile desc");
                        return compile_failed_node(input);
                    }
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

                MeshRenderStyleData effective_style = *style;
                if (!is_mesh_render_style_drawable(effective_style)) {
                    logger->warn(
                        "mesh styled renderable has no enabled render layers; "
                        "compiling as non-drawing renderable");

                    RenderableAssetData data{};
                    data.kind = RenderableKind::Mesh;
                    data.source_asset = desc->mesh_asset;
                    data.companion_asset = desc->style_asset;
                    data.mesh_field_visualization_asset = {};
                    data.program = BuiltinRenderProgram::MeshWireframeDebug;
                    data.domain = RenderDomain::Opaque;
                    data.policy_flags = RenderPolicy_None;
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

                const bool wants_field_visualization =
                    effective_style.field_visualization.enabled;
                bool field_visualization_active = wants_field_visualization;
                const bool wants_mask = effective_style.mask.enabled;
                bool mask_active = wants_mask;
                const bool allow_custom_face_mask_field =
                    !(desc->render_program_asset == wz::asset::AssetKey{})
                    && effective_style.mask.domain == MeshMaskDomain::Face;

                if (wants_field_visualization) {
                    auto disable_field_visualization =
                        [&](std::string_view reason)
                    {
                        logger->warn(
                            "mesh styled renderable field visualization disabled: "
                            + std::string(reason));
                        field_visualization_active = false;
                        effective_style.field_visualization.enabled = false;
                    };

                    if (desc->mesh_field_visualization_asset
                        == wz::asset::AssetKey{})
                    {
                        disable_field_visualization("no field asset");
                    }
                    else if (dep_handles.size() < 3) {
                        disable_field_visualization(
                            "missing mesh field dependency");
                    }
                    else {
                        const MeshDerivedFieldData* field =
                            mesh_derived_field_table->get(dep_handles[2]);
                        if (!field || !field->valid()) {
                            disable_field_visualization(
                                "field data is invalid");
                        }
                        else if (field->source_mesh_key != desc->mesh_asset) {
                            disable_field_visualization(
                                "field source mesh mismatch");
                        }
                        else if (field->domain
                            != MeshDerivedFieldDomain::Vertex
                            && field->domain != MeshDerivedFieldDomain::Face)
                        {
                            disable_field_visualization(
                                "field is not vertex- or face-domain");
                        }
                        else if (field->domain
                                == MeshDerivedFieldDomain::Vertex
                            && field->element_count != mesh->vertex_count())
                        {
                            disable_field_visualization(
                                "field vertex count mismatch");
                        }
                        else if (field->domain
                                == MeshDerivedFieldDomain::Face
                            && field->element_count
                                != mesh->index_count() / 3u)
                        {
                            disable_field_visualization(
                                "field face count mismatch");
                        }
                        else {
                            const auto channel_found = std::find_if(
                                field->channels.begin(),
                                field->channels.end(),
                                [&](const MeshDerivedFieldChannel& channel)
                                {
                                    return channel.channel_id
                                        == effective_style
                                            .field_visualization.channel_id;
                                });

                            if (channel_found == field->channels.end()) {
                                disable_field_visualization(
                                    "channel not found");
                            }
                            else if (channel_found->value_type
                                    != MeshDerivedFieldValueType::Float1
                                && channel_found->value_type
                                    != MeshDerivedFieldValueType::UInt1)
                            {
                                disable_field_visualization(
                                    "channel is not Float1 or UInt1");
                            }
                            else {
                                const uint32_t expected_bytes =
                                    field->element_count
                                    * mesh_derived_field_value_stride(
                                        channel_found->value_type);
                                if (channel_found->byte_count
                                    != expected_bytes)
                                {
                                    disable_field_visualization(
                                        "channel byte count mismatch");
                                }
                            }
                        }
                    }
                }
                if (wants_mask) {
                    auto disable_mask = [&](std::string_view reason)
                    {
                        logger->warn(
                            "mesh styled renderable mask disabled: "
                            + std::string(reason));
                        mask_active = false;
                        effective_style.mask.enabled = false;
                    };
                    auto disable_mask_or_show_unmatched =
                        [&](std::string_view reason)
                    {
                        if (effective_style.mask.show_unmatched) {
                            logger->warn(
                                "mesh styled renderable mask field unavailable; "
                                "drawing unmatched color: "
                                + std::string(reason));
                            for (MeshMaskRule& rule :
                                 effective_style.mask.rules)
                            {
                                rule.enabled = false;
                            }
                            return;
                        }
                        disable_mask(reason);
                    };

                    if (field_visualization_active) {
                        disable_mask("field visualization already active");
                    }
                    else if (desc->mesh_field_visualization_asset
                        == wz::asset::AssetKey{})
                    {
                        disable_mask_or_show_unmatched("no field asset");
                    }
                    else if (dep_handles.size() < 3) {
                        disable_mask_or_show_unmatched(
                            "missing mesh field dependency");
                    }
                    else if (effective_style.mask.domain
                            != MeshMaskDomain::Face
                        && effective_style.mask.domain
                            != MeshMaskDomain::Vertex)
                    {
                        disable_mask("only face- or vertex-domain masks are supported");
                    }
                    else if (effective_style.mask.projection_mode
                        != MeshMaskProjectionMode::Direct)
                    {
                        disable_mask("only direct masks are supported");
                    }
                    else {
                        const MeshDerivedFieldData* field =
                            mesh_derived_field_table->get(dep_handles[2]);
                        if (!field || !field->valid()) {
                            disable_mask_or_show_unmatched(
                                "field data is invalid");
                        }
                        else if (field->source_mesh_key != desc->mesh_asset
                            && !allow_custom_face_mask_field)
                        {
                            disable_mask_or_show_unmatched(
                                "field source mesh mismatch");
                        }
                        else {
                            const MeshDerivedFieldDomain required_domain =
                                effective_style.mask.domain
                                    == MeshMaskDomain::Vertex
                                ? MeshDerivedFieldDomain::Vertex
                                : MeshDerivedFieldDomain::Face;
                            const uint32_t required_count =
                                required_domain == MeshDerivedFieldDomain::Vertex
                                    ? mesh->vertex_count()
                                    : mesh->index_count() / 3u;
                            if (field->domain != required_domain) {
                                disable_mask_or_show_unmatched(
                                    required_domain
                                            == MeshDerivedFieldDomain::Vertex
                                        ? "field is not vertex-domain"
                                        : "field is not face-domain");
                            }
                            else if (field->element_count != required_count
                                && !(allow_custom_face_mask_field
                                    && required_domain
                                        == MeshDerivedFieldDomain::Face
                                    && field->element_count >= required_count))
                            {
                                disable_mask_or_show_unmatched(
                                    required_domain
                                            == MeshDerivedFieldDomain::Vertex
                                        ? "field vertex count mismatch"
                                        : "field face count mismatch");
                            }
                            else {
                                for (const MeshMaskRule& rule :
                                     effective_style.mask.rules)
                                {
                                    if (!rule.enabled) {
                                        continue;
                                    }
                                    const auto channel_found = std::find_if(
                                        field->channels.begin(),
                                        field->channels.end(),
                                        [&](const MeshDerivedFieldChannel& channel)
                                        {
                                            return channel.channel_id
                                                == rule.input_channel_id;
                                        });

                                    if (channel_found == field->channels.end()) {
                                        disable_mask_or_show_unmatched(
                                            "rule channel not found");
                                        break;
                                    }
                                    if (channel_found->value_type
                                            != MeshDerivedFieldValueType::Float1
                                        && channel_found->value_type
                                            != MeshDerivedFieldValueType::UInt1)
                                    {
                                        disable_mask_or_show_unmatched(
                                            "rule channel is not Float1 or UInt1");
                                        break;
                                    }

                                    const uint32_t expected_bytes =
                                        field->element_count
                                        * mesh_derived_field_value_stride(
                                            channel_found->value_type);
                                    if (channel_found->byte_count
                                        != expected_bytes)
                                    {
                                        disable_mask_or_show_unmatched(
                                            "rule channel byte count mismatch");
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                else if (!wants_field_visualization
                         && !(desc->mesh_field_visualization_asset
                         == wz::asset::AssetKey{}))
                {
                    if (!field_visualization_active) {
                        logger->error(
                            "mesh styled renderable has field asset but style field visualization and mask are disabled");
                        return compile_failed_node(input);
                    }
                }

                RenderableAssetData data{};
                data.kind = RenderableKind::Mesh;
                data.source_asset = desc->mesh_asset;
                data.companion_asset = desc->style_asset;
                data.mesh_field_visualization_asset =
                    field_visualization_active || mask_active
                        ? desc->mesh_field_visualization_asset
                        : wz::asset::AssetKey{};
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
                const bool transparent = is_mesh_render_style_transparent(*style);
                if (!transparent) {
                    effective_style.alpha = 1.0f;
                }
                effective_style.hidden_line_prepass = false;

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
                        data.program = field_visualization_active
                            ? BuiltinRenderProgram::MeshFieldHeatmap
                            : mask_active
                            ? BuiltinRenderProgram::MeshMaskStyle
                            : transparent
                                ? BuiltinRenderProgram::MeshSurfaceAlpha
                                : BuiltinRenderProgram::MeshSurface;
                        data.domain =
                            (field_visualization_active || mask_active)
                            ? RenderDomain::Opaque
                            : transparent
                            ? RenderDomain::Transparent
                            : RenderDomain::Opaque;
                        data.policy_flags =
                            (field_visualization_active || mask_active)
                            ? RenderPolicy_None
                            : transparent
                            ? RenderPolicy_AlphaBlend
                            : RenderPolicy_None;
                        apply_depth_policy();
                    }
                }

                if (data.program != BuiltinRenderProgram::MeshSurface
                    && data.program != BuiltinRenderProgram::MeshSurfaceAlpha)
                {
                    data.program = field_visualization_active
                        ? BuiltinRenderProgram::MeshFieldHeatmap
                        : mask_active
                        ? BuiltinRenderProgram::MeshMaskStyle
                        : transparent
                        ? BuiltinRenderProgram::MeshWireframeAlpha
                        : BuiltinRenderProgram::MeshWireframeDepthDebug;
                    data.domain = field_visualization_active
                        ? RenderDomain::Opaque
                        : mask_active
                        ? RenderDomain::Opaque
                        : transparent
                        ? RenderDomain::Transparent
                        : RenderDomain::Opaque;
                    data.policy_flags = field_visualization_active
                        ? RenderPolicy_None
                        : mask_active
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
            .input_ports = {
                { "terrain", kAssetTypeTerrain },
                { "visual_proxy", kAssetTypeTerrainVisualProxy },
            },
            .parameters = {
                {
                    .name = "mesh_program",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Mesh program",
                    .default_num = static_cast<double>(
                        BuiltinRenderProgram::TerrainMeshSurface),
                    .options = kBuiltinRenderProgramOptions,
                },
                {
                    .name = "domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num = static_cast<double>(RenderDomain::Opaque),
                    .options = kRenderDomainOptions,
                },
                {
                    .name = "mesh_policy_flags",
                    .type = wz::asset::ParamType::Int,
                    .label = "Mesh policy flags",
                    .default_num =
                        RenderPolicy_DepthTest | RenderPolicy_DepthWrite,
                },
                {
                    .name = "lighting_mode",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Lighting mode",
                    .default_num = static_cast<double>(
                        TerrainLightingMode::SceneLights),
                    .options = kTerrainLightingModeOptions,
                },
                {
                    .name = "environment_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Environment color",
                },
                {
                    .name = "environment_intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Environment intensity",
                    .default_num = 0.25,
                    .min = 0.0,
                    .max = 10.0,
                },
                {
                    .name = "dominant_light_direction",
                    .type = wz::asset::ParamType::Float3,
                    .label = "Dominant light direction",
                },
                {
                    .name = "dominant_light_color",
                    .type = wz::asset::ParamType::Color,
                    .label = "Dominant light color",
                },
                {
                    .name = "dominant_light_intensity",
                    .type = wz::asset::ParamType::Float,
                    .label = "Dominant light intensity",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 10.0,
                },
                {
                    .name = "sky_visibility_strength",
                    .type = wz::asset::ParamType::Float,
                    .label = "Sky visibility strength",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "normal_lighting_strength",
                    .type = wz::asset::ParamType::Float,
                    .label = "Normal lighting strength",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 4.0,
                },
                {
                    .name = "terrain_bounce_strength",
                    .type = wz::asset::ParamType::Float,
                    .label = "Terrain bounce strength",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 4.0,
                },
                {
                    .name = "target_pixels_per_triangle",
                    .type = wz::asset::ParamType::Float,
                    .label = "Target pixels per triangle",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 512.0,
                },
            },
            .compile = [logger, terrain_table, terrain_visual_proxy_table,
                        renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                TerrainSurfaceRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<TerrainSurfaceRenderableCompileDesc>(
                        &input.meta);

                if (!desc) {
                    const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta);
                    editor_desc =
                        terrain_surface_renderable_desc_from_editor(
                            params,
                            dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() != 2) {
                        logger->error("terrain surface renderable missing compile desc");
                        return compile_failed_node(input);
                    }
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
            .input_ports = {
                { "scalar_field", kAssetTypeScalarField },
            },
            .compile = [logger, scalar_fields_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                ScalarFieldDebugRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<ScalarFieldDebugRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    editor_desc =
                        scalar_field_debug_renderable_desc_from_deps(
                            dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() != 1) {
                        logger->error("scalar field debug renderable missing compile desc");
                        return compile_failed_node(input);
                    }
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
            .input_ports = {
                { "splat_cloud", kAssetTypeGaussianSplatCloud },
                {
                    "color_lod",
                    kAssetTypeGaussianSplatColorLOD,
                    wz::asset::InputPortRequirement::Optional,
                },
            },
            .compile = [logger, gaussian_splat_cloud_table, renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                GaussianSplatDebugRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<GaussianSplatDebugRenderableCompileDesc>(&input.meta);

                if (!desc) {
                    editor_desc =
                        gaussian_splat_debug_renderable_desc_from_deps(
                            dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() < 1 || dep_handles.size() > 2) {
                        logger->error("gaussian splat debug renderable missing compile desc");
                        return compile_failed_node(input);
                    }
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
