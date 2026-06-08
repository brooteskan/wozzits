#include <engine/assets/scene/scene_authoring_materialize.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/assets/hdri/hdri_image_loader.h>
#include <engine/assets/hdri/hdri_lighting_metadata.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <asset/key_utils.h>
#include <file/filesystem.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
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
        using CollisionCache =
            std::unordered_map<std::string, CollisionAsset>;

        uint64_t hash_string64(std::string_view text) noexcept
        {
            uint64_t hash = 14695981039346656037ull;
            for (const char ch : text) {
                hash ^= static_cast<unsigned char>(ch);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string hex64(uint64_t value)
        {
            std::ostringstream out;
            out << std::hex << std::setfill('0') << std::setw(16) << value;
            return out.str();
        }

        void append_u32(std::vector<uint8_t>& bytes, uint32_t value)
        {
            const auto* src = reinterpret_cast<const uint8_t*>(&value);
            bytes.insert(bytes.end(), src, src + sizeof(value));
        }

        void append_f32(std::vector<uint8_t>& bytes, float value)
        {
            const auto* src = reinterpret_cast<const uint8_t*>(&value);
            bytes.insert(bytes.end(), src, src + sizeof(value));
        }

        bool read_u32(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            uint32_t& out) noexcept
        {
            if (offset + sizeof(out) > bytes.size()) {
                return false;
            }
            std::memcpy(&out, bytes.data() + offset, sizeof(out));
            offset += sizeof(out);
            return true;
        }

        bool read_f32(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            float& out) noexcept
        {
            if (offset + sizeof(out) > bytes.size()) {
                return false;
            }
            std::memcpy(&out, bytes.data() + offset, sizeof(out));
            offset += sizeof(out);
            return true;
        }

        wz::fs::Path hdri_lighting_metadata_cache_path(
            const EngineAssetCacheSettings& cache,
            const std::string& file_identity,
            uint32_t sample_resolution)
        {
            if (!cache.enabled || cache.root.empty()) {
                return {};
            }

            const std::string key =
                hex64(hash_string64(file_identity))
                + hex64(hash_string64(
                    "sample:" + std::to_string(sample_resolution)));
            return wz::fs::join(
                wz::fs::join(
                    wz::fs::join(cache.root, "assets"),
                    "hdri_lighting_metadata"),
                key + ".bin");
        }

        std::vector<uint8_t> serialize_hdri_lighting_metadata(
            const HDRILightingMetadata& metadata)
        {
            std::vector<uint8_t> bytes;
            bytes.reserve(sizeof(uint32_t) + sizeof(float) * 14u);
            append_u32(bytes, 1u);
            for (float value : metadata.environment_light_color) {
                append_f32(bytes, value);
            }
            append_f32(bytes, metadata.environment_light_intensity);
            for (float value : metadata.dominant_light_direction) {
                append_f32(bytes, value);
            }
            for (float value : metadata.dominant_light_color) {
                append_f32(bytes, value);
            }
            append_f32(bytes, metadata.dominant_light_intensity);
            append_f32(bytes, metadata.dominant_light_confidence);
            return bytes;
        }

        bool deserialize_hdri_lighting_metadata(
            const std::vector<uint8_t>& bytes,
            HDRILightingMetadata& metadata)
        {
            size_t offset = 0;
            uint32_t version = 0;
            if (!read_u32(bytes, offset, version) || version != 1u) {
                return false;
            }
            HDRILightingMetadata out{};
            for (float& value : out.environment_light_color) {
                if (!read_f32(bytes, offset, value)) {
                    return false;
                }
            }
            if (!read_f32(bytes, offset, out.environment_light_intensity)) {
                return false;
            }
            for (float& value : out.dominant_light_direction) {
                if (!read_f32(bytes, offset, value)) {
                    return false;
                }
            }
            for (float& value : out.dominant_light_color) {
                if (!read_f32(bytes, offset, value)) {
                    return false;
                }
            }
            if (!read_f32(bytes, offset, out.dominant_light_intensity)
                || !read_f32(bytes, offset, out.dominant_light_confidence)
                || offset != bytes.size())
            {
                return false;
            }
            metadata = out;
            return true;
        }

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

        std::string mesh_processing_cache_suffix(
            const SceneMeshProcessingAsset* processing)
        {
            if (!processing || !processing->enabled) {
                return {};
            }

            std::ostringstream out;
            out << ":decimate"
                << ":v" << processing->target_vertex_count
                << ":t" << processing->target_triangle_count
                << ":r" << processing->target_ratio
                << ":b" << (processing->preserve_boundary ? 1 : 0)
                << ":ar" << processing->aspect_ratio
                << ":el" << processing->edge_length
                << ":mv" << processing->max_valence
                << ":nd" << processing->normal_deviation
                << ":he" << processing->hausdorff_error;
            return out.str();
        }

        std::string mesh_cache_key(
            const SceneMeshSourceAsset& source,
            const SceneMeshProcessingAsset* processing)
        {
            return mesh_source_cache_key(source)
                + mesh_processing_cache_suffix(processing);
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
            out.field_visualization.enabled =
                style.field_visualization_enabled;
            out.field_visualization.channel_id =
                style.field_visualization_channel_id;
            out.field_visualization.value_min =
                style.field_visualization_value_min;
            out.field_visualization.value_max =
                style.field_visualization_value_max;
            out.field_visualization.gamma =
                style.field_visualization_gamma;
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
                    : ":no_hidden_line_prepass")
                + (style.field_visualization_enabled
                    ? ":field_visualization:"
                        + std::to_string(style.field_visualization_channel_id)
                        + ":min:"
                        + std::to_string(
                            style.field_visualization_value_min)
                        + ":max:"
                        + std::to_string(
                            style.field_visualization_value_max)
                        + ":gamma:"
                        + std::to_string(
                            style.field_visualization_gamma)
                    : ":no_field_visualization");
        }

        std::string mesh_wavelet_analysis_cache_key(
            const SceneMeshWaveletAnalysisAsset* analysis)
        {
            if (!analysis) {
                return ":wavelet_default";
            }
            return std::string(":wavelet")
                + (analysis->enabled ? ":on" : ":off")
                + ":fn:"
                + std::to_string(static_cast<uint32_t>(analysis->function))
                + ":scales:" + std::to_string(analysis->scale_count)
                + ":lambda:"
                + std::to_string(analysis->lambda_max_estimate)
                + ":gamma:" + std::to_string(analysis->gamma);
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
            out += ":target_pixels_per_triangle:"
                + std::to_string(style.target_pixels_per_triangle);
            out += style.enable_surfel_lods
                ? ":surfel_lods"
                : ":no_surfel_lods";
            out += ":surfel_target_px:"
                + std::to_string(style.surfel_target_coverage_px);
            out += ":max_asset_density:"
                + std::to_string(style.max_asset_triangle_density);
            out += ":max_screen_density:"
                + std::to_string(style.max_screen_triangle_density);
            out += ":visual_chunk_count:"
                + std::to_string(style.visual_chunk_count);
            return out;
        }

        std::string terrain_constraint_surface_cache_key(
            const wz::asset::AssetKey& terrain_asset)
        {
            return std::string("terrain_constraint_surface:")
                + std::to_string(terrain_asset.content_hash.lo)
                + ":" + std::to_string(terrain_asset.content_hash.hi)
                + ":projected_heightfield:2048x2048";
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
            if (environment.environment_light_intensity > 0.0f
                || environment.dominant_light_intensity > 0.0f
                || environment.dominant_light_confidence > 0.0f)
            {
                // First-pass nonzero lighting metadata is authored explicitly.
                // After materialization, environment_asset marks derived metadata
                // that should refresh when exposure or rotation changes.
                if (environment.environment_asset == wz::asset::AssetKey{}) {
                    return;
                }
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
            const wz::fs::Path disk_cache_path =
                hdri_lighting_metadata_cache_path(
                    assets.cache_settings(),
                    cache_key,
                    environment.lighting_sample_resolution);

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
                if (!disk_cache_path.empty() && wz::fs::exists(disk_cache_path)) {
                    const auto bytes = wz::fs::read_file(disk_cache_path);
                    if (bytes
                        && deserialize_hdri_lighting_metadata(
                            bytes.value,
                            metadata))
                    {
                        assets.logger().info(
                            "asset disk cache hit: hdri lighting metadata "
                            + disk_cache_path);
                        std::lock_guard<std::mutex> lock(cache_mutex);
                        cache[cache_key] = metadata;
                        found_cached = true;
                    }
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

                {
                    std::lock_guard<std::mutex> lock(cache_mutex);
                    cache[cache_key] = metadata;
                }
                if (!disk_cache_path.empty()) {
                    const auto directory = wz::fs::parent_path(disk_cache_path);
                    if (!directory.empty()) {
                        wz::fs::create_directories(directory);
                    }
                    const auto bytes =
                        serialize_hdri_lighting_metadata(metadata);
                    if (wz::fs::write_file(disk_cache_path, bytes, true)
                        == wz::fs::FileError::None)
                    {
                        assets.logger().info(
                            "asset disk cache store: hdri lighting metadata "
                            + disk_cache_path);
                    }
                }
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
            SceneMeshWaveletAnalysisAsset* wavelet_analysis,
            RenderableCache& renderables,
            RenderableAsset& out)
        {
            if (const auto found = renderables.find(key);
                found != renderables.end())
            {
                out = found->second;
                return true;
            }

            SceneMeshRenderStyleAsset effective_style = style;
            if (effective_style.field_visualization_enabled
                && wavelet_analysis
                && !wavelet_analysis->enabled)
            {
                effective_style.field_visualization_enabled = false;
                effective_style.field_visualization_asset = {};
            }

            MeshRenderStyleAsset style_asset{};
            if (effective_style.style_asset == wz::asset::AssetKey{}) {
                style_asset =
                    assets.mesh_render_styles().create_mesh_render_style({
                        .name = name + "_style",
                        .style = mesh_render_style_data_for_scene_style(
                            effective_style),
                    });
                if (!style_asset.valid()) {
                    return false;
                }
                style.style_asset = style_asset.output;
                effective_style.style_asset = style_asset.output;
            }
            else {
                style_asset =
                    MeshRenderStyleAsset{ .output = effective_style.style_asset };
            }

            if (effective_style.field_visualization_enabled
                && effective_style.field_visualization_asset
                    == wz::asset::AssetKey{})
            {
                const SceneMeshWaveletAnalysisAsset analysis =
                    wavelet_analysis ? *wavelet_analysis
                                     : SceneMeshWaveletAnalysisAsset{};
                MeshDerivedFieldAsset field_asset =
                    assets.mesh_derived_fields().create_wavelet_analysis({
                        .name = name + "_wavelet_field",
                        .source_mesh = mesh,
                        .scale_count = analysis.scale_count,
                        .lambda_max_estimate =
                            analysis.lambda_max_estimate,
                        .gamma = analysis.gamma,
                    });
                if (!field_asset.valid()) {
                    return false;
                }
                style.field_visualization_asset = field_asset.output;
                effective_style.field_visualization_asset = field_asset.output;
                if (wavelet_analysis) {
                    wavelet_analysis->field_asset = field_asset.output;
                }
            }

            RenderableAsset renderable =
                assets.renderables().create_mesh_styled({
                    .name = name,
                    .mesh = mesh,
                    .style = style_asset,
                    .mesh_field_visualization =
                        effective_style.field_visualization_enabled
                            ? MeshDerivedFieldAsset{
                                .output =
                                    effective_style.field_visualization_asset,
                            }
                            : MeshDerivedFieldAsset{},
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
            TerrainVisualProxyAsset visual_proxy,
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
                    .visual_proxy = visual_proxy,
                    .mesh_policy_flags =
                        policy_flags_for_terrain_render_style(style, false),
                    .lighting = terrain_lighting_for_style(scene, style),
                    .target_pixels_per_triangle =
                        style.target_pixels_per_triangle,
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
            const SceneMeshProcessingAsset* processing,
            MeshCache& meshes,
            MeshAsset& out,
            std::string& error)
        {
            const std::string source_key = mesh_cache_key(source, processing);
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

            if (processing && processing->enabled) {
                mesh = assets.meshes().create_decimated_mesh({
                    .name = "scene_editor/processed_mesh/" + source_key,
                    .source_mesh = mesh,
                    .target_vertex_count =
                        processing->target_vertex_count,
                    .target_triangle_count =
                        processing->target_triangle_count,
                    .target_ratio = processing->target_ratio,
                    .preserve_boundary = processing->preserve_boundary,
                    .aspect_ratio = processing->aspect_ratio,
                    .edge_length = processing->edge_length,
                    .max_valence = processing->max_valence,
                    .normal_deviation = processing->normal_deviation,
                    .hausdorff_error = processing->hausdorff_error,
                });
                if (!mesh.valid()) {
                    error = "failed to register processed mesh: "
                        + source_key;
                    return false;
                }
            }

            meshes.emplace(source_key, mesh);
            out = mesh;
            return true;
        }

        bool ensure_processed_mesh_asset(
            EngineAssetLibrary& assets,
            MeshAsset source_mesh,
            const std::string& key,
            const SceneMeshProcessingAsset* processing,
            MeshCache& meshes,
            MeshAsset& out,
            std::string& error)
        {
            if (!processing || !processing->enabled) {
                out = source_mesh;
                return true;
            }

            const std::string processed_key =
                key + mesh_processing_cache_suffix(processing);
            if (const auto found = meshes.find(processed_key);
                found != meshes.end())
            {
                out = found->second;
                return true;
            }

            MeshAsset processed = assets.meshes().create_decimated_mesh({
                .name = "scene_editor/processed_mesh/" + processed_key,
                .source_mesh = source_mesh,
                .target_vertex_count = processing->target_vertex_count,
                .target_triangle_count = processing->target_triangle_count,
                .target_ratio = processing->target_ratio,
                .preserve_boundary = processing->preserve_boundary,
                .aspect_ratio = processing->aspect_ratio,
                .edge_length = processing->edge_length,
                .max_valence = processing->max_valence,
                .normal_deviation = processing->normal_deviation,
                .hausdorff_error = processing->hausdorff_error,
            });
            if (!processed.valid()) {
                error = "failed to register processed mesh: "
                    + processed_key;
                return false;
            }

            meshes.emplace(processed_key, processed);
            out = processed;
            return true;
        }

        bool ensure_renderable_for_mesh_source(
            EngineAssetLibrary& assets,
            const SceneMeshSourceAsset& source,
            const SceneMeshProcessingAsset* processing,
            SceneMeshWaveletAnalysisAsset* wavelet_analysis,
            SceneMeshRenderStyleAsset& style,
            RenderableCache& renderables,
            MeshCache& meshes,
            RenderableAsset& out,
            MeshAsset& out_mesh,
            std::string& error)
        {
            const std::string source_key = mesh_cache_key(source, processing);
            const std::string key =
                source_key + ":"
                + mesh_render_style_cache_key(style)
                + mesh_wavelet_analysis_cache_key(
                    style.field_visualization_enabled
                        ? wavelet_analysis
                        : nullptr);
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

            if (!ensure_mesh_for_source(
                    assets,
                    source,
                    processing,
                    meshes,
                    out_mesh,
                    error))
            {
                return false;
            }

            if (!ensure_wireframe_renderable_for_mesh_asset(
                    assets,
                    key,
                    "scene_editor/" + key + "_wireframe",
                    out_mesh,
                    style,
                    wavelet_analysis,
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
            wz::asset::append_unique_key(report.renderables_to_realize, key);
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
        CollisionCache collisions;
        std::unordered_map<std::string, wz::asset::AssetKey> mesh_assets_by_node;
        std::unordered_map<std::string, wz::asset::AssetKey>
            scalar_field_assets_by_node;
        std::unordered_map<std::string, wz::asset::AssetKey>
            vector_field_assets_by_node;
        const SceneMeshRenderStyleAsset default_render_style{};
        scene.sky_draws.clear();

        const auto materialize_started = std::chrono::steady_clock::now();
        auto elapsed_ms_since = [](const auto& started)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        };
        auto log_phase = [&assets](const std::string& name, int64_t ms)
        {
            assets.logger().info(
                "scene authoring materialize phase " + name
                + " ms=" + std::to_string(ms));
        };

        const auto import_started = std::chrono::steady_clock::now();
        if (!materialize_scene_import_sources(scene, assets, report.error)) {
            if (report.error.empty()) {
                report.error = "scene import source materialization failed";
            }
            return report;
        }
        log_phase("import_sources", elapsed_ms_since(import_started));

        const auto fields_started = std::chrono::steady_clock::now();
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
        log_phase("fields_and_sky", elapsed_ms_since(fields_started));

        const auto lights_started = std::chrono::steady_clock::now();
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
        log_phase("lights_and_environment", elapsed_ms_since(lights_started));

        const auto mesh_started = std::chrono::steady_clock::now();
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
            render_style.field_visualization_asset = {};
            if (node.mesh_wavelet_analysis) {
                node.mesh_wavelet_analysis->field_asset = {};
            }

            if (options.create_preview_renderables && node.visible) {
                RenderableAsset renderable{};
                if (!ensure_renderable_for_mesh_source(
                        assets,
                        *node.mesh_source,
                        node.mesh_processing
                            ? &*node.mesh_processing
                            : nullptr,
                        node.mesh_wavelet_analysis
                            ? &*node.mesh_wavelet_analysis
                            : nullptr,
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
                    node.mesh_processing ? &*node.mesh_processing : nullptr,
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
            else {
                node.renderable_asset.reset();
            }

            if (mesh.valid()) {
                mesh_assets_by_node[node.id] = mesh.output;
            }
        }
        log_phase("mesh_sources", elapsed_ms_since(mesh_started));

        const auto terrain_links_started = std::chrono::steady_clock::now();
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
        log_phase("terrain_source_links", elapsed_ms_since(terrain_links_started));

        const auto terrain_started = std::chrono::steady_clock::now();
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
            const SceneTerrainRenderStyleAsset render_style =
                node.terrain_render_style.value_or(
                    SceneTerrainRenderStyleAsset{});
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
                auto& source = *node.terrain_mesh_source;
                if (source.mesh_asset == wz::asset::AssetKey{}) {
                    terrain.terrain_asset = {};
                    node.renderable_asset.reset();
                    continue;
                }

                MeshAsset terrain_mesh{ .output = source.mesh_asset };
                if (!ensure_processed_mesh_asset(
                        assets,
                        terrain_mesh,
                        "terrain_mesh:" + node.id,
                        node.mesh_processing
                            ? &*node.mesh_processing
                            : nullptr,
                        meshes,
                        terrain_mesh,
                        report.error))
                {
                    if (report.error.empty()) {
                        report.error =
                            "processed terrain mesh unavailable for "
                            + node_log_name(node);
                    }
                    return report;
                }
                source.mesh_asset = terrain_mesh.output;

                terrain_asset = assets.terrains().create_from_mesh({
                    .name = "scene_editor/terrain/" + node.id,
                    .mesh = terrain_mesh,
                    .height_policy =
                        terrain_height_policy_for_source(source.height_policy),
                    .min_surface_normal_y = source.min_surface_normal_y,
                    .include_backfaces = source.include_backfaces,
                    .render_mode = TerrainRenderMode::DebugMesh,
                    .collision_mode = TerrainCollisionMode::MeshSurface,
                    .visual_chunk_count =
                        render_style.visual_chunk_count,
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
            terrain.visual_proxy_asset = {};

            TerrainVisualProxyAsset visual_proxy{};
            if (is_mesh_terrain) {
                visual_proxy = assets.terrain_visual_proxies().create_from_terrain({
                    .name = "scene_editor/terrain_visual_proxy/" + node.id,
                    .terrain = terrain_asset,
                });
                if (!visual_proxy.valid()) {
                    report.error =
                        "terrain visual proxy unavailable for "
                        + node_log_name(node);
                    return report;
                }
                terrain.visual_proxy_asset = visual_proxy.output;
            }

            if (terrain.calculate_constraint_surface) {
                const std::string key =
                    terrain_constraint_surface_cache_key(terrain.terrain_asset);
                CollisionAsset collision{};
                if (const auto found = collisions.find(key);
                    found != collisions.end())
                {
                    collision = found->second;
                }
                else {
                    collision = assets.collisions().create_from_terrain({
                        .name =
                            "scene_editor/collision/constraint_heightfield/"
                            + node.id,
                        .terrain = TerrainAsset{
                            .output = terrain.terrain_asset,
                        },
                        .build_method =
                            CollisionBuildMethod::TerrainProjectionHeightField,
                        .occupancy = CollisionOccupancyData{
                            .kind =
                                CollisionOccupancyKind::WalkableSurface,
                            .blocks_movement = true,
                            .queryable = true,
                        },
                        .projection_resolution_x = 2048u,
                        .projection_resolution_y = 2048u,
                    });
                    if (!collision.valid()) {
                        report.error =
                            "terrain constraint surface unavailable for "
                            + node_log_name(node);
                        return report;
                    }
                    collisions.emplace(key, collision);
                }
                terrain.constraint_surface_asset = collision.output;
            }

            if (!terrain.visible) {
                node.renderable_asset.reset();
                continue;
            }

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
                    visual_proxy,
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
        log_phase("terrain_assets", elapsed_ms_since(terrain_started));

        report.ok = true;
        assets.logger().info(
            "scene authoring materialize complete nodes="
            + std::to_string(scene.nodes.size())
            + " renderables_to_realize="
            + std::to_string(report.renderables_to_realize.size())
            + " ms="
            + std::to_string(elapsed_ms_since(materialize_started)));
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
