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

        // Locate the first dependency of a given asset type. Used by recipes
        // with optional ports (issue #218), where positional indexing is unsafe
        // because dep ordering is not guaranteed once an optional port exists.
        wz::asset::AssetKey dep_key_of_type(
            std::span<const wz::asset::AssetNode> dep_nodes,
            wz::asset::AssetType type)
        {
            for (const auto& dep : dep_nodes) {
                if (dep.type == type) {
                    return dep.key;
                }
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

        RhiPullMeshRenderableCompileDesc rhi_pull_mesh_renderable_desc_from_deps(
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            RhiPullMeshRenderableCompileDesc desc{};
            desc.mesh_asset = dep_key(dep_nodes, 0);
            desc.render_program_asset = dep_key(dep_nodes, 1);
            // Optional style dependency (issue #195 slice A) at index 2; empty
            // when the port is unconnected (dep_key returns a zero key).
            desc.style_asset = dep_key(dep_nodes, 2);
            return desc;
        }

        // Bake the SHADING subset of a MeshRenderStyle into the recipe's style
        // POD (issue #195 slice A). Only colours / emissive / alpha / layer-enable
        // flow — the style's depth/blend/raster properties are program properties
        // now, and its field_visualization + mask (geometry-generating) parts are
        // OUT OF SCOPE: they are intentionally ignored here (the style asset keeps
        // them; the rhi mesh recipe does not consume them).
        MeshRenderStyleShading bake_mesh_render_style_shading(
            const MeshRenderStyleData& style) noexcept
        {
            MeshRenderStyleShading out{};
            out.has_style = true;
            for (int i = 0; i < 4; ++i) {
                out.wireframe_color[i] = style.wireframe.color[i];
                out.surface_color[i] = style.surface.color[i];
            }
            out.wireframe_emissive = style.wireframe.emissive_strength;
            out.surface_emissive = style.surface.emissive_strength;
            out.alpha = style.alpha;
            out.wireframe_enabled = style.wireframe.enabled;
            out.surface_enabled = style.surface.enabled;
            return out;
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
            // Locate dependencies by TYPE, not positional index: the recipe now
            // has an OPTIONAL placement port (issue #218 Phase 2), so dep
            // ordering is not guaranteed. The world-space settings are not
            // recoverable from dependencies, so an editor-authored path supplies
            // them through the typed compile desc (input.meta) instead of this
            // fallback.
            ClipmapLandscapeRenderableCompileDesc desc{};
            desc.lattice_mesh_asset =
                dep_key_of_type(dep_nodes, kAssetTypeMesh);
            desc.height_field_asset =
                dep_key_of_type(dep_nodes, kAssetTypeScalarField);
            desc.render_program_asset =
                dep_key_of_type(dep_nodes, kAssetTypeRenderProgram);
            desc.placement_asset =
                dep_key_of_type(dep_nodes, kAssetTypePlacement);
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
        auto* placement_table = &ctx.placement_table;


        // DEPRECATED (issue #222): the terrain debug renderable (0x703) stands on
        // TerrainAsset, which #222 retires in favour of the shipped clipmap stack
        // (0x708 landscape renderable + a ScalarField height texture + a
        // Placement world frame). It is intentionally NOT ported by the #195
        // scrap-and-rebuild — it dies with #222's TerrainAsset deprecation, not
        // here. Kept alive only so existing terrain content/tests keep resolving.
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
                // Optional MeshRenderStyle (issue #195 slice A). When connected,
                // its SHADING constants are baked into the recipe and flow to the
                // shader as space2 root constants (the program must declare the
                // "mesh_style" root constant, binding_layout preset 4). Absent =
                // the recipe's zero "no style" default, rendered as a plain MVP
                // pull mesh exactly as before.
                {
                    "style",
                    kAssetTypeMeshRenderStyle,
                    wz::asset::InputPortRequirement::Optional,
                },
            },
            .compile = [logger, mesh_table, render_program_table,
                        mesh_render_style_table, rhi_renderable_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                RhiPullMeshRenderableCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<RhiPullMeshRenderableCompileDesc>(
                        &input.meta);

                // Geometry + program are required (indices 0, 1); style is an
                // optional 3rd dep. So 2 or 3 handles are valid.
                if (!desc) {
                    editor_desc =
                        rhi_pull_mesh_renderable_desc_from_deps(dep_nodes);
                    desc = &editor_desc;

                    if (dep_handles.size() < 2 || dep_handles.size() > 3) {
                        logger->error(
                            "RHI pull mesh renderable missing compile desc");
                        return compile_failed_node(input);
                    }
                }

                if (dep_handles.size() < 2 || dep_handles.size() > 3) {
                    logger->error(
                        "RHI pull mesh renderable requires geometry and program "
                        "dependencies, with an optional style dependency");
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

                // Bake the optional style's shading constants. The style dep, when
                // present, is index 2 (matching the input-port order). A recipe
                // with no style dep keeps the default zero "no style" POD.
                MeshRenderStyleShading style_shading{};
                const bool has_style_dep =
                    !(desc->style_asset == wz::asset::AssetKey{});
                if (has_style_dep) {
                    if (dep_handles.size() < 3) {
                        logger->error(
                            "RHI pull mesh renderable style dependency missing");
                        return compile_failed_node(input);
                    }
                    const MeshRenderStyleData* style =
                        mesh_render_style_table->get(dep_handles[2]);
                    if (!style || !style->valid()) {
                        logger->error(
                            "RHI pull mesh renderable style is invalid");
                        return compile_failed_node(input);
                    }
                    style_shading = bake_mesh_render_style_shading(*style);
                }

                const wz::asset::ResourceHandle handle =
                    rhi_renderable_table->add(RhiRenderableRecipe{
                        .mesh_key = desc->mesh_asset,
                        .program_key = desc->render_program_asset,
                        .style = style_shading,
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
                // Optional world-space frame (issue #218 Phase 2). When
                // connected, the placement is authoritative for the texture->
                // world footprint (world_origin/world_size/vertical_scale/
                // base_height), overriding the authored settings. The lattice
                // geometry snap (lattice_world_cell_size, c0) is unaffected.
                {
                    "placement",
                    kAssetTypePlacement,
                    wz::asset::InputPortRequirement::Optional,
                },
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
                        render_program_table, placement_table,
                        rhi_renderable_table](
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
                    // ParamBlock; the deps fallback only recovers the keys.
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        editor_desc.settings =
                            clipmap_landscape_render_settings_from_params(
                                *params);
                    }
                    desc = &editor_desc;
                }

                // Locate dependencies by asset TYPE, not positional index: the
                // recipe now has an OPTIONAL placement port (issue #218 Phase 2),
                // so dep ordering is not guaranteed. dep_nodes and dep_handles
                // are parallel, in DAG order, so scan them together. lattice,
                // height and program are REQUIRED; placement is OPTIONAL.
                const MeshData* lattice = nullptr;
                const ScalarFieldData* height = nullptr;
                const RenderProgramData* program = nullptr;
                const PlacementData* placement = nullptr;
                for (size_t i = 0;
                    i < dep_nodes.size() && i < dep_handles.size();
                    ++i)
                {
                    switch (dep_nodes[i].type) {
                    case kAssetTypeMesh:
                        if (!lattice) {
                            lattice = mesh_table->get(dep_handles[i]);
                        }
                        break;
                    case kAssetTypeScalarField:
                        if (!height) {
                            height = scalar_fields_table->get(dep_handles[i]);
                        }
                        break;
                    case kAssetTypeRenderProgram:
                        if (!program) {
                            program = render_program_table->get(dep_handles[i]);
                        }
                        break;
                    case kAssetTypePlacement:
                        if (!placement) {
                            placement = placement_table->get(dep_handles[i]);
                        }
                        break;
                    default:
                        break;
                    }
                }

                if (!lattice || !lattice->valid()) {
                    logger->error(
                        "clipmap landscape renderable requires a valid lattice "
                        "mesh dependency");
                    return compile_failed_node(input);
                }
                if (!height || !height->valid()) {
                    logger->error(
                        "clipmap landscape renderable requires a valid height "
                        "field dependency");
                    return compile_failed_node(input);
                }
                if (!program || !program->valid()) {
                    logger->error(
                        "clipmap landscape renderable requires a valid program "
                        "dependency");
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

                // A placement DEP overrides only the texture->world footprint
                // (origin/size/vertical/base) and flags the recipe so the
                // renderer uses the baked footprint verbatim instead of
                // re-deriving it from the scene-node transform. The lattice
                // geometry snap (lattice_world_cell_size, c0) and view_snapped
                // are left untouched — c0 stays mesh-derived (issue #218 Phase
                // 2 scope guard). The recipe key already folds the placement dep
                // (deps participate in the key) so a placement change re-compiles
                // the renderable. When no placement is connected, settings are
                // left as authored (placement_authoritative stays false) =
                // exactly today's behaviour.
                ClipmapLandscapeRenderSettings settings = desc->settings;
                if (placement) {
                    if (!placement->valid()) {
                        logger->error(
                            "clipmap landscape renderable placement is invalid");
                        return compile_failed_node(input);
                    }
                    settings.world_origin[0] = placement->origin[0];
                    settings.world_origin[1] = placement->origin[2];
                    settings.world_size[0] = placement->extent[0];
                    settings.world_size[1] = placement->extent[2];
                    settings.vertical_scale = placement->extent[1];
                    settings.base_height = placement->base_height;
                    settings.placement_authoritative = true;
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
                        .clipmap = settings,
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

        // DEPRECATED (issue #222): the terrain surface renderable (0x704) also
        // stands on TerrainAsset and is retired by #222 in favour of the shipped
        // clipmap stack (0x708 landscape renderable + ScalarField height +
        // Placement). Intentionally NOT ported by the #195 scrap-and-rebuild;
        // dies with #222. Kept alive only for existing terrain content/tests.
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
    }
}
