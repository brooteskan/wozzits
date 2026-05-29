#include <engine/assets/scene/scene_authoring_materialize.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace wz::engine::assets
{
    namespace
    {
        using MeshCache = std::unordered_map<std::string, MeshAsset>;
        using RenderableCache =
            std::unordered_map<std::string, RenderableAsset>;
        using ScalarFieldCache =
            std::unordered_map<std::string, ScalarFieldAsset>;
        using VectorFieldCache =
            std::unordered_map<std::string, VectorFieldAsset>;
        using DirectLightCache =
            std::unordered_map<std::string, DirectLightAsset>;
        using AmbientLightingCache =
            std::unordered_map<std::string, AmbientLightingAsset>;

        std::string mesh_source_cache_key(const SceneMeshSourceAsset& source)
        {
            switch (source.kind) {
            case SceneMeshSourceKind::Placeholder:
                return "placeholder";
            case SceneMeshSourceKind::GLB:
                return "glb:" + source.path + ":"
                    + std::to_string(source.mesh_index);
            case SceneMeshSourceKind::ProceduralCube:
                return "procedural:cube";
            case SceneMeshSourceKind::ProceduralQuad:
                return "procedural:quad";
            case SceneMeshSourceKind::ProceduralTriangle:
                return "procedural:triangle";
            }

            return "placeholder";
        }

        uint32_t policy_flags_for_mesh_render_style(
            const SceneMeshRenderStyleAsset& style)
        {
            uint32_t flags = RenderPolicy_Wireframe;
            if (style.depth_test || style.depth_write) {
                flags |= RenderPolicy_DepthTest;
                flags |= RenderPolicy_DepthWrite;
            }
            return flags;
        }

        BuiltinRenderProgram program_for_mesh_render_style(
            const SceneMeshRenderStyleAsset& style)
        {
            return style.depth_test || style.depth_write
                ? BuiltinRenderProgram::MeshWireframeDepthDebug
                : BuiltinRenderProgram::MeshWireframeDebug;
        }

        std::string mesh_render_style_cache_key(
            const SceneMeshRenderStyleAsset& style)
        {
            return std::string("wireframe")
                + ((style.depth_test || style.depth_write)
                    ? ":depth_occlusion"
                    : ":no_depth_occlusion");
        }

        uint32_t policy_flags_for_terrain_render_style(
            const SceneTerrainRenderStyleAsset& style,
            bool wireframe)
        {
            uint32_t flags = wireframe ? RenderPolicy_Wireframe : RenderPolicy_None;
            if (style.depth_test) {
                flags |= RenderPolicy_DepthTest;
            }
            if (style.depth_write) {
                flags |= RenderPolicy_DepthWrite;
            }
            return flags;
        }

        std::string terrain_render_style_cache_key(
            const SceneTerrainRenderStyleAsset& style)
        {
            std::string out = "terrain_render:";
            switch (style.path) {
            case SceneTerrainRenderPath::Auto:
                out += "auto";
                break;
            case SceneTerrainRenderPath::Surface:
                out += "surface";
                break;
            case SceneTerrainRenderPath::DebugWireframe:
                out += "debug_wireframe";
                break;
            case SceneTerrainRenderPath::None:
                out += "none";
                break;
            }
            out += style.depth_test ? ":depth_test" : ":no_depth_test";
            out += style.depth_write ? ":depth_write" : ":no_depth_write";
            return out;
        }

        std::string direct_light_source_cache_key(
            const SceneDirectLightSourceAsset& source)
        {
            std::ostringstream out;
            out << "direct_light:"
                << static_cast<int>(source.kind) << ":"
                << source.color[0] << ":"
                << source.color[1] << ":"
                << source.color[2] << ":"
                << source.intensity << ":"
                << source.range << ":"
                << source.inner_cone_radians << ":"
                << source.outer_cone_radians;
            return out.str();
        }

        std::string ambient_lighting_cache_key(
            const SceneAmbientLightingAsset& lighting)
        {
            std::ostringstream out;
            out << "ambient_lighting:"
                << static_cast<int>(lighting.mode) << ":"
                << lighting.color[0] << ":"
                << lighting.color[1] << ":"
                << lighting.color[2] << ":"
                << lighting.intensity << ":"
                << static_cast<int>(lighting.domain_mapping) << ":"
                << lighting.intensity_field.content_hash.lo << ":"
                << lighting.intensity_field.content_hash.hi << ":"
                << lighting.color_field.content_hash.lo << ":"
                << lighting.color_field.content_hash.hi;
            return out.str();
        }

        wz::scene::LightRecord scene_light_record_for_node(
            const SceneNodeAsset& node,
            const SceneDirectLightSourceAsset& source)
        {
            const float qx = node.local.rotation_quat[0];
            const float qy = node.local.rotation_quat[1];
            const float qz = node.local.rotation_quat[2];
            const float qw = node.local.rotation_quat[3];

            // Rotate local -Y into world space. Directional lights interpret
            // this as the direction light travels, matching common light gizmos.
            float dir[3]{
                2.0f * (qx * qy + qw * qz),
                -1.0f + 2.0f * (qx * qx + qz * qz),
                2.0f * (qy * qz - qw * qx),
            };
            const float len =
                std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (len > 1e-6f) {
                dir[0] /= len;
                dir[1] /= len;
                dir[2] /= len;
            }
            else {
                dir[0] = 0.0f;
                dir[1] = -1.0f;
                dir[2] = 0.0f;
            }

            wz::scene::LightRecord out{};
            out.position = {
                node.local.translation[0],
                node.local.translation[1],
                node.local.translation[2],
            };
            out.direction = { dir[0], dir[1], dir[2] };
            out.color = {
                source.color[0],
                source.color[1],
                source.color[2],
            };
            out.intensity = source.intensity;
            out.range = source.range;
            out.type = direct_light_kind_to_scene_light_type(source.kind);
            return out;
        }

        wz::scene::LightRecord scene_ambient_light_record_for_node(
            const SceneAmbientLightingAsset& source)
        {
            wz::scene::LightRecord out{};
            out.color = {
                source.color[0],
                source.color[1],
                source.color[2],
            };
            out.intensity = source.intensity;
            out.type = wz::scene::LightType::Ambient;
            return out;
        }

        std::string node_log_name(const SceneNodeAsset& node)
        {
            return "node id='" + node.id + "' name='" + node.name + "'";
        }

        void prioritize_terrain_render_style_lights(SceneAssetData& scene)
        {
            std::string directional_light_node;
            std::string ambient_light_node;

            for (const auto& node : scene.nodes) {
                if (!node.terrain_render_style) {
                    continue;
                }
                const auto& style = *node.terrain_render_style;
                if (directional_light_node.empty()) {
                    directional_light_node = style.directional_light_node;
                }
                if (ambient_light_node.empty()) {
                    ambient_light_node = style.ambient_light_node;
                }
                if (!directional_light_node.empty()
                    && !ambient_light_node.empty())
                {
                    break;
                }
            }

            if (directional_light_node.empty() && ambient_light_node.empty()) {
                return;
            }

            auto selected = [&](const SceneLightAsset& light)
            {
                return (!directional_light_node.empty()
                        && light.node_id == directional_light_node
                        && light.light.type == wz::scene::LightType::Directional)
                    || (!ambient_light_node.empty()
                        && light.node_id == ambient_light_node
                        && light.light.type == wz::scene::LightType::Ambient);
            };

            std::stable_sort(
                scene.lights.begin(),
                scene.lights.end(),
                [&](const SceneLightAsset& a, const SceneLightAsset& b)
                {
                    return selected(a) && !selected(b);
                });
        }

        std::string scalar_field_source_cache_key(
            const SceneScalarFieldSourceAsset& source)
        {
            std::ostringstream out;
            out << "scalar:";
            switch (source.kind) {
            case SceneScalarFieldSourceKind::RawF32:
                out << "raw_f32:" << source.path;
                break;
            case SceneScalarFieldSourceKind::ProceduralGradientX:
                out << "procedural_gradient_x";
                break;
            case SceneScalarFieldSourceKind::ProceduralGradientY:
                out << "procedural_gradient_y";
                break;
            case SceneScalarFieldSourceKind::ProceduralRadialGradient:
                out << "procedural_radial_gradient";
                break;
            case SceneScalarFieldSourceKind::ProceduralCheckerboard:
                out << "procedural_checkerboard";
                break;
            case SceneScalarFieldSourceKind::ProceduralSineWaves:
                out << "procedural_sine_waves";
                break;
            }
            out << ':' << source.width
                << ':' << source.height
                << ':' << source.depth
                << ':' << source.frequency
                << ':' << source.amplitude;
            return out.str();
        }

        std::string vector_field_source_cache_key(
            const SceneVectorFieldSourceAsset& source)
        {
            std::ostringstream out;
            out << "vector:";
            switch (source.kind) {
            case SceneVectorFieldSourceKind::RawF32:
                out << "raw_f32:" << source.path;
                break;
            }
            out << ':' << source.width
                << ':' << source.height
                << ':' << source.depth
                << ':' << source.components_per_channel;
            for (const auto& channel : source.channels) {
                out << ':' << channel.name;
            }
            return out.str();
        }

        ScalarFieldGenerator scalar_field_generator_for_source(
            SceneScalarFieldSourceKind kind)
        {
            switch (kind) {
            case SceneScalarFieldSourceKind::ProceduralGradientX:
                return ScalarFieldGenerator::GradientX;
            case SceneScalarFieldSourceKind::ProceduralGradientY:
                return ScalarFieldGenerator::GradientY;
            case SceneScalarFieldSourceKind::ProceduralRadialGradient:
                return ScalarFieldGenerator::RadialGradient;
            case SceneScalarFieldSourceKind::ProceduralCheckerboard:
                return ScalarFieldGenerator::Checkerboard;
            case SceneScalarFieldSourceKind::ProceduralSineWaves:
                return ScalarFieldGenerator::SineWaves;
            case SceneScalarFieldSourceKind::RawF32:
                break;
            }
            return ScalarFieldGenerator::GradientX;
        }

        ScalarFieldAsset create_scalar_field_asset_for_scene_source(
            EngineAssetLibrary& assets,
            const SceneScalarFieldSourceAsset& source,
            const std::string& key,
            std::string& error)
        {
            if (source.depth != 1) {
                error = "scalar field source depth must be 1 for V1: " + key;
                return {};
            }

            if (source.kind == SceneScalarFieldSourceKind::RawF32) {
                if (source.path.empty()) {
                    error = "scalar field source has empty path";
                    return {};
                }

                return assets.scalar_fields().create_scalar_field({
                    .name = "scene_editor/" + key,
                    .path = source.path,
                    .width = source.width,
                    .height = source.height,
                    .depth = source.depth,
                    .format = ScalarFieldFormat::Float32,
                    .domain_kind = ScalarFieldDomainKind::Spatial2D,
                });
            }

            return assets.scalar_fields().create_procedural_scalar_field({
                .name = "scene_editor/" + key,
                .width = source.width,
                .height = source.height,
                .depth = source.depth,
                .generator = scalar_field_generator_for_source(source.kind),
                .frequency = source.frequency,
                .amplitude = source.amplitude,
                .format = ScalarFieldFormat::Float32,
                .domain_kind = ScalarFieldDomainKind::Spatial2D,
            });
        }

        VectorFieldAsset create_vector_field_asset_for_scene_source(
            EngineAssetLibrary& assets,
            const SceneVectorFieldSourceAsset& source,
            const std::string& key,
            std::string& error)
        {
            if (source.kind == SceneVectorFieldSourceKind::RawF32) {
                if (source.path.empty()) {
                    error = "vector field source has empty path";
                    return {};
                }

                return assets.vector_fields().create_vector_field({
                    .name = "scene_editor/" + key,
                    .path = source.path,
                    .width = source.width,
                    .height = source.height,
                    .depth = source.depth,
                    .components_per_channel = source.components_per_channel,
                    .channels = source.channels,
                    .format = VectorFieldFormat::Float32,
                    .domain_kind = VectorFieldDomainKind::Spatial2D,
                });
            }

            return {};
        }

        MeshAsset create_mesh_asset_for_scene_source(
            EngineAssetLibrary& assets,
            const SceneMeshSourceAsset& source,
            std::string& error)
        {
            switch (source.kind) {
            case SceneMeshSourceKind::Placeholder:
                return assets.meshes().create_placeholder_mesh(
                    "scene_editor/placeholder_mesh");

            case SceneMeshSourceKind::GLB:
            {
                if (source.path.empty()) {
                    error = "GLB mesh source has empty path";
                    return {};
                }

                const wz::asset::AssetKey file =
                    assets.files().register_file_node(
                        source.path,
                        kRawFileSchema,
                        kAssetTypeRawFile);

                if (file == wz::asset::AssetKey{}) {
                    error = "failed to register GLB: " + source.path;
                    return {};
                }

                return assets.meshes().create_glb_mesh({
                    .name = "scene_editor/glb_mesh",
                    .source_file = file,
                    .mesh_index = source.mesh_index,
                });
            }

            case SceneMeshSourceKind::ProceduralCube:
                return assets.meshes().create_procedural_mesh({
                    .name = "scene_editor/procedural_cube",
                    .kind = ProceduralMeshKind::Cube,
                });

            case SceneMeshSourceKind::ProceduralQuad:
                return assets.meshes().create_procedural_mesh({
                    .name = "scene_editor/procedural_quad",
                    .kind = ProceduralMeshKind::Quad,
                });

            case SceneMeshSourceKind::ProceduralTriangle:
                return assets.meshes().create_procedural_mesh({
                    .name = "scene_editor/procedural_triangle",
                    .kind = ProceduralMeshKind::Triangle,
                });
            }

            return {};
        }

        bool ensure_wireframe_renderable_for_mesh_asset(
            EngineAssetLibrary& assets,
            const std::string& key,
            const std::string& name,
            MeshAsset mesh,
            const SceneMeshRenderStyleAsset& style,
            RenderableCache& renderables,
            RenderableAsset& out)
        {
            if (const auto found = renderables.find(key);
                found != renderables.end())
            {
                out = found->second;
                return true;
            }

            RenderableAsset renderable =
                assets.renderables().create_mesh_wireframe({
                    .name = name,
                    .mesh = mesh,
                    .program = program_for_mesh_render_style(style),
                    .domain = RenderDomain::Debug,
                    .policy_flags = policy_flags_for_mesh_render_style(style),
                });

            if (!renderable.valid()) {
                return false;
            }

            renderables.emplace(key, renderable);
            out = renderable;
            return true;
        }

        bool ensure_debug_renderable_for_terrain_asset(
            EngineAssetLibrary& assets,
            const std::string& key,
            const std::string& name,
            TerrainAsset terrain,
            const SceneTerrainRenderStyleAsset& style,
            RenderableCache& renderables,
            RenderableAsset& out)
        {
            if (const auto found = renderables.find(key);
                found != renderables.end())
            {
                out = found->second;
                return true;
            }

            RenderableAsset renderable =
                assets.renderables().create_terrain_debug({
                    .name = name,
                    .terrain = terrain,
                    .mesh_program =
                        (style.depth_test || style.depth_write)
                            ? BuiltinRenderProgram::MeshWireframeDepthDebug
                            : BuiltinRenderProgram::MeshWireframeDebug,
                    .mesh_policy_flags =
                        policy_flags_for_terrain_render_style(style, true),
                });

            if (!renderable.valid()) {
                return false;
            }

            renderables.emplace(key, renderable);
            out = renderable;
            return true;
        }

        bool ensure_surface_renderable_for_terrain_asset(
            EngineAssetLibrary& assets,
            const std::string& key,
            const std::string& name,
            TerrainAsset terrain,
            const SceneTerrainRenderStyleAsset& style,
            RenderableCache& renderables,
            RenderableAsset& out)
        {
            if (const auto found = renderables.find(key);
                found != renderables.end())
            {
                out = found->second;
                return true;
            }

            RenderableAsset renderable =
                assets.renderables().create_terrain_surface({
                    .name = name,
                    .terrain = terrain,
                    .mesh_policy_flags =
                        policy_flags_for_terrain_render_style(style, false),
                });

            if (!renderable.valid()) {
                return false;
            }

            renderables.emplace(key, renderable);
            out = renderable;
            return true;
        }

        TerrainMeshSurfaceHeightPolicy terrain_height_policy_for_source(
            SceneTerrainMeshHeightPolicy policy)
        {
            switch (policy) {
            case SceneTerrainMeshHeightPolicy::HighestAcceptedSurface:
                return TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface;
            }
            return TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface;
        }

        bool ensure_mesh_for_source(
            EngineAssetLibrary& assets,
            const SceneMeshSourceAsset& source,
            MeshCache& meshes,
            MeshAsset& out,
            std::string& error)
        {
            const std::string source_key = mesh_source_cache_key(source);
            if (const auto found = meshes.find(source_key);
                found != meshes.end())
            {
                out = found->second;
                return true;
            }

            MeshAsset mesh =
                create_mesh_asset_for_scene_source(assets, source, error);
            if (!mesh.valid()) {
                return false;
            }

            meshes.emplace(source_key, mesh);
            out = mesh;
            return true;
        }

        bool ensure_renderable_for_mesh_source(
            EngineAssetLibrary& assets,
            const SceneMeshSourceAsset& source,
            const SceneMeshRenderStyleAsset& style,
            RenderableCache& renderables,
            MeshCache& meshes,
            RenderableAsset& out,
            MeshAsset& out_mesh,
            std::string& error)
        {
            const std::string source_key = mesh_source_cache_key(source);
            const std::string key =
                source_key + ":" + mesh_render_style_cache_key(style);
            if (const auto found = renderables.find(key);
                found != renderables.end())
            {
                out = found->second;
                if (const auto mesh_found = meshes.find(source_key);
                    mesh_found != meshes.end())
                {
                    out_mesh = mesh_found->second;
                }
                return true;
            }

            if (!ensure_mesh_for_source(assets, source, meshes, out_mesh, error)) {
                return false;
            }

            if (!ensure_wireframe_renderable_for_mesh_asset(
                    assets,
                    key,
                    "scene_editor/" + key + "_wireframe",
                    out_mesh,
                    style,
                    renderables,
                    out))
            {
                error = "failed to register mesh renderable: " + key;
                return false;
            }

            return true;
        }

        const SceneNodeAsset* find_scene_node(
            const SceneAssetData& scene,
            const wz::scene::AuthoredEntityId& id)
        {
            for (const auto& node : scene.nodes) {
                if (node.id == id) {
                    return &node;
                }
            }
            return nullptr;
        }

        void append_unique_renderable(
            SceneAuthoringMaterializeReport& report,
            wz::asset::AssetKey key)
        {
            for (const auto& existing : report.renderables_to_realize) {
                if (existing == key) {
                    return;
                }
            }
            report.renderables_to_realize.push_back(key);
        }
    }

    SceneAuthoringMaterializeReport materialize_scene_authoring_components(
        SceneAssetData& scene,
        EngineAssetLibrary& assets,
        const SceneAuthoringMaterializeOptions& options)
    {
        SceneAuthoringMaterializeReport report{};
        RenderableCache renderables;
        MeshCache meshes;
        ScalarFieldCache scalar_fields;
        VectorFieldCache vector_fields;
        DirectLightCache direct_lights;
        AmbientLightingCache ambient_lighting;
        std::unordered_map<std::string, wz::asset::AssetKey> mesh_assets_by_node;
        std::unordered_map<std::string, wz::asset::AssetKey>
            scalar_field_assets_by_node;
        const SceneMeshRenderStyleAsset default_render_style{};

        for (auto& node : scene.nodes) {
            if (!node.scalar_field_source) {
                continue;
            }

            auto& source = *node.scalar_field_source;
            const std::string key = scalar_field_source_cache_key(source);
            ScalarFieldAsset scalar_field{};
            if (const auto found = scalar_fields.find(key);
                found != scalar_fields.end())
            {
                scalar_field = found->second;
            }
            else {
                scalar_field = create_scalar_field_asset_for_scene_source(
                    assets,
                    source,
                    key,
                    report.error);
                if (!scalar_field.valid()) {
                    if (report.error.empty()) {
                        report.error =
                            "scalar field source unavailable for "
                            + node_log_name(node);
                    }
                    return report;
                }
                scalar_fields.emplace(key, scalar_field);
            }

            source.scalar_field_asset = scalar_field.output;
            scalar_field_assets_by_node[node.id] = scalar_field.output;
        }

        for (auto& node : scene.nodes) {
            if (!node.vector_field_source) {
                continue;
            }

            auto& source = *node.vector_field_source;
            const std::string key = vector_field_source_cache_key(source);
            VectorFieldAsset vector_field{};
            if (const auto found = vector_fields.find(key);
                found != vector_fields.end())
            {
                vector_field = found->second;
            }
            else {
                vector_field = create_vector_field_asset_for_scene_source(
                    assets,
                    source,
                    key,
                    report.error);
                if (!vector_field.valid()) {
                    if (report.error.empty()) {
                        report.error =
                            "vector field source unavailable for "
                            + node_log_name(node);
                    }
                    return report;
                }
                vector_fields.emplace(key, vector_field);
            }

            source.vector_field_asset = vector_field.output;
        }

        for (auto& node : scene.nodes) {
            if (!node.direct_light_source) {
                continue;
            }

            auto& source = *node.direct_light_source;
            const std::string key = direct_light_source_cache_key(source);
            DirectLightAsset light{};
            if (const auto found = direct_lights.find(key);
                found != direct_lights.end())
            {
                light = found->second;
            }
            else {
                light = assets.lights().create_direct_light({
                    .name = "scene_editor/lights/" + node.id,
                    .kind = source.kind,
                    .color = {
                        source.color[0],
                        source.color[1],
                        source.color[2],
                    },
                    .intensity = source.intensity,
                    .range = source.range,
                    .inner_cone_radians = source.inner_cone_radians,
                    .outer_cone_radians = source.outer_cone_radians,
                });
                if (!light.valid()) {
                    report.error =
                        "direct light asset unavailable for "
                        + node_log_name(node);
                    return report;
                }
                direct_lights.emplace(key, light);
            }

            source.light_asset = light.output;
            scene.lights.erase(
                std::remove_if(
                    scene.lights.begin(),
                    scene.lights.end(),
                    [&](const SceneLightAsset& light_record)
                    {
                        return light_record.node_id == node.id;
                    }),
                scene.lights.end());
            scene.lights.push_back(SceneLightAsset{
                .node_id = node.id,
                .light = scene_light_record_for_node(node, source),
            });
        }

        for (auto& node : scene.nodes) {
            if (!node.ambient_lighting) {
                continue;
            }

            auto& lighting_source = *node.ambient_lighting;
            const std::string key = ambient_lighting_cache_key(lighting_source);
            AmbientLightingAsset lighting{};
            if (const auto found = ambient_lighting.find(key);
                found != ambient_lighting.end())
            {
                lighting = found->second;
            }
            else {
                lighting = assets.lights().create_ambient_lighting({
                    .name = "scene_editor/ambient_lighting/" + node.id,
                    .mode = lighting_source.mode,
                    .color = {
                        lighting_source.color[0],
                        lighting_source.color[1],
                        lighting_source.color[2],
                    },
                    .intensity = lighting_source.intensity,
                    .intensity_field = lighting_source.intensity_field,
                    .color_field = lighting_source.color_field,
                    .domain_mapping = lighting_source.domain_mapping,
                });
                if (!lighting.valid()) {
                    report.error =
                        "ambient lighting asset unavailable for "
                        + node_log_name(node);
                    return report;
                }
                ambient_lighting.emplace(key, lighting);
            }

            lighting_source.lighting_asset = lighting.output;
            scene.lights.erase(
                std::remove_if(
                    scene.lights.begin(),
                    scene.lights.end(),
                    [&](const SceneLightAsset& light_record)
                    {
                        return light_record.node_id == node.id;
                    }),
                scene.lights.end());
            scene.lights.push_back(SceneLightAsset{
                .node_id = node.id,
                .light = scene_ambient_light_record_for_node(lighting_source),
            });
        }

        prioritize_terrain_render_style_lights(scene);

        for (auto& node : scene.nodes) {
            if (!node.mesh_source) {
                continue;
            }

            MeshAsset mesh{};
            const SceneMeshRenderStyleAsset render_style =
                node.mesh_render_style.value_or(default_render_style);

            if (options.create_preview_renderables) {
                RenderableAsset renderable{};
                if (!ensure_renderable_for_mesh_source(
                        assets,
                        *node.mesh_source,
                        render_style,
                        renderables,
                        meshes,
                        renderable,
                        mesh,
                        report.error))
                {
                    if (report.error.empty()) {
                        report.error =
                            "mesh source unavailable for "
                            + node_log_name(node);
                    }
                    return report;
                }

                node.renderable.reset();
                attach_renderable_asset(node, renderable.output);
                append_unique_renderable(report, renderable.output);
            }
            else if (!ensure_mesh_for_source(
                    assets,
                    *node.mesh_source,
                    meshes,
                    mesh,
                    report.error))
            {
                if (report.error.empty()) {
                    report.error =
                        "mesh source unavailable for "
                        + node_log_name(node);
                }
                return report;
            }

            if (mesh.valid()) {
                mesh_assets_by_node[node.id] = mesh.output;
            }
        }

        for (auto& node : scene.nodes) {
            if (!node.terrain_mesh_source) {
                continue;
            }

            auto& source = *node.terrain_mesh_source;
            if (source.mode != SceneTerrainMeshSourceMode::SceneNode
                || source.source_node.empty())
            {
                continue;
            }

            const SceneNodeAsset* source_node =
                find_scene_node(scene, source.source_node);
            if (!source_node || !source_node->parent_id
                || *source_node->parent_id != node.id)
            {
                source.mesh_asset = {};
                continue;
            }

            if (const auto mesh_it = mesh_assets_by_node.find(source.source_node);
                mesh_it != mesh_assets_by_node.end())
            {
                source.mesh_asset = mesh_it->second;
            }
            else {
                source.mesh_asset = {};
            }
        }

        for (auto& node : scene.nodes) {
            if (!node.terrain_height_field_source) {
                continue;
            }

            auto& source = *node.terrain_height_field_source;
            if (source.mode != SceneTerrainHeightFieldSourceMode::SceneNode
                || source.source_node.empty())
            {
                continue;
            }

            const SceneNodeAsset* source_node =
                find_scene_node(scene, source.source_node);
            if (!source_node || !source_node->parent_id
                || *source_node->parent_id != node.id)
            {
                source.scalar_field_asset = {};
                continue;
            }

            if (const auto scalar_it =
                    scalar_field_assets_by_node.find(source.source_node);
                scalar_it != scalar_field_assets_by_node.end())
            {
                source.scalar_field_asset = scalar_it->second;
            }
            else {
                source.scalar_field_asset = {};
            }
        }

        for (auto& node : scene.nodes) {
            if (!node.terrain) {
                continue;
            }

            auto& terrain = *node.terrain;
            if (node.terrain_mesh_source && node.terrain_height_field_source) {
                report.error =
                    "terrain node has both mesh and heightfield sources: "
                    + node.id;
                return report;
            }

            TerrainAsset terrain_asset{};
            bool is_mesh_terrain = false;
            if (node.terrain_height_field_source) {
                const auto& source = *node.terrain_height_field_source;
                if (source.scalar_field_asset == wz::asset::AssetKey{}) {
                    terrain.terrain_asset = {};
                    node.renderable_asset.reset();
                    continue;
                }

                terrain_asset = assets.terrains().create_from_height_field({
                    .name = "scene_editor/terrain/" + node.id,
                    .height_field = ScalarFieldAsset{
                        .output = source.scalar_field_asset,
                    },
                    .origin = { source.origin[0], source.origin[1] },
                    .size = { source.size[0], source.size[1] },
                    .vertical_scale = source.vertical_scale,
                    .base_height = source.base_height,
                    .render_mode = TerrainRenderMode::DebugMesh,
                    .collision_mode = TerrainCollisionMode::HeightOnly,
                });
                if (!terrain_asset.valid()) {
                    report.error =
                        "heightfield terrain asset unavailable for "
                        + node_log_name(node);
                    return report;
                }
            }
            else if (node.terrain_mesh_source) {
                is_mesh_terrain = true;
                const auto& source = *node.terrain_mesh_source;
                if (source.mesh_asset == wz::asset::AssetKey{}) {
                    terrain.terrain_asset = {};
                    node.renderable_asset.reset();
                    continue;
                }

                terrain_asset = assets.terrains().create_from_mesh({
                    .name = "scene_editor/terrain/" + node.id,
                    .mesh = MeshAsset{ .output = source.mesh_asset },
                    .height_policy =
                        terrain_height_policy_for_source(source.height_policy),
                    .min_surface_normal_y = source.min_surface_normal_y,
                    .include_backfaces = source.include_backfaces,
                    .render_mode = TerrainRenderMode::DebugMesh,
                    .collision_mode = TerrainCollisionMode::MeshSurface,
                });
                if (!terrain_asset.valid()) {
                    report.error =
                        "terrain asset unavailable for "
                        + node_log_name(node);
                    return report;
                }
            }
            else {
                continue;
            }

            terrain.terrain_asset = terrain_asset.output;

            if (!terrain.visible) {
                node.renderable_asset.reset();
                continue;
            }

            const SceneTerrainRenderStyleAsset render_style =
                node.terrain_render_style.value_or(
                    SceneTerrainRenderStyleAsset{});

            RenderableAsset renderable{};
            bool use_surface = false;
            bool use_debug = false;

            switch (render_style.path) {
            case SceneTerrainRenderPath::Auto:
                use_surface =
                    is_mesh_terrain
                    && options.create_terrain_surface_renderables;
                use_debug =
                    !use_surface && options.create_terrain_debug_renderables;
                break;
            case SceneTerrainRenderPath::Surface:
                if (!is_mesh_terrain) {
                    report.error =
                        "terrain surface render path requires mesh terrain for "
                        + node_log_name(node);
                    return report;
                }
                use_surface = options.create_terrain_surface_renderables;
                break;
            case SceneTerrainRenderPath::DebugWireframe:
                use_debug = options.create_terrain_debug_renderables;
                break;
            case SceneTerrainRenderPath::None:
                break;
            }

            if (!use_surface && !use_debug) {
                node.renderable_asset.reset();
                continue;
            }

            const std::string key = "terrain:" + node.id
                + (use_surface ? ":surface:" : ":debug:")
                + terrain_render_style_cache_key(render_style);
            const std::string name = "scene_editor/terrain/" + node.id
                + (use_surface ? "_surface" : "_debug");
            const bool renderable_ok = use_surface
                ? ensure_surface_renderable_for_terrain_asset(
                    assets,
                    key,
                    name,
                    terrain_asset,
                    render_style,
                    renderables,
                    renderable)
                : ensure_debug_renderable_for_terrain_asset(
                    assets,
                    key,
                    name,
                    terrain_asset,
                    render_style,
                    renderables,
                    renderable);

            if (!renderable_ok)
            {
                report.error =
                    "terrain renderable unavailable for "
                    + node_log_name(node);
                return report;
            }

            node.renderable_asset = renderable.output;
            append_unique_renderable(report, renderable.output);
        }

        report.ok = true;
        return report;
    }

    SceneAssetData make_default_scene_authoring_scene(std::string name)
    {
        SceneAssetData scene{};
        scene.name = std::move(name);

        add_scene_node(scene, make_scene_node("root"));

        SceneNodeAsset camera = make_scene_node("camera_01");
        set_parent(camera, "root");
        camera.local.translation[1] = 2.0f;
        camera.local.translation[2] = -8.0f;
        attach_camera(camera);
        add_scene_node(scene, std::move(camera));

        scene.defaults.active_camera_node = "camera_01";
        return scene;
    }

} // namespace wz::engine::assets
