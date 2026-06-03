#include <engine/assets/scene/scene_authoring_materialize.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/assets/hdri/hdri_image_loader.h>
#include <engine/assets/hdri/hdri_lighting_metadata.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <file/filesystem.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
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
        using HDRIEnvironmentCache =
            std::unordered_map<std::string, HDRIEnvironmentAsset>;

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

        MeshRenderLayerStyle mesh_render_layer_style_for_scene_layer(
            const SceneMeshRenderLayerAsset& layer)
        {
            MeshRenderLayerStyle out{};
            out.enabled = layer.enabled;
            for (int i = 0; i < 4; ++i) {
                out.color[i] = layer.color[i];
            }
            out.emissive_strength = layer.emissive_strength;
            return out;
        }

        MeshRenderStyleData mesh_render_style_data_for_scene_style(
            const SceneMeshRenderStyleAsset& style)
        {
            MeshRenderStyleData out{};
            out.wireframe =
                mesh_render_layer_style_for_scene_layer(style.wireframe);
            out.surface =
                mesh_render_layer_style_for_scene_layer(style.surface);
            out.alpha = style.alpha;
            out.depth_test = style.depth_test;
            out.depth_write = style.depth_write;
            out.double_sided = style.double_sided;
            out.hidden_line_prepass = style.hidden_line_prepass;
            return out;
        }

        std::string mesh_render_style_cache_key(
            const SceneMeshRenderStyleAsset& style)
        {
            const auto layer_key =
                [](const char* prefix, const SceneMeshRenderLayerAsset& layer) {
                    return std::string(prefix)
                        + (layer.enabled ? ":on" : ":off")
                        + ":color:" + std::to_string(layer.color[0])
                        + "," + std::to_string(layer.color[1])
                        + "," + std::to_string(layer.color[2])
                        + "," + std::to_string(layer.color[3])
                        + ":emissive:"
                        + std::to_string(layer.emissive_strength);
                };
            return std::string("mesh_style")
                + ((style.depth_test || style.depth_write)
                    ? ":depth_occlusion"
                    : ":no_depth_occlusion")
                + ":" + layer_key("wireframe", style.wireframe)
                + ":" + layer_key("surface", style.surface)
                + ":alpha:" + std::to_string(style.alpha)
                + (style.double_sided ? ":double_sided" : ":single_sided")
                + (style.hidden_line_prepass
                    ? ":hidden_line_prepass"
                    : ":no_hidden_line_prepass");
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
            out += ":lighting:";
            out += std::to_string(static_cast<int>(style.lighting_source));
            out += ":env:" + style.environment_node;
            out += ":dir:" + style.directional_light_node;
            out += ":amb:" + style.ambient_light_node;
            out += ":ambient_strength:" + std::to_string(style.ambient_strength);
            out += ":sky_visibility:" + std::to_string(style.sky_visibility_strength);
            out += ":normal_lighting:" + std::to_string(style.normal_lighting_strength);
            out += ":terrain_bounce:" + std::to_string(style.terrain_bounce_strength);
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

        std::string hdri_environment_cache_key(
            const SceneHDRIEnvironmentAsset& environment)
        {
            std::ostringstream out;
            out << "hdri_environment:"
                << environment.path << ":"
                << static_cast<int>(environment.format) << ":"
                << environment.exposure << ":"
                << environment.rotation_x_radians << ":"
                << environment.rotation_y_radians << ":"
                << environment.rotation_z_radians << ":"
                << environment.lighting_intensity << ":"
                << environment.reflection_intensity << ":"
                << environment.background_intensity << ":"
                << environment.lighting_sample_resolution << ":"
                << environment.environment_light_color[0] << ":"
                << environment.environment_light_color[1] << ":"
                << environment.environment_light_color[2] << ":"
                << environment.environment_light_intensity << ":"
                << environment.dominant_light_direction[0] << ":"
                << environment.dominant_light_direction[1] << ":"
                << environment.dominant_light_direction[2] << ":"
                << environment.dominant_light_color[0] << ":"
                << environment.dominant_light_color[1] << ":"
                << environment.dominant_light_color[2] << ":"
                << environment.dominant_light_intensity << ":"
                << environment.dominant_light_confidence;
            return out.str();
        }

        void derive_hdri_environment_metadata_for_scene(
            const EngineAssetLibrary& assets,
            SceneHDRIEnvironmentAsset& environment)
        {
            if (environment.path.empty()) {
                return;
            }
            if (environment.format == HDRIEnvironmentFormat::RadianceHDR) {
                return;
            }

            const wz::fs::Path full_path =
                wz::fs::is_absolute(environment.path)
                    ? environment.path
                    : wz::fs::join(assets.resource_root(), environment.path);

            static std::mutex cache_mutex;
            static std::unordered_map<std::string, HDRILightingMetadata> cache;
            std::string file_identity;
            std::string file_identity_error;
            if (!openexr_image_file_identity_key(
                    full_path,
                    file_identity,
                    file_identity_error))
            {
                return;
            }
            const std::string cache_key =
                full_path + ":sample_width:"
                + std::to_string(environment.lighting_sample_resolution)
                + ":file_identity:" + file_identity;

            HDRILightingMetadata metadata{};
            bool found_cached = false;
            {
                std::lock_guard<std::mutex> lock(cache_mutex);
                const auto found = cache.find(cache_key);
                if (found != cache.end()) {
                    metadata = found->second;
                    found_cached = true;
                }
            }

            if (!found_cached) {
                std::shared_ptr<const HDRImageData> image;
                std::string error;
                if (!load_openexr_image_from_file_cached(
                        full_path,
                        image,
                        error))
                {
                    return;
                }

                if (!derive_hdri_lighting_metadata(
                        *image,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        environment.lighting_sample_resolution,
                        metadata))
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(cache_mutex);
                cache[cache_key] = metadata;
            }

            metadata = transform_hdri_lighting_metadata(
                metadata,
                environment.exposure,
                environment.rotation_x_radians,
                environment.rotation_y_radians,
                environment.rotation_z_radians);

            if (metadata.environment_light_intensity <= 0.0f
                && metadata.dominant_light_intensity <= 0.0f)
            {
                return;
            }

            environment.environment_light_color[0] =
                metadata.environment_light_color[0];
            environment.environment_light_color[1] =
                metadata.environment_light_color[1];
            environment.environment_light_color[2] =
                metadata.environment_light_color[2];
            environment.environment_light_intensity =
                metadata.environment_light_intensity;

            environment.dominant_light_direction[0] =
                metadata.dominant_light_direction[0];
            environment.dominant_light_direction[1] =
                metadata.dominant_light_direction[1];
            environment.dominant_light_direction[2] =
                metadata.dominant_light_direction[2];
            environment.dominant_light_color[0] =
                metadata.dominant_light_color[0];
            environment.dominant_light_color[1] =
                metadata.dominant_light_color[1];
            environment.dominant_light_color[2] =
                metadata.dominant_light_color[2];
            environment.dominant_light_intensity =
                metadata.dominant_light_intensity;
            environment.dominant_light_confidence =
                metadata.dominant_light_confidence;
        }

        const SceneHDRIEnvironmentAsset* find_hdri_environment_for_style(
            const SceneAssetData& scene,
            const SceneTerrainRenderStyleAsset& style)
        {
            if (!style.environment_node.empty()) {
                const SceneNodeAsset* node =
                    find_scene_node(scene, style.environment_node);
                return node && node->hdri_environment
                    ? &*node->hdri_environment
                    : nullptr;
            }

            for (const auto& node : scene.nodes) {
                if (node.hdri_environment) {
                    return &*node.hdri_environment;
                }
            }
            return nullptr;
        }

        TerrainLightingData terrain_lighting_for_style(
            const SceneAssetData& scene,
            const SceneTerrainRenderStyleAsset& style)
        {
            TerrainLightingData out{};
            const bool use_environment =
                style.lighting_source == SceneTerrainLightingSource::EnvironmentNode
                || style.lighting_source == SceneTerrainLightingSource::Hybrid;
            if (!use_environment) {
                return out;
            }

            const SceneHDRIEnvironmentAsset* environment =
                find_hdri_environment_for_style(scene, style);
            if (!environment) {
                return out;
            }

            out.mode = TerrainLightingMode::HDRIEnvironment;
            const bool has_environment_light =
                environment->environment_light_intensity > 0.0f;
            for (int i = 0; i < 3; ++i) {
                out.environment_color[i] =
                    has_environment_light
                        ? environment->environment_light_color[i]
                        : environment->dominant_light_color[i];
                out.dominant_light_direction[i] =
                    environment->dominant_light_direction[i];
                out.dominant_light_color[i] =
                    environment->dominant_light_color[i];
            }
            out.environment_intensity =
                (environment->environment_light_intensity > 0.0f
                    ? (std::max)(0.0f, environment->environment_light_intensity)
                        * (std::max)(0.0f, environment->lighting_intensity)
                    : (std::max)(0.0f, environment->lighting_intensity))
                * (std::max)(0.0f, style.ambient_strength);
            out.dominant_light_intensity =
                (std::max)(0.0f, environment->dominant_light_intensity)
                * (std::max)(0.0f, environment->lighting_intensity)
                * (std::max)(0.0f, style.normal_lighting_strength);
            out.sky_visibility_strength =
                (std::max)(0.0f, style.sky_visibility_strength);
            out.normal_lighting_strength =
                (std::max)(0.0f, style.normal_lighting_strength);
            out.terrain_bounce_strength =
                (std::max)(0.0f, style.terrain_bounce_strength);
            return out;
        }

        wz::scene::LightRecord scene_light_record_for_node(
            const SceneNodeAsset& node,
            const SceneDirectLightSourceAsset& source)
        {
            float dir[3]{};
            authored_light_direction_from_node(node, dir);

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

        std::string sanitize_import_segment(std::string text)
        {
            for (char& ch : text) {
                const auto byte = static_cast<unsigned char>(ch);
                if (!std::isalnum(byte) && ch != '_' && ch != '-') {
                    ch = '_';
                }
            }
            while (!text.empty() && text.front() == '_') {
                text.erase(text.begin());
            }
            while (!text.empty() && text.back() == '_') {
                text.pop_back();
            }
            return text.empty() ? "glb_scene" : text;
        }

        std::string scene_import_prefix_for_node(
            const SceneNodeAsset& anchor,
            const SceneImportSourceAsset& source)
        {
            if (!source.import_prefix.empty()) {
                return source.import_prefix;
            }

            return anchor.id + "/"
                + sanitize_import_segment(wz::fs::stem(source.path));
        }

        std::string imported_scene_node_id(
            const std::string& import_prefix,
            const std::string& imported_id)
        {
            return import_prefix + "/" + imported_id;
        }

        bool node_belongs_to_import(
            const SceneNodeAsset& node,
            const std::string& anchor_id,
            const std::string& import_prefix)
        {
            return node.imported_node
                && node.imported_node->anchor_node == anchor_id
                && node.imported_node->import_prefix == import_prefix;
        }

        bool materialize_scene_import_source(
            SceneAssetData& scene,
            EngineAssetLibrary& assets,
            const std::string& anchor_id,
            const SceneImportSourceAsset& source,
            std::string& error)
        {
            if (source.kind != SceneImportSourceKind::GLB) {
                error = "unsupported scene import source on node " + anchor_id;
                return false;
            }
            if (source.path.empty()) {
                error = "GLB scene import has empty path on node " + anchor_id;
                return false;
            }

            SceneNodeAsset* anchor = find_scene_node(scene, anchor_id);
            if (!anchor) {
                error = "scene import anchor not found: " + anchor_id;
                return false;
            }
            const std::optional<SceneMeshRenderStyleAsset>
                inherited_render_style = anchor->mesh_render_style;

            const std::string import_prefix =
                scene_import_prefix_for_node(*anchor, source);

            const auto bytes = wz::fs::read_file(
                assets.files().resolve_path(source.path));
            if (!bytes) {
                error = "failed to read GLB scene import: " + source.path;
                return false;
            }

            ImportedGLTFScene imported{};
            std::string import_error;
            if (!import_gltf_scene(
                    bytes.value.data(),
                    bytes.value.size(),
                    GLTFSceneImportOptions{ .scene_index = source.scene_index },
                    imported,
                    &import_error))
            {
                error = "failed to import GLB scene hierarchy: "
                    + source.path + ": " + import_error;
                return false;
            }

            for (auto& node : scene.nodes) {
                if (node_belongs_to_import(node, anchor_id, import_prefix)) {
                    node.imported_node->missing_source = true;
                }
            }

            for (const auto& imported_node : imported.nodes) {
                const std::string authored_id =
                    imported_scene_node_id(import_prefix, imported_node.id);

                SceneNodeAsset* node = find_scene_node(scene, authored_id);
                const bool existed = node != nullptr;
                if (node
                    && !node_belongs_to_import(*node, anchor_id, import_prefix))
                {
                    error = "GLB scene import node id collides with existing "
                        "authored node: " + authored_id;
                    return false;
                }

                if (!node) {
                    SceneNodeAsset created =
                        make_scene_node(authored_id, imported_node.name);
                    scene.nodes.push_back(std::move(created));
                    node = &scene.nodes.back();
                }

                node->name = imported_node.name.empty()
                    ? imported_node.id
                    : imported_node.name;
                node->local = imported_node.local;
                node->imported_node = SceneImportedNodeAsset{
                    .anchor_node = anchor_id,
                    .import_prefix = import_prefix,
                    .source_node_id = imported_node.id,
                    .missing_source = false,
                };

                if (imported_node.parent_id) {
                    node->parent_id = imported_scene_node_id(
                        import_prefix,
                        *imported_node.parent_id);
                }
                else {
                    node->parent_id = anchor_id;
                }

                if (imported_node.mesh_index) {
                    node->mesh_source = SceneMeshSourceAsset{
                        .kind = SceneMeshSourceKind::GLB,
                        .path = source.path,
                        .mesh_index = *imported_node.mesh_index,
                    };
                    if (!existed && inherited_render_style) {
                        node->mesh_render_style = *inherited_render_style;
                    }
                }
                else {
                    node->mesh_source.reset();
                    node->renderable_asset.reset();
                }
            }

            return true;
        }

        bool materialize_scene_import_sources(
            SceneAssetData& scene,
            EngineAssetLibrary& assets,
            std::string& error)
        {
            struct PendingImport
            {
                std::string anchor_id;
                SceneImportSourceAsset source;
            };

            std::vector<PendingImport> imports;
            for (const auto& node : scene.nodes) {
                if (node.scene_import_source) {
                    imports.push_back(PendingImport{
                        .anchor_id = node.id,
                        .source = *node.scene_import_source,
                    });
                }
            }

            for (const auto& import : imports) {
                if (!materialize_scene_import_source(
                        scene,
                        assets,
                        import.anchor_id,
                        import.source,
                        error))
                {
                    return false;
                }
            }

            return true;
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
                const bool uses_explicit_lights =
                    style.lighting_source
                        == SceneTerrainLightingSource::ExplicitNodes
                    || style.lighting_source
                        == SceneTerrainLightingSource::Hybrid;
                if (!uses_explicit_lights) {
                    continue;
                }
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
            SceneMeshRenderStyleAsset& style,
            RenderableCache& renderables,
            RenderableAsset& out)
        {
            if (const auto found = renderables.find(key);
                found != renderables.end())
            {
                out = found->second;
                return true;
            }

            MeshRenderStyleAsset style_asset{};
            if (style.style_asset == wz::asset::AssetKey{}) {
                style_asset =
                    assets.mesh_render_styles().create_mesh_render_style({
                        .name = name + "_style",
                        .style = mesh_render_style_data_for_scene_style(style),
                    });
                if (!style_asset.valid()) {
                    return false;
                }
                style.style_asset = style_asset.output;
            }
            else {
                style_asset = MeshRenderStyleAsset{ .output = style.style_asset };
            }

            RenderableAsset renderable =
                assets.renderables().create_mesh_styled({
                    .name = name,
                    .mesh = mesh,
                    .style = style_asset,
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
            const SceneAssetData& scene,
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
                    .lighting = terrain_lighting_for_style(scene, style),
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
            SceneMeshRenderStyleAsset& style,
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

        const SceneNodeAsset* find_sky_visual_node(
            const SceneAssetData& scene,
            const SceneNodeAsset& surface_node,
            const SceneSkySurfaceAsset& surface)
        {
            if (!surface.visual_node.empty()) {
                return find_scene_node(scene, surface.visual_node);
            }
            return surface_node.sky_visual ? &surface_node : nullptr;
        }

        bool sky_visual_has_drawable_source(
            const SceneSkyVisualAsset& visual)
        {
            switch (visual.kind) {
            case SceneSkyVisualKind::SolidColor:
            case SceneSkyVisualKind::DirectionDebug:
            case SceneSkyVisualKind::Gradient:
                return true;
            case SceneSkyVisualKind::EquirectangularTexture:
                return !(visual.texture_asset == wz::asset::AssetKey{})
                    || !visual.texture_path.empty();
            case SceneSkyVisualKind::ScalarField:
                return !(visual.scalar_field_asset == wz::asset::AssetKey{});
            case SceneSkyVisualKind::VectorField:
                return !(visual.vector_field_asset == wz::asset::AssetKey{});
            case SceneSkyVisualKind::None:
                return false;
            }
            return false;
        }

        void materialize_sky_draws(
            SceneAssetData& scene,
            const std::unordered_map<std::string, wz::asset::AssetKey>&
                scalar_field_assets_by_node,
            const std::unordered_map<std::string, wz::asset::AssetKey>&
                vector_field_assets_by_node)
        {
            scene.sky_draws.clear();

            for (const auto& node : scene.nodes) {
                if (!node.sky_surface || !node.visible) {
                    continue;
                }

                const SceneSkySurfaceAsset& surface = *node.sky_surface;
                if (!surface.visible_to_camera) {
                    continue;
                }

                const SceneNodeAsset* visual_node =
                    find_sky_visual_node(scene, node, surface);
                if (!visual_node || !visual_node->sky_visual) {
                    continue;
                }

                SceneSkyVisualAsset visual = *visual_node->sky_visual;
                if (visual.kind == SceneSkyVisualKind::ScalarField
                    && visual.scalar_field_node.empty()
                    && visual_node->scalar_field_source)
                {
                    visual.scalar_field_node = visual_node->id;
                }
                if (visual.kind == SceneSkyVisualKind::ScalarField
                    && !visual.scalar_field_node.empty())
                {
                    const auto found =
                        scalar_field_assets_by_node.find(
                            visual.scalar_field_node);
                    if (found != scalar_field_assets_by_node.end()) {
                        visual.scalar_field_asset = found->second;
                    }
                }
                if (visual.kind == SceneSkyVisualKind::VectorField
                    && visual.vector_field_node.empty()
                    && visual_node->vector_field_source)
                {
                    visual.vector_field_node = visual_node->id;
                }
                if (visual.kind == SceneSkyVisualKind::VectorField
                    && !visual.vector_field_node.empty())
                {
                    const auto found =
                        vector_field_assets_by_node.find(
                            visual.vector_field_node);
                    if (found != vector_field_assets_by_node.end()) {
                        visual.vector_field_asset = found->second;
                    }
                }

                if (!sky_visual_has_drawable_source(visual)) {
                    continue;
                }

                SceneSkyDrawAsset draw{};
                draw.surface_node = node.id;
                draw.visual_node = visual_node->id;
                draw.visual_kind = visual.kind;
                draw.projection = surface.projection;
                draw.radius = surface.radius;
                draw.visible_to_camera = surface.visible_to_camera;
                draw.solid_color[0] = visual.solid_color[0];
                draw.solid_color[1] = visual.solid_color[1];
                draw.solid_color[2] = visual.solid_color[2];
                draw.gradient_top_color[0] = visual.gradient_top_color[0];
                draw.gradient_top_color[1] = visual.gradient_top_color[1];
                draw.gradient_top_color[2] = visual.gradient_top_color[2];
                draw.gradient_bottom_color[0] =
                    visual.gradient_bottom_color[0];
                draw.gradient_bottom_color[1] =
                    visual.gradient_bottom_color[1];
                draw.gradient_bottom_color[2] =
                    visual.gradient_bottom_color[2];
                draw.texture_asset = visual.texture_asset;
                draw.texture_path = visual.texture_path;
                draw.texture_format = visual.texture_format;
                draw.scalar_field_asset = visual.scalar_field_asset;
                draw.vector_field_asset = visual.vector_field_asset;
                draw.exposure = visual.exposure;
                draw.rotation_x_radians = visual.rotation_x_radians;
                draw.rotation_y_radians = visual.rotation_y_radians;
                draw.rotation_z_radians = visual.rotation_z_radians;

                scene.sky_draws.push_back(draw);
            }
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
        HDRIEnvironmentCache hdri_environments;
        std::unordered_map<std::string, wz::asset::AssetKey> mesh_assets_by_node;
        std::unordered_map<std::string, wz::asset::AssetKey>
            scalar_field_assets_by_node;
        std::unordered_map<std::string, wz::asset::AssetKey>
            vector_field_assets_by_node;
        const SceneMeshRenderStyleAsset default_render_style{};
        scene.sky_draws.clear();

        if (!materialize_scene_import_sources(scene, assets, report.error)) {
            if (report.error.empty()) {
                report.error = "scene import source materialization failed";
            }
            return report;
        }

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
            vector_field_assets_by_node[node.id] = vector_field.output;
        }

        materialize_sky_draws(
            scene,
            scalar_field_assets_by_node,
            vector_field_assets_by_node);

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

        for (auto& node : scene.nodes) {
            if (!node.hdri_environment) {
                continue;
            }

            auto& environment_source = *node.hdri_environment;
            if (environment_source.path.empty()) {
                environment_source.environment_asset = {};
                continue;
            }

            derive_hdri_environment_metadata_for_scene(
                assets,
                environment_source);

            const std::string key =
                hdri_environment_cache_key(environment_source);
            HDRIEnvironmentAsset environment{};
            if (const auto found = hdri_environments.find(key);
                found != hdri_environments.end())
            {
                environment = found->second;
            }
            else {
                environment = assets.lights().create_hdri_environment({
                    .name = "scene_editor/hdri_environment/" + node.id,
                    .path = environment_source.path,
                    .format = environment_source.format,
                    .exposure = environment_source.exposure,
                    .rotation_x_radians =
                        environment_source.rotation_x_radians,
                    .rotation_y_radians =
                        environment_source.rotation_y_radians,
                    .rotation_z_radians =
                        environment_source.rotation_z_radians,
                    .lighting_intensity =
                        environment_source.lighting_intensity,
                    .reflection_intensity =
                        environment_source.reflection_intensity,
                    .background_intensity =
                        environment_source.background_intensity,
                    .lighting_sample_resolution =
                        environment_source.lighting_sample_resolution,
                    .environment_light_color = {
                        environment_source.environment_light_color[0],
                        environment_source.environment_light_color[1],
                        environment_source.environment_light_color[2],
                    },
                    .environment_light_intensity =
                        environment_source.environment_light_intensity,
                    .dominant_light_direction = {
                        environment_source.dominant_light_direction[0],
                        environment_source.dominant_light_direction[1],
                        environment_source.dominant_light_direction[2],
                    },
                    .dominant_light_color = {
                        environment_source.dominant_light_color[0],
                        environment_source.dominant_light_color[1],
                        environment_source.dominant_light_color[2],
                    },
                    .dominant_light_intensity =
                        environment_source.dominant_light_intensity,
                    .dominant_light_confidence =
                        environment_source.dominant_light_confidence,
                });
                if (!environment.valid()) {
                    report.error =
                        "HDRI environment asset unavailable for "
                        + node_log_name(node);
                    return report;
                }
                hdri_environments.emplace(key, environment);
            }

            environment_source.environment_asset = environment.output;
        }

        prioritize_terrain_render_style_lights(scene);

        for (auto& node : scene.nodes) {
            if (!node.mesh_source) {
                continue;
            }

            MeshAsset mesh{};
            const bool has_authored_render_style =
                node.mesh_render_style.has_value();
            SceneMeshRenderStyleAsset render_style =
                node.mesh_render_style.value_or(default_render_style);
            render_style.style_asset = {};

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
                if (has_authored_render_style) {
                    node.mesh_render_style = render_style;
                }
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
                    scene,
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
