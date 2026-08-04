#include <engine/assets/scene/scene_authoring_materialize.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/compute_pipeline/hlsl_binding_extract.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/assets/hdri/hdri_image_loader.h>
#include <engine/assets/hdri/hdri_lighting_metadata.h>
#include <engine/assets/mesh_cluster_hierarchy_asset_module.h>
#include <engine/assets/mesh_style_pull_program.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <asset/draft.h>
#include <asset/key_utils.h>
#include <file/filesystem.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace wz::engine::assets
{
    namespace
    {
        struct SceneMeshProcessingOutputs
        {
            MeshAsset mesh{};
            MeshClusterHierarchyAsset hierarchy{};
        };

        using MeshCache = std::unordered_map<std::string, MeshAsset>;
        using MeshProcessingCache =
            std::unordered_map<std::string, SceneMeshProcessingOutputs>;
        using RenderableCache =
            std::unordered_map<std::string, RenderableAsset>;
        using ScalarFieldCache =
            std::unordered_map<std::string, ScalarFieldAsset>;
        using VectorFieldCache =
            std::unordered_map<std::string, VectorFieldAsset>;
        struct MeshFieldRefEntry
        {
            wz::asset::AssetKey asset{};
            MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Vertex;
            std::string label;
        };

        struct MeshOperatorRefEntry
        {
            wz::asset::AssetKey asset{};
            MeshOperatorDomain domain = MeshOperatorDomain::Vertex;
            std::string label;
        };

        using MeshFieldRefCache =
            std::unordered_map<std::string, MeshFieldRefEntry>;
        using MeshOperatorRefCache =
            std::unordered_map<std::string, MeshOperatorRefEntry>;
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

        std::string short_asset_key(const wz::asset::AssetKey& key)
        {
            if (key == wz::asset::AssetKey{}) {
                return "none";
            }
            std::ostringstream out;
            out << std::hex << std::setfill('0')
                << std::setw(8) << static_cast<uint32_t>(key.content_hash.lo)
                << ":"
                << std::setw(8) << static_cast<uint32_t>(key.deps_hash.lo);
            return out.str();
        }

        const char* mesh_field_domain_name(
            MeshDerivedFieldDomain domain) noexcept
        {
            switch (domain) {
            case MeshDerivedFieldDomain::Vertex:
                return "Vertex";
            case MeshDerivedFieldDomain::Edge:
                return "Edge";
            case MeshDerivedFieldDomain::Face:
                return "Face";
            case MeshDerivedFieldDomain::Corner:
                return "Corner";
            }
            return "Unknown";
        }

        const char* mesh_operator_domain_name(
            MeshOperatorDomain domain) noexcept
        {
            switch (domain) {
            case MeshOperatorDomain::Vertex:
                return "Vertex";
            case MeshOperatorDomain::Edge:
                return "Edge";
            case MeshOperatorDomain::Face:
                return "Face";
            case MeshOperatorDomain::Corner:
                return "Corner";
            }
            return "Unknown";
        }

        std::string describe_field_ref(
            std::string_view canonical_ref,
            const MeshFieldRefEntry& entry)
        {
            std::string out = std::string(canonical_ref)
                + " domain=" + mesh_field_domain_name(entry.domain)
                + " asset=" + short_asset_key(entry.asset);
            if (!entry.label.empty()) {
                out += " source=" + entry.label;
            }
            return out;
        }

        std::string describe_operator_ref(
            std::string_view canonical_ref,
            const MeshOperatorRefEntry& entry)
        {
            std::string out = std::string(canonical_ref)
                + " domain=" + mesh_operator_domain_name(entry.domain)
                + " asset=" + short_asset_key(entry.asset);
            if (!entry.label.empty()) {
                out += " source=" + entry.label;
            }
            return out;
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
            const SceneMeshProcessingAsset* processing,
            const SceneMeshRegionSetAsset* region_set = nullptr)
        {
            if (!processing || !processing->enabled) {
                return {};
            }

            if (processing->operation
                == SceneMeshProcessingOperation::MeshClusterHierarchyPreview)
            {
                std::string key = ":cluster_hierarchy_preview"
                    ":level" + std::to_string(
                        processing->preview_level_index);
                if (region_set && region_set->enabled) {
                    key += ":region_set:" + region_set->region_set_id
                        + ":intent:"
                        + std::to_string(
                            static_cast<uint32_t>(region_set->intent))
                        + ":field_ref:" + region_set->source_field_ref
                        + ":domain:"
                        + std::to_string(
                            static_cast<uint32_t>(region_set->mask.domain))
                        + ":rules:"
                        + std::to_string(region_set->mask.rules.size());
                    for (const MeshMaskRule& rule :
                         region_set->mask.rules)
                    {
                        key += ":rule:"
                            + std::to_string(rule.enabled ? 1 : 0)
                            + ":ch:"
                            + std::to_string(rule.input_channel_id)
                            + ":lo:" + std::to_string(rule.lo)
                            + ":hi:" + std::to_string(rule.hi)
                            + ":priority:"
                            + std::to_string(rule.priority);
                    }
                }
                return key;
            }

            if (processing->operation
                == SceneMeshProcessingOperation::DebugTriangleStride)
            {
                return ":debug_triangle_stride"
                    ":level" + std::to_string(
                        processing->preview_level_index);
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

        uint32_t debug_triangle_stride_for_level(uint32_t level) noexcept
        {
            constexpr uint32_t kMaxDebugStrideLevel = 30u;
            level = std::min(level, kMaxDebugStrideLevel);
            return 1u << (level + 1u);
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
            out.field_visualization.palette =
                style.field_visualization_palette;
            out.mask = style.mask;
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
            std::string key = std::string("mesh_style")
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
            key += style.field_visualization_enabled
                ? ":field_visualization:"
                    + std::to_string(style.field_visualization_channel_id)
                    + ":min:"
                    + std::to_string(style.field_visualization_value_min)
                    + ":max:"
                    + std::to_string(style.field_visualization_value_max)
                    + ":gamma:"
                    + std::to_string(style.field_visualization_gamma)
                    + ":palette:"
                    + std::to_string(
                        static_cast<uint32_t>(
                            style.field_visualization_palette))
                    + ":field_ref:" + style.field_visualization_field_ref
                : ":no_field_visualization";
            if (!style.mask.enabled) {
                key += ":no_mask";
                return key;
            }
            key += ":mask:domain:"
                + std::to_string(static_cast<uint32_t>(style.mask.domain))
                + ":projection:"
                + std::to_string(
                    static_cast<uint32_t>(style.mask.projection_mode))
                + ":overlap:"
                + std::to_string(
                    static_cast<uint32_t>(style.mask.overlap_mode))
                + ":unmatched:"
                + std::to_string(style.mask.unmatched_color[0])
                + "," + std::to_string(style.mask.unmatched_color[1])
                + "," + std::to_string(style.mask.unmatched_color[2])
                + "," + std::to_string(style.mask.unmatched_color[3])
                + (style.mask.show_unmatched
                    ? ":show_unmatched"
                    : ":hide_unmatched")
                + ":field_ref:" + style.mask_source_field_ref
                + ":rules:" + std::to_string(style.mask.rules.size());
            for (const MeshMaskRule& rule : style.mask.rules) {
                key += ":rule:"
                    + std::to_string(rule.enabled ? 1 : 0)
                    + ":ch:" + std::to_string(rule.input_channel_id)
                    + ":lo:" + std::to_string(rule.lo)
                    + ":hi:" + std::to_string(rule.hi)
                    + ":color:" + std::to_string(rule.color[0])
                    + "," + std::to_string(rule.color[1])
                    + "," + std::to_string(rule.color[2])
                    + "," + std::to_string(rule.color[3])
                    + ":priority:" + std::to_string(rule.priority);
            }
            return key;
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

        std::string mesh_compute_field_cache_key(
            const SceneMeshComputeFieldAsset* field)
        {
            if (!field) {
                return ":compute_field_none";
            }
            std::string key = std::string(":compute_field")
                + (field->enabled ? ":on" : ":off")
                + ":" + field->kernel_id
                + ":" + field->hlsl_path
                + ":" + field->entry
                + ":" + field->target
                + ":tg:" + std::to_string(field->thread_group_size_x)
                + ":" + std::to_string(field->thread_group_size_y)
                + ":" + std::to_string(field->thread_group_size_z);
            key += ":inputs:" + std::to_string(field->inputs.size());
            for (const MeshComputeInput input : field->inputs) {
                key += ":" + std::to_string(static_cast<uint32_t>(input));
            }
            key += ":channels:" + std::to_string(field->channels.size());
            for (const auto& channel : field->channels) {
                key += ":" + std::to_string(channel.channel_id)
                    + ":"
                    + std::to_string(
                        static_cast<uint32_t>(channel.value_type));
            }
            key += ":params:" + std::to_string(field->params.size());
            for (const uint32_t param : field->params) {
                key += ":" + std::to_string(param);
            }
            return key;
        }

        std::string mesh_derived_field_source_cache_key(
            const SceneMeshDerivedFieldSourceAsset* source)
        {
            if (!source) {
                return ":mesh_field_source_none";
            }
            return std::string(":mesh_field_source")
                + (source->enabled ? ":on" : ":off")
                + ":id:" + source->field_id
                + ":domain:"
                + std::to_string(static_cast<uint32_t>(source->domain))
                + ":channel:" + std::to_string(source->channel_id)
                + ":type:"
                + std::to_string(static_cast<uint32_t>(source->value_type))
                + ":kind:"
                + std::to_string(static_cast<uint32_t>(source->source_kind))
                + ":component:"
                + std::to_string(static_cast<uint32_t>(source->component))
                + (source->normalize ? ":normalized" : ":raw")
                + ":constant:" + std::to_string(source->constant_value);
        }

        ComputePipelineAsset create_builtin_mesh_wavelet_pipeline(
            EngineAssetLibrary& assets)
        {
            if (!assets.gpu_device_valid()) {
                return {};
            }

            const ComputeShaderAsset shader =
                assets.shaders().create_compute_shader({
                    .name = "mesh_wavelet/detail_heat",
                    .path = "shaders/mesh_wavelet/detail_heat_cs.hlsl",
                    .entry = "main",
                    .target = "cs_5_0",
                });
            if (!shader.valid()) {
                return {};
            }

            return assets.compute_pipelines().create_compute_pipeline({
                .name = "mesh_wavelet/detail_heat_pipeline",
                .compute_shader = shader.shader,
                .bindings = {
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::MeshVertices,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(float) * 6u,
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferUAV,
                        .semantic =
                            ComputeBindingSemantic::MeshDerivedFieldValues,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(float),
                    },
                },
                .root_constant_dwords = 12,
                .thread_group_size_x = 128,
                .thread_group_size_y = 1,
                .thread_group_size_z = 1,
            });
        }

        ComputePipelineAsset create_builtin_mesh_cluster_graph_cells_pipeline(
            EngineAssetLibrary& assets)
        {
            if (!assets.gpu_device_valid()) {
                return {};
            }

            const ComputeShaderAsset shader =
                assets.shaders().create_compute_shader({
                    .name = "mesh_cluster_hierarchy/laplacian_graph_cells_neighbors",
                    .path =
                        "shaders/mesh_cluster_hierarchy/laplacian_graph_cells_neighbors_cs.hlsl",
                    .entry = "main",
                    .target = "cs_5_0",
                });
            if (!shader.valid()) {
                return {};
            }

            return assets.compute_pipelines().create_compute_pipeline({
                .name =
                    "mesh_cluster_hierarchy/laplacian_graph_cells_neighbors_pipeline",
                .compute_shader = shader.shader,
                .bindings = {
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::MeshVertices,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(float) * 3u,
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::Scratch,
                        .shader_register = 1,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(uint32_t),
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::Scratch,
                        .shader_register = 2,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(uint32_t),
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferUAV,
                        .semantic = ComputeBindingSemantic::Scratch,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(uint32_t),
                    },
                },
                .root_constant_dwords = 4,
                .thread_group_size_x = 128,
                .thread_group_size_y = 1,
                .thread_group_size_z = 1,
            });
        }

        ComputePipelineAsset create_builtin_mesh_cluster_graph_cells_compact_pipeline(
            EngineAssetLibrary& assets)
        {
            if (!assets.gpu_device_valid()) {
                return {};
            }

            const ComputeShaderAsset shader =
                assets.shaders().create_compute_shader({
                    .name =
                        "mesh_cluster_hierarchy/laplacian_graph_cells_compact",
                    .path =
                        "shaders/mesh_cluster_hierarchy/laplacian_graph_cells_compact_cs.hlsl",
                    .entry = "main",
                    .target = "cs_5_0",
                });
            if (!shader.valid()) {
                return {};
            }

            return assets.compute_pipelines().create_compute_pipeline({
                .name =
                    "mesh_cluster_hierarchy/laplacian_graph_cells_compact_pipeline",
                .compute_shader = shader.shader,
                .bindings = {
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::MeshVertices,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(float) * 3u,
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::MeshIndices,
                        .shader_register = 1,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(uint32_t),
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::Scratch,
                        .shader_register = 2,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(uint32_t),
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferSRV,
                        .semantic = ComputeBindingSemantic::Scratch,
                        .shader_register = 3,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(float),
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferUAV,
                        .semantic = ComputeBindingSemantic::Scratch,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(float) * 3u,
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferUAV,
                        .semantic = ComputeBindingSemantic::Scratch,
                        .shader_register = 1,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(uint32_t),
                    },
                    ComputeBindingDesc{
                        .kind = ComputeBindingKind::StructuredBufferUAV,
                        .semantic = ComputeBindingSemantic::Scratch,
                        .shader_register = 2,
                        .register_space = 0,
                        .descriptor_count = 1,
                        .stride_bytes = sizeof(uint32_t),
                    },
                },
                .root_constant_dwords = 4,
                .thread_group_size_x = 128,
                .thread_group_size_y = 1,
                .thread_group_size_z = 1,
            });
        }

        RenderProgramAsset create_builtin_mesh_vertex_pull_program(
            EngineAssetLibrary& assets)
        {
            const ShaderPairAsset shaders =
                assets.shaders().create_shader_pair({
                    .name = "mesh_vertex_pull/mesh_vertex_pull",
                    .vertex_path =
                        "shaders/mesh_vertex_pull/mesh_vertex_pull_vs.hlsl",
                    .pixel_path =
                        "shaders/mesh_vertex_pull/mesh_vertex_pull_ps.hlsl",
                    .vertex_entry = "main",
                    .pixel_entry = "main",
                    .vertex_target = "vs_5_0",
                    .pixel_target = "ps_5_0",
                });
            if (!shaders.valid()) {
                return {};
            }

            return assets.render_programs().create_custom({
                .name = "mesh_vertex_pull/mesh_vertex_pull",
                .vertex_shader = shaders.vertex_shader,
                .pixel_shader = shaders.pixel_shader,
                .binding_model = RenderBindingModel::MeshVertexPull,
                .topology = RenderPrimitiveTopology::TriangleList,
                .default_domain = RenderDomain::Opaque,
                .default_policy_flags =
                    RenderPolicy_DepthTest | RenderPolicy_DepthWrite,
                .input_layout = InputLayoutKind::None,
                .blend_mode = wz::rhi::BlendMode::Opaque,
                .depth_mode = DepthMode::TestWrite,
                .raster_mode = RasterMode::SolidCullNone,
                .root_constants = {{
                    .visibility = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space = 0,
                    .value_count = 40,
                }},
                .descriptor_bindings = {
                    DescriptorBinding{
                        .kind = DescriptorKind::StructuredBufferSRV,
                        .visibility = ShaderVisibility::Vertex,
                        .semantic = DescriptorSemantic::PulledMeshPositions,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                    },
                    DescriptorBinding{
                        .kind = DescriptorKind::StructuredBufferSRV,
                        .visibility = ShaderVisibility::Vertex,
                        .semantic = DescriptorSemantic::PulledMeshIndices,
                        .shader_register = 1,
                        .register_space = 0,
                        .descriptor_count = 1,
                    },
                },
            });
        }

        RenderProgramAsset create_builtin_mesh_vertex_pull_mask_program(
            EngineAssetLibrary& assets)
        {
            const ShaderPairAsset shaders =
                assets.shaders().create_shader_pair({
                    .name = "mesh_vertex_pull/mesh_vertex_pull_mask",
                    .vertex_path =
                        "shaders/mesh_vertex_pull/"
                        "mesh_vertex_pull_mask_vs.hlsl",
                    .pixel_path =
                        "shaders/mesh_mask_style/mesh_mask_style_ps.hlsl",
                    .vertex_entry = "main",
                    .pixel_entry = "main",
                    .vertex_target = "vs_5_0",
                    .pixel_target = "ps_5_0",
                });
            if (!shaders.valid()) {
                return {};
            }

            return assets.render_programs().create_custom({
                .name = "mesh_vertex_pull/mesh_vertex_pull_mask",
                .vertex_shader = shaders.vertex_shader,
                .pixel_shader = shaders.pixel_shader,
                .binding_model = RenderBindingModel::MeshVertexPull,
                .topology = RenderPrimitiveTopology::TriangleList,
                .default_domain = RenderDomain::Opaque,
                .default_policy_flags =
                    RenderPolicy_DepthTest | RenderPolicy_DepthWrite,
                .input_layout = InputLayoutKind::None,
                .blend_mode = wz::rhi::BlendMode::Opaque,
                .depth_mode = DepthMode::TestWrite,
                .raster_mode = RasterMode::SolidCullNone,
                .root_constants = {{
                    .visibility = ShaderVisibility::All,
                    .shader_register = 0,
                    .register_space = 0,
                    .value_count = 48,
                }},
                .descriptor_bindings = {
                    DescriptorBinding{
                        .kind = DescriptorKind::StructuredBufferSRV,
                        .visibility = ShaderVisibility::Vertex,
                        .semantic = DescriptorSemantic::PulledMeshPositions,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                    },
                    DescriptorBinding{
                        .kind = DescriptorKind::StructuredBufferSRV,
                        .visibility = ShaderVisibility::Vertex,
                        .semantic = DescriptorSemantic::PulledMeshIndices,
                        .shader_register = 1,
                        .register_space = 0,
                        .descriptor_count = 1,
                    },
                    DescriptorBinding{
                        .kind = DescriptorKind::StructuredBufferSRV,
                        .visibility = ShaderVisibility::Pixel,
                        .semantic = DescriptorSemantic::MeshFieldVisualization,
                        .shader_register = 0,
                        .register_space = 0,
                        .descriptor_count = 1,
                    },
                    DescriptorBinding{
                        .kind = DescriptorKind::StructuredBufferSRV,
                        .visibility = ShaderVisibility::Pixel,
                        .semantic = DescriptorSemantic::MeshMaskRules,
                        .shader_register = 1,
                        .register_space = 0,
                        .descriptor_count = 1,
                    },
                },
            });
        }

        ComputeBindingKind compute_binding_kind_for_scene_port(
            const SceneComputeKernelPortAsset& port)
        {
            if (port.binding_kind == SceneComputeKernelBindingKind::UAV) {
                return ComputeBindingKind::StructuredBufferUAV;
            }
            return ComputeBindingKind::StructuredBufferSRV;
        }

        uint32_t compute_kernel_root_constant_dwords(
            const SceneComputeKernelAsset& kernel)
        {
            uint32_t total = 0;
            for (const auto& port : kernel.ports) {
                if (port.kind
                    == SceneComputeKernelPortKind::StructuredBuffer)
                {
                    continue;
                }

                const uint64_t end =
                    static_cast<uint64_t>(port.root_constant_offset)
                    + static_cast<uint64_t>(port.root_constant_dwords);
                if (end > static_cast<uint64_t>(total)) {
                    total = static_cast<uint32_t>(end);
                }
            }
            return total;
        }

        std::vector<ComputeBindingDesc> compute_bindings_from_hlsl(
            const HlslBindingExtraction& extraction)
        {
            std::vector<ComputeBindingDesc> bindings;
            for (const HlslBindingPort& port : extraction.ports) {
                if (port.target != HlslBindingPortTarget::Buffer) {
                    continue;
                }
                bindings.push_back(ComputeBindingDesc{
                    .kind = port.binding_kind,
                    .semantic = ComputeBindingSemantic::Unknown,
                    .shader_register = port.shader_register,
                    .register_space = port.register_space,
                    .descriptor_count = 1,
                    .stride_bytes = port.stride_bytes,
                });
            }
            return bindings;
        }

        std::vector<ComputeBindingDesc> compute_bindings_from_scene_ports(
            const SceneComputeKernelAsset& kernel)
        {
            std::vector<ComputeBindingDesc> bindings;
            bindings.reserve(kernel.ports.size());
            for (const auto& port : kernel.ports) {
                if (port.kind
                    != SceneComputeKernelPortKind::StructuredBuffer)
                {
                    continue;
                }

                bindings.push_back(ComputeBindingDesc{
                    .kind = compute_binding_kind_for_scene_port(port),
                    .semantic = ComputeBindingSemantic::Unknown,
                    .shader_register = port.shader_register,
                    .register_space = port.register_space,
                    .descriptor_count = 1,
                    .stride_bytes = port.stride_bytes,
                });
            }
            return bindings;
        }

        bool materialize_compute_kernel(
            EngineAssetLibrary& assets,
            SceneNodeAsset& node,
            std::string& error)
        {
            if (!node.compute_kernel) {
                return true;
            }

            SceneComputeKernelAsset& kernel = *node.compute_kernel;
            const std::string node_name =
                !node.id.empty() ? node.id : node.name;

            if (kernel.kernel_id.empty()) {
                error = "compute kernel missing kernel_id for " + node_name;
                return false;
            }
            if (kernel.hlsl_path.empty()) {
                error = "compute kernel missing hlsl_path for " + node_name;
                return false;
            }

            std::vector<ComputeBindingDesc> bindings;
            uint32_t root_constant_dwords = 0u;
            if (kernel.ports.empty()) {
                const std::string shader_path =
                    wz::fs::is_absolute(kernel.hlsl_path)
                        ? kernel.hlsl_path
                        : wz::fs::join(
                            assets.resource_root(),
                            kernel.hlsl_path);
                const HlslBindingExtraction extraction =
                    extract_hlsl_bindings_from_file(shader_path);
                if (!extraction.ok()) {
                    error = "compute kernel shader binding extraction failed for "
                        + node_name + ": " + extraction.diagnostics.front();
                    return false;
                }
                bindings = compute_bindings_from_hlsl(extraction);
                root_constant_dwords = extraction.root_constant_dwords;
            }
            else {
                bindings = compute_bindings_from_scene_ports(kernel);
                root_constant_dwords =
                    compute_kernel_root_constant_dwords(kernel);
            }

            const ComputeShaderAsset shader =
                assets.shaders().create_compute_shader({
                    .name = kernel.kernel_id,
                    .path = kernel.hlsl_path,
                    .entry = kernel.entry,
                    .target = kernel.target,
                });
            if (!shader.valid()) {
                error = "compute shader unavailable for " + node_name;
                return false;
            }

            const ComputePipelineAsset pipeline =
                assets.compute_pipelines().create_compute_pipeline({
                    .name = kernel.kernel_id,
                    .compute_shader = shader.shader,
                    .bindings = std::move(bindings),
                    .root_constant_dwords = root_constant_dwords,
                    .thread_group_size_x = kernel.thread_group_size_x,
                    .thread_group_size_y = kernel.thread_group_size_y,
                    .thread_group_size_z = kernel.thread_group_size_z,
                });
            if (!pipeline.valid()) {
                error = "compute pipeline unavailable for " + node_name;
                return false;
            }

            kernel.compute_shader_asset = shader.shader;
            kernel.compute_pipeline_asset = pipeline.key;
            return true;
        }

        ComputeBindingDesc compute_binding_for_mesh_compute_input(
            MeshComputeInput input,
            uint32_t shader_register)
        {
            ComputeBindingDesc binding{
                .kind = ComputeBindingKind::StructuredBufferSRV,
                .semantic = ComputeBindingSemantic::Unknown,
                .shader_register = shader_register,
                .register_space = 0,
                .descriptor_count = 1,
                .stride_bytes = 0,
            };
            switch (input) {
            case MeshComputeInput::Positions:
            case MeshComputeInput::Normals:
                binding.stride_bytes = sizeof(float) * 3u;
                break;
            case MeshComputeInput::UV0:
                binding.stride_bytes = sizeof(float) * 2u;
                break;
            case MeshComputeInput::Indices:
                binding.semantic = ComputeBindingSemantic::MeshIndices;
                binding.stride_bytes = sizeof(uint32_t);
                break;
            case MeshComputeInput::Vertices:
                binding.semantic = ComputeBindingSemantic::MeshVertices;
                binding.stride_bytes = sizeof(MeshVertex);
                break;
            }
            return binding;
        }

        // Materializes an authored mesh_compute_field component into a
        // registered compute-derived-field asset on the node's mesh. The
        // produced field is a first-class cached asset regardless of whether
        // the node's render style visualizes one of its channels.
        bool materialize_mesh_compute_field(
            EngineAssetLibrary& assets,
            SceneNodeAsset& node,
            MeshAsset mesh,
            std::string& error)
        {
            if (!node.mesh_compute_field
                || !node.mesh_compute_field->enabled)
            {
                return true;
            }

            SceneMeshComputeFieldAsset& field = *node.mesh_compute_field;
            const std::string node_name =
                !node.id.empty() ? node.id : node.name;

            if (field.kernel_id.empty()) {
                error = "mesh compute field missing kernel_id for "
                    + node_name;
                return false;
            }
            if (field.hlsl_path.empty()) {
                error = "mesh compute field missing hlsl_path for "
                    + node_name;
                return false;
            }
            if (field.channels.empty()) {
                error = "mesh compute field missing channels for "
                    + node_name;
                return false;
            }
            if (!mesh.valid()) {
                error = "mesh compute field requires a mesh for "
                    + node_name;
                return false;
            }

            const ComputeShaderAsset shader =
                assets.shaders().create_compute_shader({
                    .name = field.kernel_id,
                    .path = field.hlsl_path,
                    .entry = field.entry,
                    .target = field.target,
                });
            if (!shader.valid()) {
                error = "mesh compute field shader unavailable for "
                    + node_name;
                return false;
            }

            std::vector<ComputeBindingDesc> bindings;
            bindings.reserve(field.inputs.size() + 1u);
            for (uint32_t i = 0;
                i < static_cast<uint32_t>(field.inputs.size());
                ++i)
            {
                bindings.push_back(compute_binding_for_mesh_compute_input(
                    field.inputs[i],
                    i));
            }
            bindings.push_back(ComputeBindingDesc{
                .kind = ComputeBindingKind::StructuredBufferUAV,
                .semantic = ComputeBindingSemantic::MeshDerivedFieldValues,
                .shader_register = 0,
                .register_space = 0,
                .descriptor_count = 1,
                .stride_bytes = sizeof(uint32_t),
            });

            // Three engine-filled dwords (vertex_count, index_count,
            // triangle_count) precede the authored params at dispatch.
            const ComputePipelineAsset pipeline =
                assets.compute_pipelines().create_compute_pipeline({
                    .name = field.kernel_id,
                    .compute_shader = shader.shader,
                    .bindings = std::move(bindings),
                    .root_constant_dwords =
                        3u + static_cast<uint32_t>(field.params.size()),
                    .thread_group_size_x = field.thread_group_size_x,
                    .thread_group_size_y = field.thread_group_size_y,
                    .thread_group_size_z = field.thread_group_size_z,
                });
            if (!pipeline.valid()) {
                error = "mesh compute field pipeline unavailable for "
                    + node_name;
                return false;
            }

            std::vector<MeshDerivedFieldChannelDesc> channels;
            channels.reserve(field.channels.size());
            std::ranges::transform(
                field.channels,
                std::back_inserter(channels),
                [](const auto& channel)
                {
                    return MeshDerivedFieldChannelDesc{
                    .channel_id = channel.channel_id,
                    .value_type = channel.value_type,
                    };
                });

            const MeshDerivedFieldAsset field_asset =
                assets.mesh_derived_fields().create_compute_derived_field({
                    .name = node_name + "_compute_field",
                    .source_mesh = mesh,
                    .compute_pipeline = pipeline,
                    .domain = MeshDerivedFieldDomain::Vertex,
                    .channels = std::move(channels),
                    .inputs = field.inputs,
                    .root_constants = field.params,
                });
            if (!field_asset.valid()) {
                error = "mesh compute field asset unavailable for "
                    + node_name;
                return false;
            }

            field.field_asset = field_asset.output;
            return true;
        }

        BuiltinMeshDerivedFieldSourceKind builtin_source_kind_for_scene(
            SceneMeshDerivedFieldSourceKind kind) noexcept
        {
            switch (kind) {
            case SceneMeshDerivedFieldSourceKind::Constant:
                return BuiltinMeshDerivedFieldSourceKind::Constant;
            case SceneMeshDerivedFieldSourceKind::PositionGradient:
                return BuiltinMeshDerivedFieldSourceKind::PositionGradient;
            case SceneMeshDerivedFieldSourceKind::VertexIndexGradient:
                return BuiltinMeshDerivedFieldSourceKind::VertexIndexGradient;
            case SceneMeshDerivedFieldSourceKind::TriangleCornerCount:
                return BuiltinMeshDerivedFieldSourceKind::TriangleCornerCount;
            case SceneMeshDerivedFieldSourceKind::VertexArea:
                return BuiltinMeshDerivedFieldSourceKind::VertexArea;
            case SceneMeshDerivedFieldSourceKind::TriangleArea:
                return BuiltinMeshDerivedFieldSourceKind::TriangleArea;
            case SceneMeshDerivedFieldSourceKind::MeanEdgeLength:
                return BuiltinMeshDerivedFieldSourceKind::MeanEdgeLength;
            case SceneMeshDerivedFieldSourceKind::InverseAreaDensity:
                return BuiltinMeshDerivedFieldSourceKind::InverseAreaDensity;
            case SceneMeshDerivedFieldSourceKind::LogDensity:
                return BuiltinMeshDerivedFieldSourceKind::LogDensity;
            }
            return BuiltinMeshDerivedFieldSourceKind::PositionGradient;
        }

        BuiltinMeshDerivedFieldComponent builtin_component_for_scene(
            SceneMeshDerivedFieldComponent component) noexcept
        {
            switch (component) {
            case SceneMeshDerivedFieldComponent::X:
                return BuiltinMeshDerivedFieldComponent::X;
            case SceneMeshDerivedFieldComponent::Y:
                return BuiltinMeshDerivedFieldComponent::Y;
            case SceneMeshDerivedFieldComponent::Z:
                return BuiltinMeshDerivedFieldComponent::Z;
            }
            return BuiltinMeshDerivedFieldComponent::Y;
        }

        std::string canonical_mesh_field_ref(
            std::string_view node_id,
            std::string_view field_id)
        {
            return "node:" + std::string(node_id)
                + "/field:" + std::string(field_id);
        }

        std::string canonical_mesh_field_ref_for_node(
            const SceneNodeAsset& node,
            std::string_view field_ref)
        {
            constexpr std::string_view kNodePrefix = "node:";
            constexpr std::string_view kFieldPrefix = "field:";

            if (field_ref.starts_with(kNodePrefix)) {
                return std::string(field_ref);
            }
            if (field_ref.starts_with(kFieldPrefix)) {
                field_ref.remove_prefix(kFieldPrefix.size());
            }
            return canonical_mesh_field_ref(node.id, field_ref);
        }

        std::string canonical_mesh_operator_ref(
            std::string_view node_id,
            std::string_view operator_id)
        {
            return "node:" + std::string(node_id)
                + "/operator:" + std::string(operator_id);
        }

        std::string canonical_mesh_operator_ref_for_node(
            const SceneNodeAsset& node,
            std::string_view operator_ref)
        {
            constexpr std::string_view kNodePrefix = "node:";
            constexpr std::string_view kOperatorPrefix = "operator:";

            if (operator_ref.starts_with(kNodePrefix)) {
                return std::string(operator_ref);
            }
            if (operator_ref.starts_with(kOperatorPrefix)) {
                operator_ref.remove_prefix(kOperatorPrefix.size());
            }
            return canonical_mesh_operator_ref(node.id, operator_ref);
        }

        MeshSparseApplyMode mesh_sparse_apply_mode_for_scene(
            SceneMeshSparseApplyMode mode) noexcept
        {
            switch (mode) {
            case SceneMeshSparseApplyMode::Residual:
                return MeshSparseApplyMode::Residual;
            }
            return MeshSparseApplyMode::Residual;
        }

        MeshSparseDiffusionMode mesh_sparse_diffusion_mode_for_scene(
            SceneMeshSparseDiffusionMode mode) noexcept
        {
            switch (mode) {
            case SceneMeshSparseDiffusionMode::Smooth:
                return MeshSparseDiffusionMode::Smooth;
            case SceneMeshSparseDiffusionMode::DiffusionStep:
                return MeshSparseDiffusionMode::DiffusionStep;
            }
            return MeshSparseDiffusionMode::Smooth;
        }

        bool infer_sparse_operator_domain_from_consumers(
            const SceneNodeAsset& node,
            const MeshFieldRefCache& field_refs,
            const SceneMeshSparseOperatorSourceAsset& source,
            MeshOperatorDomain& domain,
            std::string& error)
        {
            const std::string node_name =
                !node.id.empty() ? node.id : node.name;
            const std::string local_operator_ref =
                canonical_mesh_operator_ref(node.id, source.operator_id);

            bool saw_domain = false;
            auto consume = [&](std::string_view consumer_name,
                               std::string_view operator_ref,
                               std::string_view input_field_ref) -> bool
            {
                if (operator_ref.empty() || input_field_ref.empty()) {
                    return true;
                }
                if (canonical_mesh_operator_ref_for_node(node, operator_ref)
                    != local_operator_ref)
                {
                    return true;
                }

                const std::string canonical_field_ref =
                    canonical_mesh_field_ref_for_node(node, input_field_ref);
                const auto field_found = field_refs.find(canonical_field_ref);
                if (field_found == field_refs.end()) {
                    return true;
                }
                const MeshOperatorDomain inferred =
                    field_found->second.domain;
                if (inferred != MeshOperatorDomain::Vertex
                    && inferred != MeshOperatorDomain::Face)
                {
                    error =
                        "mesh sparse operator source cannot infer domain "
                        "from unsupported "
                        + std::string(consumer_name)
                        + " input for " + node_name
                        + ": input_ref=" + std::string(input_field_ref)
                        + " input{"
                        + describe_field_ref(
                            canonical_field_ref,
                            field_found->second)
                        + "}";
                    return false;
                }
                if (saw_domain && domain != inferred) {
                    error =
                        "mesh sparse operator source has conflicting "
                        "consumer field domains for "
                        + node_name
                        + ": previous_domain="
                        + mesh_operator_domain_name(domain)
                        + " " + std::string(consumer_name)
                        + "_domain="
                        + mesh_operator_domain_name(inferred)
                        + " input_ref=" + std::string(input_field_ref)
                        + " input{"
                        + describe_field_ref(
                            canonical_field_ref,
                            field_found->second)
                        + "}";
                    return false;
                }
                domain = inferred;
                saw_domain = true;
                return true;
            };

            if (node.mesh_sparse_apply_field
                && node.mesh_sparse_apply_field->enabled
                && !consume(
                    "mesh_sparse_apply_field",
                    node.mesh_sparse_apply_field->operator_ref,
                    node.mesh_sparse_apply_field->input_field_ref))
            {
                return false;
            }

            if (node.mesh_sparse_diffusion_bands
                && node.mesh_sparse_diffusion_bands->enabled
                && !consume(
                    "mesh_sparse_diffusion_bands",
                    node.mesh_sparse_diffusion_bands->operator_ref,
                    node.mesh_sparse_diffusion_bands->input_field_ref))
            {
                return false;
            }

            return true;
        }

        bool materialize_mesh_derived_field_source(
            EngineAssetLibrary& assets,
            SceneNodeAsset& node,
            MeshAsset mesh,
            MeshFieldRefCache& field_refs,
            std::string& error)
        {
            if (!node.mesh_derived_field_source
                || !node.mesh_derived_field_source->enabled)
            {
                return true;
            }

            SceneMeshDerivedFieldSourceAsset& source =
                *node.mesh_derived_field_source;
            const std::string node_name =
                !node.id.empty() ? node.id : node.name;

            if (source.field_id.empty()) {
                error = "mesh derived field source missing field_id for "
                    + node_name;
                return false;
            }
            if (source.channel_id == 0u) {
                error = "mesh derived field source has invalid channel_id for "
                    + node_name;
                return false;
            }
            if (!mesh.valid()) {
                error = "mesh derived field source requires a mesh for "
                    + node_name;
                return false;
            }
            if (source.domain != MeshDerivedFieldDomain::Vertex
                && source.domain != MeshDerivedFieldDomain::Face)
            {
                error =
                    "mesh derived field source only supports vertex or "
                    "face domain for "
                    + node_name;
                return false;
            }
            if (source.value_type != MeshDerivedFieldValueType::Float1) {
                error = "mesh derived field source only supports Float1 for "
                    + node_name;
                return false;
            }

            const MeshDerivedFieldAsset field_asset =
                assets.mesh_derived_fields().create_builtin_field({
                    .name = node_name + "_field_" + source.field_id,
                    .source_mesh = mesh,
                    .domain = source.domain,
                    .channel_id = source.channel_id,
                    .value_type = source.value_type,
                    .source_kind =
                        builtin_source_kind_for_scene(source.source_kind),
                    .component = builtin_component_for_scene(source.component),
                    .normalize = source.normalize,
                    .constant_value = source.constant_value,
                });
            if (!field_asset.valid()) {
                error = "mesh derived field source asset unavailable for "
                    + node_name;
                return false;
            }

            source.resolved_field_asset = field_asset.output;
            field_refs[canonical_mesh_field_ref(node.id, source.field_id)] =
                MeshFieldRefEntry{
                    .asset = field_asset.output,
                    .domain = source.domain,
                    .label = "mesh_derived_field_source field:"
                        + source.field_id,
                };
            return true;
        }

        bool materialize_mesh_sparse_operator_source(
            EngineAssetLibrary& assets,
            SceneNodeAsset& node,
            MeshAsset mesh,
            const MeshFieldRefCache& field_refs,
            MeshOperatorRefCache& operator_refs,
            std::string& error)
        {
            if (!node.mesh_sparse_operator_source
                || !node.mesh_sparse_operator_source->enabled)
            {
                return true;
            }

            SceneMeshSparseOperatorSourceAsset& source =
                *node.mesh_sparse_operator_source;
            const std::string node_name =
                !node.id.empty() ? node.id : node.name;

            if (source.operator_id.empty()) {
                error = "mesh sparse operator source missing operator_id for "
                    + node_name;
                return false;
            }
            if (!mesh.valid()) {
                error = "mesh sparse operator source requires a mesh for "
                    + node_name;
                return false;
            }
            if (source.kind
                != MeshSparseOperatorKind::UniformVertexLaplacian)
            {
                error = "mesh sparse operator source only supports "
                    "UniformVertexLaplacian for " + node_name;
                return false;
            }
            if (source.domain != MeshOperatorDomain::Vertex
                && source.domain != MeshOperatorDomain::Face)
            {
                error =
                    "mesh sparse operator source only supports vertex or "
                    "face domain "
                    "for " + node_name;
                return false;
            }
            if (source.value_convention
                != MeshSparseOperatorValueConvention::NeighborWeights)
            {
                error = "mesh sparse operator source only supports "
                    "NeighborWeights for " + node_name;
                return false;
            }

            MeshOperatorDomain effective_domain = source.domain;
            if (!infer_sparse_operator_domain_from_consumers(
                    node,
                    field_refs,
                    source,
                    effective_domain,
                    error))
            {
                return false;
            }
            source.domain = effective_domain;

            const MeshSparseOperatorAsset operator_asset =
                assets.mesh_sparse_operators().create_sparse_operator({
                    .name =
                        node_name + "_operator_" + source.operator_id,
                    .source_mesh = mesh,
                    .kind = source.kind,
                    .domain = effective_domain,
                });
            if (!operator_asset.valid()) {
                error = "mesh sparse operator source asset unavailable for "
                    + node_name;
                return false;
            }

            source.resolved_operator_asset = operator_asset.output;
            operator_refs[canonical_mesh_operator_ref(
                node.id,
                source.operator_id)] = MeshOperatorRefEntry{
                    .asset = operator_asset.output,
                    .domain = effective_domain,
                    .label = "mesh_sparse_operator_source operator:"
                        + source.operator_id,
                };
            return true;
        }

        bool materialize_mesh_sparse_apply_field(
            EngineAssetLibrary& assets,
            SceneNodeAsset& node,
            MeshAsset mesh,
            MeshFieldRefCache& field_refs,
            const MeshOperatorRefCache& operator_refs,
            std::string& error)
        {
            if (!node.mesh_sparse_apply_field
                || !node.mesh_sparse_apply_field->enabled)
            {
                return true;
            }

            SceneMeshSparseApplyFieldAsset& field =
                *node.mesh_sparse_apply_field;
            const std::string node_name =
                !node.id.empty() ? node.id : node.name;

            if (!mesh.valid()) {
                error = "mesh sparse apply field requires a mesh for "
                    + node_name;
                return false;
            }
            if (field.operator_ref.empty()) {
                error = "mesh sparse apply field missing operator_ref for "
                    + node_name;
                return false;
            }
            if (field.input_field_ref.empty()) {
                error = "mesh sparse apply field missing input_field_ref for "
                    + node_name;
                return false;
            }
            if (field.input_channel_id == 0u
                || field.output_channel_id == 0u)
            {
                error = "mesh sparse apply field has invalid channel ids for "
                    + node_name;
                return false;
            }

            const std::string canonical_field_ref =
                canonical_mesh_field_ref_for_node(
                    node,
                    field.input_field_ref);
            const auto field_found = field_refs.find(canonical_field_ref);
            if (field_found == field_refs.end()) {
                error = "mesh sparse apply field input ref not found for "
                    + node_name + ": " + field.input_field_ref;
                return false;
            }

            const std::string canonical_operator =
                canonical_mesh_operator_ref_for_node(
                    node,
                    field.operator_ref);
            const auto operator_found =
                operator_refs.find(canonical_operator);
            if (operator_found == operator_refs.end()) {
                error = "mesh sparse apply field operator ref not found for "
                    + node_name + ": " + field.operator_ref;
                return false;
            }
            if (field_found->second.domain != operator_found->second.domain) {
                error =
                    "mesh sparse apply field input domain does not match "
                    "operator domain for "
                    + node_name
                    + ": input_ref=" + field.input_field_ref
                    + " input{"
                    + describe_field_ref(
                        canonical_field_ref,
                        field_found->second)
                    + "} operator_ref=" + field.operator_ref
                    + " operator{"
                    + describe_operator_ref(
                        canonical_operator,
                        operator_found->second)
                    + "}";
                return false;
            }

            const MeshDerivedFieldAsset output =
                assets.mesh_derived_fields().create_sparse_apply_field({
                    .name = node_name + "_sparse_apply_field",
                    .source_mesh = mesh,
                    .sparse_operator =
                        MeshSparseOperatorAsset{
                            .output = operator_found->second.asset,
                        },
                    .input_field =
                        MeshDerivedFieldAsset{
                            .output = field_found->second.asset,
                        },
                    .input_channel_id = field.input_channel_id,
                    .output_channel_id = field.output_channel_id,
                    .apply_mode =
                        mesh_sparse_apply_mode_for_scene(field.apply_mode),
                });
            if (!output.valid()) {
                error = "mesh sparse apply field asset unavailable for "
                    + node_name;
                return false;
            }

            field.output_field_asset = output.output;
            field_refs[canonical_mesh_field_ref(
                node.id,
                "sparse_apply")] = MeshFieldRefEntry{
                    .asset = output.output,
                    .domain = operator_found->second.domain,
                    .label = "mesh_sparse_apply_field output field:"
                        "sparse_apply",
                };
            field_refs[canonical_mesh_field_ref(
                node.id,
                "residual")] = MeshFieldRefEntry{
                    .asset = output.output,
                    .domain = operator_found->second.domain,
                    .label = "mesh_sparse_apply_field output field:"
                        "residual",
                };
            return true;
        }

        bool materialize_mesh_sparse_diffusion_bands(
            EngineAssetLibrary& assets,
            SceneNodeAsset& node,
            MeshAsset mesh,
            MeshFieldRefCache& field_refs,
            const MeshOperatorRefCache& operator_refs,
            std::string& error)
        {
            if (!node.mesh_sparse_diffusion_bands
                || !node.mesh_sparse_diffusion_bands->enabled)
            {
                return true;
            }

            SceneMeshSparseDiffusionBandsAsset& bands =
                *node.mesh_sparse_diffusion_bands;
            const std::string node_name =
                !node.id.empty() ? node.id : node.name;

            if (!mesh.valid()) {
                error = "mesh sparse diffusion bands requires a mesh for "
                    + node_name;
                return false;
            }
            if (bands.operator_ref.empty()) {
                error = "mesh sparse diffusion bands missing operator_ref for "
                    + node_name;
                return false;
            }
            if (bands.input_field_ref.empty()) {
                error =
                    "mesh sparse diffusion bands missing input_field_ref for "
                    + node_name;
                return false;
            }
            if (bands.input_channel_id == 0u
                || bands.output_base_channel_id == 0u
                || bands.band_count == 0u
                || bands.iterations_per_band == 0u
                || !std::isfinite(bands.tau)
                || bands.tau < 0.0f)
            {
                error =
                    "mesh sparse diffusion bands has invalid parameters for "
                    + node_name;
                return false;
            }

            const std::string canonical_field_ref =
                canonical_mesh_field_ref_for_node(
                    node,
                    bands.input_field_ref);
            const auto field_found = field_refs.find(canonical_field_ref);
            if (field_found == field_refs.end()) {
                error = "mesh sparse diffusion bands input ref not found for "
                    + node_name + ": " + bands.input_field_ref;
                return false;
            }

            const std::string canonical_operator =
                canonical_mesh_operator_ref_for_node(
                    node,
                    bands.operator_ref);
            const auto operator_found =
                operator_refs.find(canonical_operator);
            if (operator_found == operator_refs.end()) {
                error =
                    "mesh sparse diffusion bands operator ref not found for "
                    + node_name + ": " + bands.operator_ref;
                return false;
            }
            if (field_found->second.domain != operator_found->second.domain) {
                error =
                    "mesh sparse diffusion bands input domain does not match "
                    "operator domain for "
                    + node_name
                    + ": input_ref=" + bands.input_field_ref
                    + " input{"
                    + describe_field_ref(
                        canonical_field_ref,
                        field_found->second)
                    + "} operator_ref=" + bands.operator_ref
                    + " operator{"
                    + describe_operator_ref(
                        canonical_operator,
                        operator_found->second)
                    + "}";
                return false;
            }

            const MeshDerivedFieldAsset output =
                assets.mesh_derived_fields().create_sparse_diffusion_bands({
                    .name = node_name + "_sparse_diffusion_bands",
                    .source_mesh = mesh,
                    .sparse_operator =
                        MeshSparseOperatorAsset{
                            .output = operator_found->second.asset,
                        },
                    .input_field =
                        MeshDerivedFieldAsset{
                            .output = field_found->second.asset,
                        },
                    .input_channel_id = bands.input_channel_id,
                    .output_base_channel_id =
                        bands.output_base_channel_id,
                    .band_count = bands.band_count,
                    .iterations_per_band = bands.iterations_per_band,
                    .mode =
                        mesh_sparse_diffusion_mode_for_scene(bands.mode),
                    .tau = bands.tau,
                });
            if (!output.valid()) {
                error =
                    "mesh sparse diffusion bands asset unavailable for "
                    + node_name;
                return false;
            }

            bands.output_field_asset = output.output;
            field_refs[canonical_mesh_field_ref(
                node.id,
                "diffusion_bands")] = MeshFieldRefEntry{
                    .asset = output.output,
                    .domain = operator_found->second.domain,
                    .label = "mesh_sparse_diffusion_bands output field:"
                        "diffusion_bands",
                };
            field_refs[canonical_mesh_field_ref(
                node.id,
                "topology_irregularity")] = MeshFieldRefEntry{
                    .asset = output.output,
                    .domain = operator_found->second.domain,
                    .label = "mesh_sparse_diffusion_bands output field:"
                        "topology_irregularity",
                };
            for (uint32_t band = 0; band < bands.band_count; ++band) {
                field_refs[canonical_mesh_field_ref(
                    node.id,
                    "diffusion_band" + std::to_string(band))] =
                    MeshFieldRefEntry{
                        .asset = output.output,
                        .domain = operator_found->second.domain,
                        .label = "mesh_sparse_diffusion_bands output field:"
                            "diffusion_band" + std::to_string(band),
                    };
            }
            return true;
        }

        bool materialize_mesh_level_mask_source(
            EngineAssetLibrary& assets,
            SceneNodeAsset& node,
            MeshAsset mesh,
            MeshFieldRefCache& field_refs,
            std::string& error)
        {
            if (!node.mesh_level_mask_source
                || !node.mesh_level_mask_source->enabled)
            {
                return true;
            }

            SceneMeshLevelMaskSourceAsset& masks =
                *node.mesh_level_mask_source;
            const std::string node_name =
                !node.id.empty() ? node.id : node.name;

            if (!mesh.valid()) {
                error = "mesh level mask source requires a mesh for "
                    + node_name;
                return false;
            }
            if (masks.input_field_ref.empty()) {
                error = "mesh level mask source missing input_field_ref for "
                    + node_name;
                return false;
            }
            if (masks.output_field_id.empty()) {
                error = "mesh level mask source missing output_field_id for "
                    + node_name;
                return false;
            }
            if (masks.domain != MeshDerivedFieldDomain::Vertex
                && masks.domain != MeshDerivedFieldDomain::Face)
            {
                error =
                    "mesh level mask source only supports vertex or "
                    "face domain for "
                    + node_name;
                return false;
            }
            if (masks.regions.empty()) {
                error = "mesh level mask source requires at least one region for "
                    + node_name;
                return false;
            }

            const std::string canonical_field_ref =
                canonical_mesh_field_ref_for_node(
                    node,
                    masks.input_field_ref);
            const auto field_found = field_refs.find(canonical_field_ref);
            if (field_found == field_refs.end()) {
                error = "mesh level mask input ref not found for "
                    + node_name + ": " + masks.input_field_ref;
                return false;
            }

            const MeshDerivedFieldDomain inferred_domain =
                field_found->second.domain;
            if (inferred_domain != MeshDerivedFieldDomain::Vertex
                && inferred_domain != MeshDerivedFieldDomain::Face)
            {
                error =
                    "mesh level mask source cannot infer supported domain "
                    "from input field for "
                    + node_name
                    + ": input_ref=" + masks.input_field_ref
                    + " input{"
                    + describe_field_ref(
                        canonical_field_ref,
                        field_found->second)
                    + " output_field_id=" + masks.output_field_id;
                return false;
            }
            masks.domain = inferred_domain;

            std::vector<MeshFieldLevelMaskRegionDesc> regions;
            regions.reserve(masks.regions.size());
            for (const SceneMeshLevelMaskRegionAsset& region :
                 masks.regions)
            {
                if (region.input_channel_id == 0u
                    || region.output_channel_id == 0u
                    || !std::isfinite(region.min_value)
                    || !std::isfinite(region.max_value))
                {
                    error =
                        "mesh level mask source has invalid region for "
                        + node_name;
                    return false;
                }
                regions.push_back(MeshFieldLevelMaskRegionDesc{
                    .input_channel_id = region.input_channel_id,
                    .output_channel_id = region.output_channel_id,
                    .min_value = region.min_value,
                    .max_value = region.max_value,
                });
            }

            const MeshDerivedFieldAsset output =
                assets.mesh_derived_fields().create_field_level_mask({
                    .name =
                        node_name + "_level_mask_" + masks.output_field_id,
                    .source_mesh = mesh,
                    .input_field =
                        MeshDerivedFieldAsset{
                            .output = field_found->second.asset,
                        },
                    .domain = masks.domain,
                    .regions = std::move(regions),
                });
            if (!output.valid()) {
                error = "mesh level mask asset unavailable for "
                    + node_name;
                return false;
            }

            masks.output_field_asset = output.output;
            field_refs[canonical_mesh_field_ref(
                node.id,
                masks.output_field_id)] = MeshFieldRefEntry{
                    .asset = output.output,
                    .domain = masks.domain,
                    .label = "mesh_level_mask_source output field:"
                        + masks.output_field_id,
                };
            return true;
        }

        bool resolve_mesh_field_visualization_ref(
            const SceneNodeAsset& node,
            const SceneMeshRenderStyleAsset& style,
            const MeshFieldRefCache& field_refs,
            wz::asset::AssetKey& out,
            std::string& error)
        {
            if (!style.field_visualization_enabled
                || style.field_visualization_field_ref.empty())
            {
                return true;
            }

            const std::string canonical_ref =
                canonical_mesh_field_ref_for_node(
                    node,
                    style.field_visualization_field_ref);
            const auto found = field_refs.find(canonical_ref);
            if (found == field_refs.end()) {
                const std::string node_name =
                    !node.id.empty() ? node.id : node.name;
                error = "mesh render style field_ref '"
                    + style.field_visualization_field_ref
                    + "' did not resolve on " + node_name;
                return false;
            }
            out = found->second.asset;
            return true;
        }

        bool resolve_mesh_mask_field_ref(
            const SceneNodeAsset& node,
            const SceneMeshRenderStyleAsset& style,
            const MeshFieldRefCache& field_refs,
            wz::asset::AssetKey& out,
            std::string& error)
        {
            if (!style.mask.enabled || style.mask_source_field_ref.empty()) {
                return true;
            }

            const std::string canonical_ref =
                canonical_mesh_field_ref_for_node(
                    node,
                    style.mask_source_field_ref);
            const auto found = field_refs.find(canonical_ref);
            if (found == field_refs.end()) {
                const std::string node_name =
                    !node.id.empty() ? node.id : node.name;
                error = "mesh render style mask.source_field_ref '"
                    + style.mask_source_field_ref
                    + "' did not resolve on " + node_name;
                return false;
            }
            out = found->second.asset;
            return true;
        }

        bool resolve_mesh_region_set_field_ref(
            const SceneNodeAsset& node,
            const MeshFieldRefCache& field_refs,
            SceneMeshRegionSetAsset& region_set,
            std::string& error)
        {
            if (!region_set.enabled || region_set.source_field_ref.empty()) {
                return true;
            }

            const std::string canonical_ref =
                canonical_mesh_field_ref_for_node(
                    node,
                    region_set.source_field_ref);
            const auto found = field_refs.find(canonical_ref);
            if (found == field_refs.end()) {
                const std::string node_name =
                    !node.id.empty() ? node.id : node.name;
                error = "mesh region set source_field_ref '"
                    + region_set.source_field_ref
                    + "' did not resolve on " + node_name;
                return false;
            }
            region_set.source_field_asset = found->second.asset;
            return true;
        }

        bool processing_uses_region_set(
            const SceneMeshProcessingAsset& processing,
            const SceneMeshRegionSetAsset& region_set)
        {
            if (!region_set.enabled
                || region_set.intent
                    != SceneMeshRegionSetIntent::RemeshSubset
                || region_set.source_field_asset == wz::asset::AssetKey{}
                || !region_set.mask.enabled
                || region_set.mask.rules.empty()
                || processing.region_set_ref.empty())
            {
                return false;
            }

            return processing.region_set_ref == region_set.region_set_id
                || processing.region_set_ref
                    == "region_set:" + region_set.region_set_id;
        }

        const SceneMeshRegionSetAsset* region_set_for_processing(
            const SceneMeshProcessingAsset& processing,
            const SceneMeshRegionSetAsset* region_set)
        {
            if (!region_set
                || !processing_uses_region_set(processing, *region_set))
            {
                return nullptr;
            }
            return region_set;
        }

        bool mesh_compute_field_has_channel(
            const SceneMeshComputeFieldAsset& field,
            uint32_t channel_id) noexcept
        {
            return std::any_of(
                field.channels.begin(),
                field.channels.end(),
                [channel_id](const SceneMeshComputeFieldChannelAsset& channel)
                {
                    return channel.channel_id == channel_id;
                });
        }

        void normalize_implicit_mesh_field_visualization(
            const SceneNodeAsset& node,
            bool derived_field_source,
            bool sparse_apply_field_source,
            bool sparse_diffusion_bands_source,
            bool compute_field_source,
            bool behavior_field_source,
            SceneMeshRenderStyleAsset& render_style)
        {
            if (!render_style.field_visualization_enabled
                || !render_style.field_visualization_field_ref.empty())
            {
                return;
            }

            if (compute_field_source
                && node.mesh_compute_field
                && !node.mesh_compute_field->channels.empty())
            {
                if (!mesh_compute_field_has_channel(
                        *node.mesh_compute_field,
                        render_style.field_visualization_channel_id))
                {
                    render_style.field_visualization_channel_id =
                        node.mesh_compute_field->channels.front().channel_id;
                }
                return;
            }

            if (derived_field_source && node.mesh_derived_field_source) {
                render_style.field_visualization_channel_id =
                    node.mesh_derived_field_source->channel_id;
                return;
            }

            if (sparse_apply_field_source && node.mesh_sparse_apply_field) {
                render_style.field_visualization_channel_id =
                    node.mesh_sparse_apply_field->output_channel_id;
                return;
            }

            if (sparse_diffusion_bands_source
                && node.mesh_sparse_diffusion_bands)
            {
                render_style.field_visualization_channel_id =
                    node.mesh_sparse_diffusion_bands
                        ->output_base_channel_id;
                return;
            }

            if (node.mesh_wavelet_analysis
                && node.mesh_wavelet_analysis->enabled)
            {
                render_style.field_visualization_channel_id =
                    MeshWaveletChannelID::kDetailCost;
                return;
            }

            if (!behavior_field_source) {
                render_style.field_visualization_channel_id =
                    MeshWaveletChannelID::kDetailCost;
            }
        }

        // A node is a behavior mesh-field source when a compute kernel is
        // present and the authored render style enables field visualization.
        // Compute kernels without that intent must not grow render styles or
        // field assets.
        bool node_has_behavior_field_source(const SceneNodeAsset& node)
        {
            if (!node.compute_kernel) {
                return false;
            }
            return node.mesh_render_style
                && node.mesh_render_style->field_visualization_enabled;
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
            wz::asset::AssetKey render_program_asset_override,
            bool has_behavior_field_source,
            bool has_compute_field_source,
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
                && !wavelet_analysis->enabled
                && !has_behavior_field_source
                && !has_compute_field_source)
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
                const bool use_wavelet =
                    !has_behavior_field_source
                    || (wavelet_analysis && wavelet_analysis->enabled);

                MeshDerivedFieldAsset field_asset{};
                if (use_wavelet) {
                    const SceneMeshWaveletAnalysisAsset analysis =
                        wavelet_analysis ? *wavelet_analysis
                                         : SceneMeshWaveletAnalysisAsset{};
                    const ComputePipelineAsset wavelet_pipeline =
                        create_builtin_mesh_wavelet_pipeline(assets);
                    field_asset =
                        assets.mesh_derived_fields().create_wavelet_analysis({
                            .name = name + "_wavelet_field",
                            .source_mesh = mesh,
                            .compute_pipeline = wavelet_pipeline,
                            .scale_count = analysis.scale_count,
                            .lambda_max_estimate =
                                analysis.lambda_max_estimate,
                            .gamma = analysis.gamma,
                        });
                }
                else {
                    field_asset =
                        assets.mesh_derived_fields()
                            .create_behavior_field_placeholder({
                                .name = name + "_behavior_field",
                                .source_mesh = mesh,
                                .domain = MeshDerivedFieldDomain::Vertex,
                                .channel_id =
                                    effective_style
                                        .field_visualization_channel_id,
                            });
                }

                if (!field_asset.valid()) {
                    return false;
                }
                style.field_visualization_asset = field_asset.output;
                effective_style.field_visualization_asset = field_asset.output;
                if (wavelet_analysis) {
                    wavelet_analysis->field_asset = field_asset.output;
                }
            }

            // Issue #195: the legacy 0x705 styled renderable is gone; a styled
            // mesh node is an RHI pull-mesh renderable (0x706) = mesh + program
            // + the style as data. Program precedence is unchanged from the
            // 0x705 path: the mesh_vertex_pull override wins, else a
            // provisioned mesh_style program whose raster/depth/blend derive
            // from the style. The style's
            // geometry-generating parts (field_visualization, mask) are NOT
            // renderable inputs any more — the field/wavelet assets above are
            // still created and registered exactly as before (they feed the
            // authoring/inspector paths), but the recipe does not consume them.
            //
            // Device gate: shader assets cannot compile without a GPU device,
            // so deviceless (and with no authored program) the node gets NO
            // renderable — mirroring the app model where a node without a
            // program does not draw. That is a success, not an error: `out`
            // stays invalid and the caller skips the attach.
            const wz::asset::AssetKey authored_program =
                render_program_asset_override;

            RenderableAsset renderable{};
            if (!(authored_program == wz::asset::AssetKey{})) {
                renderable = assets.renderables().create_rhi_pull_mesh({
                    .name = name,
                    .mesh = mesh,
                    .program = RenderProgramAsset{ .key = authored_program },
                    .style = style_asset,
                });
                if (!renderable.valid()) {
                    return false;
                }
            }
            else if (assets.gpu_device_valid()) {
                const RenderProgramAsset program =
                    ensure_mesh_style_pull_program(
                        assets.logger(),
                        assets.files(),
                        assets.shaders(),
                        assets.render_programs(),
                        mesh_render_style_data_for_scene_style(
                            effective_style));
                if (!program.valid()) {
                    return false;
                }
                renderable = assets.renderables().create_rhi_pull_mesh({
                    .name = name,
                    .mesh = mesh,
                    .program = program,
                    .style = style_asset,
                });
                if (!renderable.valid()) {
                    return false;
                }
            }

            renderables.emplace(key, renderable);
            out = renderable;
            return true;
        }        TerrainMeshSurfaceHeightPolicy terrain_height_policy_for_source(
            SceneTerrainMeshHeightPolicy policy)
        {
            switch (policy) {
            case SceneTerrainMeshHeightPolicy::HighestAcceptedSurface:
                return TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface;
            }
            return TerrainMeshSurfaceHeightPolicy::HighestAcceptedSurface;
        }

        SceneMeshProcessingOutputs create_processed_mesh_asset(
            EngineAssetLibrary& assets,
            MeshAsset source_mesh,
            const std::string& name,
            const SceneMeshProcessingAsset& processing,
            const SceneMeshRegionSetAsset* region_set)
        {
            switch (processing.operation) {
            case SceneMeshProcessingOperation::Decimate:
                return SceneMeshProcessingOutputs{
                    .mesh = assets.meshes().create_decimated_mesh({
                        .name = name,
                        .source_mesh = source_mesh,
                        .target_vertex_count =
                            processing.target_vertex_count,
                        .target_triangle_count =
                            processing.target_triangle_count,
                        .target_ratio = processing.target_ratio,
                        .preserve_boundary = processing.preserve_boundary,
                        .aspect_ratio = processing.aspect_ratio,
                        .edge_length = processing.edge_length,
                        .max_valence = processing.max_valence,
                        .normal_deviation = processing.normal_deviation,
                        .hausdorff_error = processing.hausdorff_error,
                    }),
                };

            case SceneMeshProcessingOperation::MeshClusterHierarchyPreview:
            {
                std::optional<MeshClusterHierarchyRegionMaskDesc>
                    region_mask;
                if (region_set) {
                    region_mask = MeshClusterHierarchyRegionMaskDesc{
                        .field = MeshDerivedFieldAsset{
                            .output = region_set->source_field_asset,
                        },
                        .mask = region_set->mask,
                    };
                }
                const MeshSparseOperatorAsset graph_operator =
                    assets.mesh_sparse_operators().create_sparse_operator({
                        .name = name + "_graph_operator",
                        .source_mesh = source_mesh,
                        .kind =
                            MeshSparseOperatorKind::UniformVertexLaplacian,
                        .domain = MeshOperatorDomain::Vertex,
                    });
                const ComputePipelineAsset graph_cells_pipeline =
                    create_builtin_mesh_cluster_graph_cells_pipeline(
                        assets);
                const ComputePipelineAsset graph_cells_compact_pipeline =
                    create_builtin_mesh_cluster_graph_cells_compact_pipeline(
                        assets);
                const MeshClusterHierarchyAsset hierarchy =
                    assets.mesh_cluster_hierarchies()
                        .create_mesh_cluster_hierarchy({
                            .name = name + "_hierarchy",
                            .source_mesh = source_mesh,
                            .method =
                                MeshClusterHierarchyBuildMethod::
                                    LaplacianGraphCells,
                            .max_level_index =
                                processing.preview_level_index,
                            .graph_operator = graph_operator,
                            .graph_cells_pipeline = graph_cells_pipeline,
                            .graph_cells_compact_pipeline =
                                graph_cells_compact_pipeline,
                            .region_mask = region_mask,
                        });
                if (!hierarchy.valid()) {
                    return {};
                }

                return SceneMeshProcessingOutputs{
                    .mesh = assets.mesh_cluster_hierarchies()
                        .create_mesh_cluster_hierarchy_preview_mesh({
                            .name = name + "_level_"
                                + std::to_string(
                                    processing.preview_level_index),
                            .hierarchy = hierarchy,
                            .level_index = processing.preview_level_index,
                        }),
                    .hierarchy = hierarchy,
                };
            }

            case SceneMeshProcessingOperation::DebugTriangleStride:
                return SceneMeshProcessingOutputs{
                    .mesh =
                        assets.meshes().create_debug_triangle_stride_mesh({
                            .name = name + "_debug_triangle_stride",
                            .source_mesh = source_mesh,
                            .stride = debug_triangle_stride_for_level(
                                processing.preview_level_index),
                            .phase = 0,
                        }),
                };
            }

            return {};
        }

        bool ensure_mesh_for_source(
            EngineAssetLibrary& assets,
            const SceneMeshSourceAsset& source,
            SceneMeshProcessingAsset* processing,
            const SceneMeshRegionSetAsset* region_set,
            MeshCache& meshes,
            MeshProcessingCache& processed_meshes,
            MeshAsset& out,
            std::string& error)
        {
            const std::string source_key = mesh_source_cache_key(source);
            MeshAsset source_mesh{};
            if (const auto found = meshes.find(source_key);
                found != meshes.end())
            {
                source_mesh = found->second;
            }
            else {
                source_mesh =
                    create_mesh_asset_for_scene_source(assets, source, error);
                if (!source_mesh.valid()) {
                    return false;
                }
                meshes.emplace(source_key, source_mesh);
            }

            if (!processing || !processing->enabled) {
                if (processing) {
                    processing->source_mesh_asset = source_mesh.output;
                    processing->processed_mesh_asset = source_mesh.output;
                    processing->hierarchy_asset = {};
                }
                out = source_mesh;
                return true;
            }

            processing->source_mesh_asset = source_mesh.output;
            processing->processed_mesh_asset = {};
            processing->hierarchy_asset = {};

            const SceneMeshRegionSetAsset* active_region_set =
                region_set_for_processing(*processing, region_set);
            const std::string processed_key =
                mesh_source_cache_key(source)
                + mesh_processing_cache_suffix(
                    processing,
                    active_region_set);
            if (const auto found = processed_meshes.find(processed_key);
                found != processed_meshes.end())
            {
                processing->processed_mesh_asset = found->second.mesh.output;
                processing->hierarchy_asset = found->second.hierarchy.output;
                out = found->second.mesh;
                return true;
            }

            const SceneMeshProcessingOutputs outputs =
                create_processed_mesh_asset(
                    assets,
                    source_mesh,
                    "scene_editor/processed_mesh/" + processed_key,
                    *processing,
                    active_region_set);
            MeshAsset mesh = outputs.mesh;
            processing->processed_mesh_asset = mesh.output;
            processing->hierarchy_asset = outputs.hierarchy.output;
            if (!mesh.valid()) {
                error = "failed to register processed mesh: "
                    + processed_key;
                return false;
            }

            meshes.emplace(processed_key, mesh);
            processed_meshes.emplace(processed_key, outputs);
            out = mesh;
            return true;
        }

        bool ensure_processed_mesh_asset(
            EngineAssetLibrary& assets,
            MeshAsset source_mesh,
            const std::string& key,
            SceneMeshProcessingAsset* processing,
            const SceneMeshRegionSetAsset* region_set,
            MeshCache& meshes,
            MeshProcessingCache& processed_meshes,
            MeshAsset& out,
            std::string& error)
        {
            if (!processing || !processing->enabled) {
                if (processing) {
                    processing->source_mesh_asset = source_mesh.output;
                    processing->processed_mesh_asset = source_mesh.output;
                    processing->hierarchy_asset = {};
                }
                out = source_mesh;
                return true;
            }

            processing->source_mesh_asset = source_mesh.output;
            processing->processed_mesh_asset = {};
            processing->hierarchy_asset = {};

            const SceneMeshRegionSetAsset* active_region_set =
                region_set_for_processing(*processing, region_set);
            const std::string processed_key =
                key + mesh_processing_cache_suffix(
                    processing,
                    active_region_set);
            if (const auto found = processed_meshes.find(processed_key);
                found != processed_meshes.end())
            {
                processing->processed_mesh_asset = found->second.mesh.output;
                processing->hierarchy_asset = found->second.hierarchy.output;
                out = found->second.mesh;
                return true;
            }

            const SceneMeshProcessingOutputs outputs =
                create_processed_mesh_asset(
                    assets,
                    source_mesh,
                    "scene_editor/processed_mesh/" + processed_key,
                    *processing,
                    active_region_set);
            MeshAsset processed = outputs.mesh;
            processing->processed_mesh_asset = processed.output;
            processing->hierarchy_asset = outputs.hierarchy.output;
            if (!processed.valid()) {
                error = "failed to register processed mesh: "
                    + processed_key;
                return false;
            }

            meshes.emplace(processed_key, processed);
            processed_meshes.emplace(processed_key, outputs);
            out = processed;
            return true;
        }

        bool ensure_renderable_for_mesh_source(
            EngineAssetLibrary& assets,
            const SceneMeshSourceAsset& source,
            SceneMeshProcessingAsset* processing,
            const SceneMeshRegionSetAsset* region_set,
            const SceneMeshDerivedFieldSourceAsset* field_source,
            SceneMeshWaveletAnalysisAsset* wavelet_analysis,
            const SceneMeshComputeFieldAsset* compute_field,
            bool has_behavior_field_source,
            SceneMeshRenderStyleAsset& style,
            RenderableCache& renderables,
            MeshCache& meshes,
            MeshProcessingCache& processed_meshes,
            RenderableAsset& out,
            MeshAsset& out_mesh,
            std::string& error)
        {
            const bool has_compute_field_source =
                compute_field && compute_field->enabled;
            const bool use_mesh_vertex_pull_mask_program =
                style.mask.enabled
                && style.mask.domain == MeshMaskDomain::Face;
            const bool use_mesh_vertex_pull_program =
                !style.field_visualization_enabled
                && (!style.mask.enabled || use_mesh_vertex_pull_mask_program)
                && processing
                && processing->enabled
                && processing->operation
                    == SceneMeshProcessingOperation
                        ::MeshClusterHierarchyPreview
                && assets.gpu_device_valid();
            const SceneMeshRegionSetAsset* active_region_set =
                processing
                    ? region_set_for_processing(*processing, region_set)
                    : nullptr;
            const std::string source_key =
                mesh_source_cache_key(source)
                + mesh_processing_cache_suffix(
                    processing,
                    active_region_set);
            const std::string key =
                source_key + ":"
                + mesh_render_style_cache_key(style)
                + mesh_wavelet_analysis_cache_key(
                    style.field_visualization_enabled
                        ? wavelet_analysis
                        : nullptr)
                + mesh_derived_field_source_cache_key(field_source)
                + mesh_compute_field_cache_key(
                    has_compute_field_source ? compute_field : nullptr)
                + (use_mesh_vertex_pull_program
                    ? use_mesh_vertex_pull_mask_program
                        ? ":mesh_vertex_pull_mask"
                        : ":mesh_vertex_pull"
                    : "")
                + (has_behavior_field_source
                    ? ":behavior_field" : "");
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

            wz::asset::AssetKey render_program_asset_override{};
            if (use_mesh_vertex_pull_program) {
                const RenderProgramAsset pull_program =
                    use_mesh_vertex_pull_mask_program
                        ? create_builtin_mesh_vertex_pull_mask_program(assets)
                        : create_builtin_mesh_vertex_pull_program(assets);
                render_program_asset_override = pull_program.key;
            }

            if (!ensure_mesh_for_source(
                    assets,
                    source,
                    processing,
                    region_set,
                    meshes,
                    processed_meshes,
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
                    render_program_asset_override,
                    has_behavior_field_source,
                    has_compute_field_source,
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

        void materialize_event_trigger(const SceneNodeAsset& node)
        {
            if (!node.event_trigger) {
                return;
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
        MeshProcessingCache processed_meshes;
        ScalarFieldCache scalar_fields;
        VectorFieldCache vector_fields;
        MeshFieldRefCache mesh_field_refs;
        MeshOperatorRefCache mesh_operator_refs;
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

        const auto compute_kernels_started = std::chrono::steady_clock::now();
        for (auto& node : scene.nodes) {
            if (!materialize_compute_kernel(assets, node, report.error)) {
                if (report.error.empty()) {
                    report.error =
                        "compute kernel materialization failed for "
                        + node_log_name(node);
                }
                return report;
            }
        }
        log_phase(
            "compute_kernels",
            elapsed_ms_since(compute_kernels_started));

        const auto event_triggers_started = std::chrono::steady_clock::now();
        for (auto& node : scene.nodes) {
            materialize_event_trigger(node);
        }
        log_phase(
            "event_triggers",
            elapsed_ms_since(event_triggers_started));

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

            MeshAsset source_mesh{};
            auto ensure_source_mesh = [&]() -> bool
            {
                if (source_mesh.valid()) {
                    return true;
                }
                if (!ensure_mesh_for_source(
                        assets,
                        *node.mesh_source,
                        nullptr,
                        nullptr,
                        meshes,
                        processed_meshes,
                        source_mesh,
                        report.error))
                {
                    if (report.error.empty()) {
                        report.error =
                            "mesh source unavailable for "
                            + node_log_name(node);
                    }
                    return false;
                }
                if (node.mesh_processing) {
                    node.mesh_processing->source_mesh_asset =
                        source_mesh.output;
                }
                return true;
            };

            MeshAsset processed_mesh{};
            auto ensure_processed_mesh = [&]() -> bool
            {
                if (!node.mesh_processing) {
                    return true;
                }
                if (processed_mesh.valid()) {
                    return true;
                }
                if (!ensure_mesh_for_source(
                        assets,
                        *node.mesh_source,
                        &*node.mesh_processing,
                        node.mesh_region_set
                            ? &*node.mesh_region_set
                            : nullptr,
                        meshes,
                        processed_meshes,
                        processed_mesh,
                        report.error))
                {
                    if (report.error.empty()) {
                        report.error =
                            "processed mesh unavailable for "
                            + node_log_name(node);
                    }
                    return false;
                }
                return true;
            };

            const bool has_authored_render_style =
                node.mesh_render_style.has_value();
            SceneMeshRenderStyleAsset render_style =
                node.mesh_render_style.value_or(default_render_style);
            if (node.mesh_mask_render_style && !has_authored_render_style) {
                render_style = SceneMeshRenderStyleAsset{};
                render_style.wireframe.enabled = false;
                render_style.surface.enabled = false;
                render_style.depth_test = true;
                render_style.depth_write = true;
                render_style.double_sided = true;
                render_style.hidden_line_prepass = false;
            }
            render_style.style_asset = {};
            render_style.field_visualization_asset = {};
            render_style.mask = {};
            render_style.mask_source_field_ref.clear();
            render_style.mask_source_field_asset = {};
            if (node.mesh_mask_render_style) {
                node.mesh_mask_render_style->source_field_asset = {};
            }
            if (node.mesh_region_set) {
                node.mesh_region_set->source_field_asset = {};
            }
            const bool behavior_field_source =
                node_has_behavior_field_source(node);
            if (behavior_field_source
                && !render_style.field_visualization_enabled)
            {
                render_style.field_visualization_enabled = true;
            }
            if (node.mesh_wavelet_analysis) {
                node.mesh_wavelet_analysis->field_asset = {};
            }
            if (node.mesh_derived_field_source) {
                node.mesh_derived_field_source->resolved_field_asset = {};
            }
            if (node.mesh_sparse_operator_source) {
                node.mesh_sparse_operator_source->resolved_operator_asset = {};
            }
            if (node.mesh_sparse_apply_field) {
                node.mesh_sparse_apply_field->output_field_asset = {};
            }
            if (node.mesh_sparse_diffusion_bands) {
                node.mesh_sparse_diffusion_bands->output_field_asset = {};
            }
            if (node.mesh_level_mask_source) {
                node.mesh_level_mask_source->output_field_asset = {};
            }
            if (node.mesh_compute_field) {
                node.mesh_compute_field->field_asset = {};
            }

            const bool derived_field_source =
                node.mesh_derived_field_source
                && node.mesh_derived_field_source->enabled;
            if (derived_field_source) {
                if (!ensure_source_mesh()) {
                    return report;
                }
                if (!materialize_mesh_derived_field_source(
                        assets,
                        node,
                        source_mesh,
                        mesh_field_refs,
                        report.error))
                {
                    return report;
                }
                if (render_style.field_visualization_enabled
                    && render_style.field_visualization_field_ref.empty()
                    && render_style.field_visualization_asset
                        == wz::asset::AssetKey{})
                {
                    render_style.field_visualization_asset =
                        node.mesh_derived_field_source->resolved_field_asset;
                }
            }

            const bool sparse_operator_source =
                node.mesh_sparse_operator_source
                && node.mesh_sparse_operator_source->enabled;
            if (sparse_operator_source) {
                if (!node.mesh_source) {
                    report.error =
                        "mesh sparse operator source requires a mesh source "
                        "for " + node_log_name(node);
                    return report;
                }
                if (!ensure_source_mesh()) {
                    return report;
                }
                if (!materialize_mesh_sparse_operator_source(
                        assets,
                        node,
                        source_mesh,
                        mesh_field_refs,
                        mesh_operator_refs,
                        report.error))
                {
                    return report;
                }
            }

            const bool sparse_apply_field_source =
                node.mesh_sparse_apply_field
                && node.mesh_sparse_apply_field->enabled;
            if (sparse_apply_field_source) {
                if (!ensure_source_mesh()) {
                    return report;
                }
                if (!materialize_mesh_sparse_apply_field(
                        assets,
                        node,
                        source_mesh,
                        mesh_field_refs,
                        mesh_operator_refs,
                        report.error))
                {
                    return report;
                }
                if (render_style.field_visualization_enabled
                    && render_style.field_visualization_field_ref.empty()
                    && render_style.field_visualization_asset
                        == wz::asset::AssetKey{})
                {
                    render_style.field_visualization_asset =
                        node.mesh_sparse_apply_field->output_field_asset;
                }
            }

            const bool sparse_diffusion_bands_source =
                node.mesh_sparse_diffusion_bands
                && node.mesh_sparse_diffusion_bands->enabled;
            if (sparse_diffusion_bands_source) {
                if (!ensure_source_mesh()) {
                    return report;
                }
                if (!materialize_mesh_sparse_diffusion_bands(
                        assets,
                        node,
                        source_mesh,
                        mesh_field_refs,
                        mesh_operator_refs,
                        report.error))
                {
                    return report;
                }
                if (render_style.field_visualization_enabled
                    && render_style.field_visualization_field_ref.empty()
                    && render_style.field_visualization_asset
                        == wz::asset::AssetKey{})
                {
                    render_style.field_visualization_asset =
                        node.mesh_sparse_diffusion_bands->output_field_asset;
                }
            }

            const bool level_mask_source =
                node.mesh_level_mask_source
                && node.mesh_level_mask_source->enabled;
            if (level_mask_source) {
                if (!ensure_source_mesh()) {
                    return report;
                }
                if (!materialize_mesh_level_mask_source(
                        assets,
                        node,
                        source_mesh,
                        mesh_field_refs,
                        report.error))
                {
                    return report;
                }
            }

            const bool mask_render_style =
                node.mesh_mask_render_style
                && node.mesh_mask_render_style->enabled;
            if (mask_render_style) {
                const bool depth_test = render_style.depth_test;
                const bool depth_write = render_style.depth_write;
                const bool double_sided = render_style.double_sided;
                render_style = SceneMeshRenderStyleAsset{};
                render_style.wireframe =
                    node.mesh_mask_render_style->wireframe;
                render_style.surface.enabled = false;
                render_style.depth_test = depth_test;
                render_style.depth_write = depth_write;
                render_style.double_sided = double_sided;
                render_style.hidden_line_prepass = false;
                render_style.field_visualization_enabled = false;
                render_style.field_visualization_asset = {};
                render_style.mask = node.mesh_mask_render_style->mask;
                render_style.mask.enabled = true;
                render_style.mask_source_field_ref =
                    node.mesh_mask_render_style->source_field_ref;
            }

            if (render_style.field_visualization_enabled
                && !render_style.field_visualization_field_ref.empty())
            {
                if (!resolve_mesh_field_visualization_ref(
                        node,
                        render_style,
                        mesh_field_refs,
                        render_style.field_visualization_asset,
                        report.error))
                {
                    return report;
                }
            }
            if (render_style.mask.enabled
                && !render_style.mask_source_field_ref.empty())
            {
                if (!resolve_mesh_mask_field_ref(
                        node,
                        render_style,
                        mesh_field_refs,
                        render_style.mask_source_field_asset,
                        report.error))
                {
                    return report;
                }
                if (node.mesh_mask_render_style) {
                    node.mesh_mask_render_style->source_field_asset =
                        render_style.mask_source_field_asset;
                }
            }
            if (node.mesh_region_set
                && node.mesh_region_set->enabled
                && !node.mesh_region_set->source_field_ref.empty())
            {
                if (!resolve_mesh_region_set_field_ref(
                        node,
                        mesh_field_refs,
                        *node.mesh_region_set,
                        report.error))
                {
                    return report;
                }
            }

            const bool compute_field_source =
                node.mesh_compute_field
                && node.mesh_compute_field->enabled;
            if (compute_field_source) {
                // The compute-derived field is a first-class asset on the
                // node's mesh, materialized whether or not a preview
                // renderable visualizes one of its channels.
                if (!ensure_source_mesh()) {
                    return report;
                }
                if (!materialize_mesh_compute_field(
                        assets,
                        node,
                        source_mesh,
                        report.error))
                {
                    return report;
                }
                if (render_style.field_visualization_enabled
                    && render_style.field_visualization_asset
                        == wz::asset::AssetKey{})
                {
                    render_style.field_visualization_asset =
                        node.mesh_compute_field->field_asset;
                }
            }

            normalize_implicit_mesh_field_visualization(
                node,
                derived_field_source,
                sparse_apply_field_source,
                sparse_diffusion_bands_source,
                compute_field_source,
                behavior_field_source,
                render_style);

            if (options.create_preview_renderables && node.visible) {
                if (!ensure_source_mesh()) {
                    return report;
                }
                const SceneMeshDerivedFieldSourceAsset* active_field_source =
                    derived_field_source
                        ? &*node.mesh_derived_field_source
                        : nullptr;
                SceneMeshComputeFieldAsset* active_compute_field =
                    compute_field_source
                        ? &*node.mesh_compute_field
                        : nullptr;
                const bool render_requires_source_mesh =
                    render_style.field_visualization_enabled
                    || (render_style.mask.enabled
                        && (!node.mesh_mask_render_style
                            || node.mesh_mask_render_style->mesh_input
                                == SceneMeshMaskRenderMeshInput::Source));
                if (node.mesh_processing && !ensure_processed_mesh())
                {
                    return report;
                }
                SceneMeshProcessingAsset* render_processing =
                    !render_requires_source_mesh && node.mesh_processing
                        ? &*node.mesh_processing
                        : nullptr;
                RenderableAsset renderable{};
                MeshAsset render_mesh{};
                if (!ensure_renderable_for_mesh_source(
                        assets,
                        *node.mesh_source,
                        render_processing,
                        node.mesh_region_set
                            ? &*node.mesh_region_set
                            : nullptr,
                        active_field_source,
                        node.mesh_wavelet_analysis
                            ? &*node.mesh_wavelet_analysis
                            : nullptr,
                        active_compute_field,
                        behavior_field_source,
                        render_style,
                        renderables,
                        meshes,
                        processed_meshes,
                        renderable,
                        render_mesh,
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
                // Deviceless with no authored program the mesh gets NO
                // renderable (issue #195: shader assets need a GPU device to
                // compile, so the mesh_style program cannot be provisioned) —
                // leave renderable_asset unset instead of attaching a zero key.
                if (renderable.valid()) {
                    attach_renderable_asset(node, renderable.output);
                }
                else {
                    node.renderable_asset.reset();
                }
                if (has_authored_render_style)
                {
                    SceneMeshRenderStyleAsset materialized_style =
                        render_style;
                    materialized_style.mask = {};
                    materialized_style.mask_source_field_ref.clear();
                    materialized_style.mask_source_field_asset = {};
                    node.mesh_render_style = materialized_style;
                }
                if (renderable.valid()) {
                    append_unique_renderable(report, renderable.output);
                }
            }
            else if (options.create_preview_renderables && !node.visible) {
                if (!ensure_source_mesh()) {
                    return report;
                }
                node.renderable_asset.reset();
            }
            else if (!ensure_source_mesh()) {
                return report;
            }
            else if (!ensure_processed_mesh()) {
                return report;
            }
            else {
                node.renderable_asset.reset();
            }

            // Terrain mesh-sources that pull their mesh from this scene node
            // expect the node's authored result, which includes mesh
            // processing (e.g. decimation) when it was built. Prefer the
            // processed mesh, falling back to the raw source mesh (e.g. for
            // hidden nodes that intentionally skip preview processing).
            if (node.mesh_processing && processed_mesh.valid()) {
                mesh_assets_by_node[node.id] = processed_mesh.output;
            }
            else if (source_mesh.valid()) {
                mesh_assets_by_node[node.id] = source_mesh.output;
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
                        node.mesh_region_set
                            ? &*node.mesh_region_set
                            : nullptr,
                        meshes,
                        processed_meshes,
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

            // A terrain node no longer produces a renderable of its own. The
            // 0x703 debug and 0x704 surface recipes died with the nine MeshIA
            // builtin programs they drew through; terrain renders as a 0x70A
            // CameraSnappedTerrain custom renderable (#234). Everything above
            // still runs -- the terrain, visual-proxy and constraint-collision
            // assets feed collision and the authoring/inspector paths.
            node.renderable_asset.reset();
        }
        log_phase("terrain_assets", elapsed_ms_since(terrain_started));

        // Materialize movement-constraint collision surfaces authored
        // DIRECTLY from a scalar field heightfield (no TerrainAsset).
        const auto collision_started = std::chrono::steady_clock::now();
        for (auto& node : scene.nodes) {
            if (!node.collision || !node.collision->height_field_source) {
                continue;
            }
            // Precedence (issue #216/#217): a graph collision reference wins over
            // the inline heightfield materialize path; bridge_scene_collision_keys
            // sets collision_asset from the node id, so skip the inline build.
            if (node.collision->collision_asset_node_id) {
                continue;
            }

            auto& collision_component = *node.collision;
            const auto& source = *collision_component.height_field_source;
            if (source.scalar_field_asset == wz::asset::AssetKey{}) {
                collision_component.collision_asset = {};
                continue;
            }

            const std::string key =
                "collision/constraint_heightfield/" + node.id;
            CollisionAsset collision{};
            if (const auto found = collisions.find(key);
                found != collisions.end())
            {
                collision = found->second;
            }
            else {
                collision = assets.collisions().create_from_height_field({
                    .name =
                        "scene_editor/collision/constraint_heightfield/"
                        + node.id,
                    .height_field = ScalarFieldAsset{
                        .output = source.scalar_field_asset,
                    },
                    .origin = { source.origin[0], source.origin[1] },
                    .size = { source.size[0], source.size[1] },
                    .vertical_scale = source.vertical_scale,
                    .base_height = source.base_height,
                    .occupancy = CollisionOccupancyData{
                        .kind = CollisionOccupancyKind::WalkableSurface,
                        .blocks_movement = true,
                        .queryable = true,
                    },
                    .projection_resolution_x =
                        source.projection_resolution_x,
                    .projection_resolution_y =
                        source.projection_resolution_y,
                });
                if (!collision.valid()) {
                    report.error =
                        "heightfield constraint collision unavailable for "
                        + node_log_name(node);
                    return report;
                }
                collisions.emplace(key, collision);
            }
            collision_component.collision_asset = collision.output;
        }
        log_phase(
            "collision_constraint_assets",
            elapsed_ms_since(collision_started));

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

    uint32_t bridge_scene_renderable_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft)
    {
        uint32_t bridged = 0;
        for (SceneNodeAsset& node : nodes) {
            if (!node.renderable_asset_node_id) {
                continue;
            }
            // Clear first: if the new graph removed/renamed this authored
            // renderable, the node must stop drawing the previous graph's key
            // (old compiled payloads can still resolve), not keep it stale.
            node.renderable_asset.reset();
            const auto it =
                draft.node_index.find(*node.renderable_asset_node_id);
            if (it != draft.node_index.end()) {
                node.renderable_asset = draft.nodes[it->second].node.key;
                ++bridged;
            }
        }
        return bridged;
    }

    std::vector<wz::scene::AuthoredEntityId>
    scene_nodes_with_unresolvable_renderable(
        std::span<const SceneNodeAsset> nodes,
        const std::function<bool(const wz::asset::AssetKey&)>&
            renderable_has_recipe)
    {
        std::vector<wz::scene::AuthoredEntityId> out;
        for (const SceneNodeAsset& node : nodes) {
            if (node.renderable_asset
                && !renderable_has_recipe(*node.renderable_asset)) {
                out.push_back(node.id);
            }
        }
        return out;
    }

    uint32_t bridge_scene_source_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft)
    {
        uint32_t bridged = 0;
        for (SceneNodeAsset& node : nodes) {
            if (!node.scene_source_node_id) {
                continue;
            }
            // Clear first (same reasoning as renderables): a removed/renamed
            // authored scene source must stop resolving the previous key.
            node.scene_source.reset();
            const auto it =
                draft.node_index.find(*node.scene_source_node_id);
            if (it != draft.node_index.end()) {
                node.scene_source = draft.nodes[it->second].node.key;
                ++bridged;
            }
        }
        return bridged;
    }

    uint32_t bridge_scene_collision_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft)
    {
        uint32_t bridged = 0;
        for (SceneNodeAsset& node : nodes) {
            if (!node.collision || !node.collision->collision_asset_node_id) {
                continue;
            }
            // Clear first (same reasoning as renderables/scene sources): a
            // removed/renamed authored collision must stop resolving the
            // previous key.
            node.collision->collision_asset = {};
            const auto it =
                draft.node_index.find(*node.collision->collision_asset_node_id);
            if (it != draft.node_index.end()) {
                node.collision->collision_asset = draft.nodes[it->second].node.key;
                ++bridged;
            }
        }
        return bridged;
    }

    uint32_t bridge_scene_atmosphere_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft)
    {
        uint32_t bridged = 0;
        for (SceneNodeAsset& node : nodes) {
            if (!node.atmosphere
                || node.atmosphere->atmosphere_asset_node_id == 0) {
                continue;
            }
            // Clear first (same reasoning as renderables/collisions): a
            // removed/renamed authored atmosphere must stop resolving the
            // previous key, leaving the frame unfogged rather than stale.
            node.atmosphere->atmosphere_asset = {};
            const auto it = draft.node_index.find(
                node.atmosphere->atmosphere_asset_node_id);
            if (it != draft.node_index.end()) {
                node.atmosphere->atmosphere_asset =
                    draft.nodes[it->second].node.key;
                ++bridged;
            }
        }
        return bridged;
    }

    uint32_t bridge_scene_environment_keys(
        std::span<SceneNodeAsset> nodes,
        const wz::asset::AssetGraphDraft& draft)
    {
        uint32_t bridged = 0;
        for (SceneNodeAsset& node : nodes) {
            if (!node.environment
                || node.environment->environment_asset_node_id == 0) {
                continue;
            }
            // Clear first (same reasoning as atmosphere/collision): a
            // removed/renamed authored environment must stop resolving the
            // previous key, leaving the frame with no environment rather than a
            // stale one.
            node.environment->environment_asset = {};
            const auto it = draft.node_index.find(
                node.environment->environment_asset_node_id);
            if (it != draft.node_index.end()) {
                node.environment->environment_asset =
                    draft.nodes[it->second].node.key;
                ++bridged;
            }
        }
        return bridged;
    }

    namespace
    {
        // The frame-global source rule, shared by the atmosphere and its
        // successor the frame environment: the FIRST enabled node wins, and a
        // second is REPORTED as a duplicate rather than blended -- an authoring
        // error the caller names, not something resolved silently here. Both
        // components are frame-global in the same way, so this is one rule with
        // two spellings; it was copy-pasted per component before.
        //
        // The same contract is why SceneInstance keeps these as tables rather
        // than scalars: the duplicate stays visible on both sides.
        template <class EnabledFn>
        void select_frame_global_source(
            std::span<const SceneNodeAsset> nodes,
            EnabledFn enabled,
            const SceneNodeAsset*& source,
            const SceneNodeAsset*& duplicate)
        {
            for (const SceneNodeAsset& node : nodes) {
                if (!enabled(node)) {
                    continue;
                }
                if (!source) {
                    source = &node;
                    continue;
                }
                duplicate = &node;
                break;
            }
        }
    }

    SceneFrameAtmosphere resolve_scene_frame_atmosphere(
        std::span<const SceneNodeAsset> nodes,
        const EngineAssetLibrary& assets)
    {
        SceneFrameAtmosphere out{};

        select_frame_global_source(
            nodes,
            [](const SceneNodeAsset& node) {
                return node.atmosphere && node.atmosphere->enabled;
            },
            out.source,
            out.duplicate);

        if (!out.source) {
            return out;
        }

        const wz::asset::AssetKey key = out.source->atmosphere->atmosphere_asset;
        if (key == wz::asset::AssetKey{}) {
            return out;
        }

        // find_ rather than get_: get_ logs on a miss, and a miss here is a
        // transient state (an unbridged key mid-edit), not an error.
        out.atmosphere = assets.atmospheres().get_atmosphere_data(
            assets.atmospheres().find_atmosphere(
                AtmosphereAsset{ .output = key }));
        return out;
    }

    SceneFrameEnvironment resolve_scene_frame_environment(
        std::span<const SceneNodeAsset> nodes,
        const EngineAssetLibrary& assets)
    {
        SceneFrameEnvironment out{};

        select_frame_global_source(
            nodes,
            [](const SceneNodeAsset& node) {
                return node.environment && node.environment->enabled;
            },
            out.source,
            out.duplicate);

        if (!out.source) {
            return out;
        }

        const wz::asset::AssetKey key =
            out.source->environment->environment_asset;
        if (key == wz::asset::AssetKey{}) {
            return out;
        }

        // find_ rather than get_: get_ logs on a miss, and a miss here is a
        // transient state (an unbridged key mid-edit), not an error.
        out.environment = assets.environments().get_environment_data(
            assets.environments().find_environment(
                EnvironmentAsset{ .output = key }));
        return out;
    }

    std::vector<SceneNodeAsset> expand_scene_source_children(
        const SceneNodeAsset& host,
        const SceneAssetData& sub_scene)
    {
        std::vector<SceneNodeAsset> children;
        children.reserve(sub_scene.nodes.size());

        // The set of sub-scene ids present, so a node whose parent is missing
        // from the sub-scene is treated as a root (reparents to the host) rather
        // than dangling under a non-existent "<host>/<gone>".
        std::unordered_map<std::string, bool> sub_ids;
        sub_ids.reserve(sub_scene.nodes.size());
        for (const SceneNodeAsset& n : sub_scene.nodes) {
            if (!n.id.empty()) {
                sub_ids.emplace(n.id, true);
            }
        }

        const auto namespaced =
            [&host](const std::string& sub_id) -> std::string {
                return host.id + "/" + sub_id;
            };

        for (const SceneNodeAsset& src : sub_scene.nodes) {
            if (src.id.empty()) {
                continue;  // no stable namespaced id can be formed
            }

            SceneNodeAsset child = src;  // carry name/local/renderable/etc.
            child.id = namespaced(src.id);

            const bool is_root =
                !src.parent_id.has_value()
                || src.parent_id->empty()
                || sub_ids.find(*src.parent_id) == sub_ids.end();
            if (is_root) {
                // Sub-scene roots reparent to the host so the host transform
                // composes onto the whole sub-tree.
                child.parent_id = host.id;
            }
            else {
                child.parent_id = namespaced(*src.parent_id);
            }

            // A grafted child is a fresh node in the host namespace; it must not
            // also carry its own scene-source reference (it is the expansion).
            child.scene_source_node_id.reset();
            child.scene_source.reset();

            // Intrinsic geometry (issue #213 increment 3): a mesh-bearing part
            // gets an indexed GLB-part geometry binding pointing back at the host's
            // "Scene from GLB" node + this part's name, so the whole subtree renders
            // from a single render program assigned on the host (inherited down the
            // tree) without the user wiring geometry per node. Skipped when the part
            // already carries a renderable (the imperative create_scene_from_glb
            // path supplies its own) or when the host uses the descriptor route
            // (no asset-graph node id to reference).
            if (host.scene_source_node_id
                && src.mesh_index
                && !child.renderable_asset
                && !child.renderable)
            {
                attach_geometry_glb_part(
                    child, *host.scene_source_node_id, src.id);
            }

            children.push_back(std::move(child));
        }

        return children;
    }

} // namespace wz::engine::assets
