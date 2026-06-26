// src/engine/assets/scene/scene_compilers.cpp

#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/json/json.h>

#include <scene/compile/scene_node_class.h>
#include <scene/compile/compiled_scene.h>

#include <external/json/json_document.h>
#include <external/json/json_read_helpers.h>

#include <algorithm>
#include <any>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace wz::engine::assets::internal
{
    namespace
    {
        using wz::json::find_member;
        using wz::json::read_string;
        using wz::json::read_number;
        using wz::json::read_bool;
        using wz::json::read_float3;
        using wz::json::read_float4;

        SceneFromGLBCompileDesc scene_from_glb_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            SceneFromGLBCompileDesc desc{};
            desc.scene_index =
                params.get<uint32_t>("scene_index", desc.scene_index);
            // consume_mode: how a host node expands this Scene asset (#213).
            // "flatten" => one-time expansion; anything else (incl. the default
            // and "instance") keeps the live-instance default.
            const std::string consume_mode =
                params.get<std::string>("consume_mode", "instance");
            desc.consume_mode = (consume_mode == "flatten")
                ? SceneSourceConsumeMode::Flatten
                : SceneSourceConsumeMode::Instance;
            return desc;
        }

        AuthoredTransform parse_transform(const wz::json::JSONValue& obj)
        {
            AuthoredTransform t{};
            read_float3(obj, "translation", t.translation);
            read_float4(obj, "rotation_quat", t.rotation_quat);
            read_float3(obj, "scale", t.scale);
            return t;
        }

        wz::scene::SceneNodeClass parse_node_class_for_pipeline(
            std::string_view pipeline)
        {
            namespace sc = wz::scene;

            if (pipeline == "OpaqueGeometry") {
                return {
                    .role = sc::SceneRole::Renderable,
                    .producer = sc::ProducerKind::Mesh,
                    .default_surface = sc::SurfaceClass::Opaque,
                    .spatial = sc::SpatialKind::MeshBounds,
                    .compile = sc::CompileBehavior::Static,
                    .domains = sc::RenderDomain::Surface | sc::RenderDomain::Shadow,
                };
            }
            if (pipeline == "TransparentGeometry") {
                return {
                    .role = sc::SceneRole::Renderable,
                    .producer = sc::ProducerKind::Mesh,
                    .default_surface = sc::SurfaceClass::Transparent,
                    .spatial = sc::SpatialKind::MeshBounds,
                    .compile = sc::CompileBehavior::Static,
                    .domains = sc::RenderDomain::Surface | sc::RenderDomain::Transparent,
                };
            }
            return {};
        }

        std::optional<uint64_t> parse_hex_u64(std::string_view text)
        {
            uint64_t value = 0;
            const char* first = text.data();
            const char* last = text.data() + text.size();
            const auto result = std::from_chars(first, last, value, 16);
            if (result.ec != std::errc{} || result.ptr != last) {
                return std::nullopt;
            }
            return value;
        }

        std::optional<wz::asset::AssetKey> parse_asset_key_string(
            std::string_view text)
        {
            // Concrete AssetKey syntax used by asset-reference fields.
            constexpr std::string_view kPrefix = "asset-key:";
            if (!text.starts_with(kPrefix)) {
                return std::nullopt;
            }

            text.remove_prefix(kPrefix.size());

            std::array<uint64_t, 8> parts{};
            for (std::size_t i = 0; i < parts.size(); ++i) {
                const std::size_t end = text.find(':');
                const std::string_view part =
                    end == std::string_view::npos
                        ? text
                        : text.substr(0, end);

                auto value = parse_hex_u64(part);
                if (!value) {
                    return std::nullopt;
                }
                parts[i] = *value;

                if (i + 1 == parts.size()) {
                    if (end != std::string_view::npos) {
                        return std::nullopt;
                    }
                }
                else {
                    if (end == std::string_view::npos) {
                        return std::nullopt;
                    }
                    text.remove_prefix(end + 1);
                }
            }

            return wz::asset::AssetKey{
                .content_hash = { parts[0], parts[1] },
                .schema_hash = { parts[2], parts[3] },
                .compiler_hash = { parts[4], parts[5] },
                .deps_hash = { parts[6], parts[7] },
            };
        }

        std::optional<SceneRenderableBinding> parse_debug_renderable(
            const wz::json::JSONValue& obj)
        {
            const auto* dr = find_member(obj, "debug_renderable");
            if (!dr || dr->kind != wz::json::JSONValueKind::Object)
                return std::nullopt;

            SceneRenderableBinding binding{};

            auto pipeline = read_string(*dr, "pipeline");
            if (pipeline) {
                binding.node_class = parse_node_class_for_pipeline(*pipeline);
            }

            auto mesh_val = read_number(*dr, "mesh");
            if (mesh_val)
                binding.mesh = static_cast<wz::scene::MeshHandle>(
                    static_cast<uint32_t>(*mesh_val));

            auto mat_val = read_number(*dr, "material");
            if (mat_val)
                binding.material = static_cast<wz::scene::MaterialHandle>(
                    static_cast<uint32_t>(*mat_val));

            const auto* bounds = find_member(*dr, "bounds");
            if (bounds && bounds->kind == wz::json::JSONValueKind::Object) {
                float mn[3]{}, mx[3]{};
                if (read_float3(*bounds, "min", mn) &&
                    read_float3(*bounds, "max", mx)) {
                    binding.local_bounds = wz::scene::AABB{
                        .min = { mn[0], mn[1], mn[2] },
                        .max = { mx[0], mx[1], mx[2] },
                    };
                }
            }

            auto vis = read_bool(*dr, "visible");
            if (vis) binding.visible = *vis;

            return binding;
        }

        std::optional<SceneActorMovementSpace> parse_actor_movement_space(
            std::string_view text)
        {
            if (text == "world") {
                return SceneActorMovementSpace::World;
            }
            if (text == "local") {
                return SceneActorMovementSpace::Local;
            }
            return std::nullopt;
        }

        std::optional<SceneMeshSourceKind> parse_mesh_source_kind(
            std::string_view text)
        {
            if (text == "placeholder") {
                return SceneMeshSourceKind::Placeholder;
            }
            if (text == "glb") {
                return SceneMeshSourceKind::GLB;
            }
            if (text == "procedural_cube") {
                return SceneMeshSourceKind::ProceduralCube;
            }
            if (text == "procedural_quad") {
                return SceneMeshSourceKind::ProceduralQuad;
            }
            if (text == "procedural_triangle") {
                return SceneMeshSourceKind::ProceduralTriangle;
            }
            return std::nullopt;
        }

        std::optional<SceneImportSourceKind> parse_scene_import_source_kind(
            std::string_view text)
        {
            if (text == "glb") {
                return SceneImportSourceKind::GLB;
            }
            return std::nullopt;
        }

        std::optional<SceneMeshProcessingOperation>
        parse_mesh_processing_operation(std::string_view text)
        {
            if (text == "decimate" || text == "pmp_decimate") {
                return SceneMeshProcessingOperation::Decimate;
            }
            if (text == "cluster_hierarchy_preview"
                || text == "mesh_cluster_hierarchy_preview")
            {
                return SceneMeshProcessingOperation::
                    MeshClusterHierarchyPreview;
            }
            if (text == "debug_triangle_stride") {
                return SceneMeshProcessingOperation::DebugTriangleStride;
            }
            return std::nullopt;
        }

        std::optional<SceneMeshWaveletAnalysisFunction>
        parse_mesh_wavelet_analysis_function(std::string_view text)
        {
            if (text == "builtin_detail_heat_v0"
                || text == "gpu_detail_heat_v0"
                || text == "wavelet_heatmap_v0")
            {
                return SceneMeshWaveletAnalysisFunction::BuiltinDetailHeatV0;
            }
            return std::nullopt;
        }

        std::optional<MeshFieldVisualizationPalette>
        parse_mesh_field_visualization_palette(std::string_view text)
        {
            if (text == "heat" || text == "default") {
                return MeshFieldVisualizationPalette::Heat;
            }
            if (text == "grayscale" || text == "greyscale") {
                return MeshFieldVisualizationPalette::Grayscale;
            }
            if (text == "diverging" || text == "signed") {
                return MeshFieldVisualizationPalette::Diverging;
            }
            return std::nullopt;
        }

        std::optional<MeshMaskDomain> parse_mesh_mask_domain(
            std::string_view text)
        {
            if (text == "face") {
                return MeshMaskDomain::Face;
            }
            if (text == "vertex") {
                return MeshMaskDomain::Vertex;
            }
            if (text == "edge") {
                return MeshMaskDomain::Edge;
            }
            return std::nullopt;
        }

        std::optional<MeshMaskProjectionMode> parse_mesh_mask_projection_mode(
            std::string_view text)
        {
            if (text == "direct") {
                return MeshMaskProjectionMode::Direct;
            }
            if (text == "any") {
                return MeshMaskProjectionMode::Any;
            }
            if (text == "all") {
                return MeshMaskProjectionMode::All;
            }
            if (text == "majority") {
                return MeshMaskProjectionMode::Majority;
            }
            return std::nullopt;
        }

        std::optional<MeshMaskOverlapMode> parse_mesh_mask_overlap_mode(
            std::string_view text)
        {
            if (text == "priority" || text == "last_wins") {
                return MeshMaskOverlapMode::Priority;
            }
            if (text == "alpha_blend" || text == "blend") {
                return MeshMaskOverlapMode::AlphaBlend;
            }
            return std::nullopt;
        }

        std::optional<SceneMeshMaskRenderMeshInput>
        parse_mesh_mask_render_mesh_input(std::string_view text)
        {
            if (text == "source") {
                return SceneMeshMaskRenderMeshInput::Source;
            }
            if (text == "processed") {
                return SceneMeshMaskRenderMeshInput::Processed;
            }
            return std::nullopt;
        }

        std::optional<SceneMeshRegionSetIntent>
        parse_mesh_region_set_intent(std::string_view text)
        {
            if (text == "material_mask") {
                return SceneMeshRegionSetIntent::MaterialMask;
            }
            if (text == "remesh_subset" || text == "remesh") {
                return SceneMeshRegionSetIntent::RemeshSubset;
            }
            return std::nullopt;
        }

        void read_mesh_render_layer(
            const wz::json::JSONValue& obj,
            const char* field_name,
            SceneMeshRenderLayerAsset& layer);

        bool read_mesh_mask_render_style(
            const wz::json::JSONValue& obj,
            const std::string& node_id,
            const char* field_name,
            wz::Logger& logger,
            SceneMeshMaskRenderStyleAsset& style)
        {
            if (auto enabled = read_bool(obj, "enabled")) {
                style.enabled = *enabled;
                style.mask.enabled = *enabled;
            }
            if (auto mesh_input = read_string(obj, "mesh_input")) {
                const auto parsed =
                    parse_mesh_mask_render_mesh_input(*mesh_input);
                if (!parsed) {
                    logger.error(std::string(field_name) + " on node '"
                        + node_id + "' has unknown mesh_input '"
                        + std::string(*mesh_input) + "'");
                    return false;
                }
                style.mesh_input = *parsed;
            }
            if (auto source_ref = read_string(obj, "source_field_ref")) {
                style.source_field_ref = std::string(*source_ref);
            }
            if (style.source_field_ref.empty()) {
                if (auto source_ref = read_string(obj, "field_ref")) {
                    style.source_field_ref = std::string(*source_ref);
                }
            }
            if (auto domain = read_string(obj, "domain")) {
                const auto parsed = parse_mesh_mask_domain(*domain);
                if (!parsed) {
                    logger.error(std::string(field_name) + " on node '"
                        + node_id + "' has unknown domain '"
                        + std::string(*domain) + "'");
                    return false;
                }
                style.mask.domain = *parsed;
            }
            if (auto projection = read_string(obj, "projection_mode")) {
                const auto parsed =
                    parse_mesh_mask_projection_mode(*projection);
                if (!parsed) {
                    logger.error(std::string(field_name) + " on node '"
                        + node_id + "' has unknown projection_mode '"
                        + std::string(*projection) + "'");
                    return false;
                }
                style.mask.projection_mode = *parsed;
            }
            if (auto overlap = read_string(obj, "overlap_mode")) {
                const auto parsed = parse_mesh_mask_overlap_mode(*overlap);
                if (!parsed) {
                    logger.error(std::string(field_name) + " on node '"
                        + node_id + "' has unknown overlap_mode '"
                        + std::string(*overlap) + "'");
                    return false;
                }
                style.mask.overlap_mode = *parsed;
            }
            if (auto show = read_bool(obj, "show_unmatched")) {
                style.mask.show_unmatched = *show;
            }
            read_float4(obj, "unmatched_color", style.mask.unmatched_color);
            read_mesh_render_layer(obj, "wireframe", style.wireframe);

            if (const auto* rules = find_member(obj, "rules")) {
                if (rules->kind != wz::json::JSONValueKind::Array) {
                    logger.error(std::string(field_name) + " on node '"
                        + node_id + "' has non-array rules");
                    return false;
                }
                style.mask.rules.clear();
                for (const auto& rule_value : rules->array_values) {
                    if (!rule_value
                        || rule_value->kind
                            != wz::json::JSONValueKind::Object)
                    {
                        logger.error(std::string(field_name) + " on node '"
                            + node_id + "' has non-object rule");
                        return false;
                    }
                    MeshMaskRule rule{};
                    if (auto enabled = read_bool(*rule_value, "enabled")) {
                        rule.enabled = *enabled;
                    }
                    if (auto channel =
                            read_number(*rule_value, "input_channel_id"))
                    {
                        rule.input_channel_id =
                            static_cast<uint32_t>(
                                (std::max)(0.0, *channel));
                    }
                    if (auto lo = read_number(*rule_value, "lo")) {
                        rule.lo = static_cast<float>(*lo);
                    }
                    if (auto hi = read_number(*rule_value, "hi")) {
                        rule.hi = static_cast<float>(*hi);
                    }
                    read_float4(*rule_value, "color", rule.color);
                    if (auto priority =
                            read_number(*rule_value, "priority"))
                    {
                        rule.priority = static_cast<int32_t>(*priority);
                    }
                    style.mask.rules.push_back(rule);
                }
            }
            if (!style.mask.enabled
                && (!style.source_field_ref.empty()
                    || !style.mask.rules.empty()))
            {
                style.mask.enabled = true;
                style.enabled = true;
            }
            return true;
        }

        bool read_mesh_region_set(
            const wz::json::JSONValue& obj,
            const std::string& node_id,
            wz::Logger& logger,
            SceneMeshRegionSetAsset& region_set)
        {
            SceneMeshMaskRenderStyleAsset style{};
            if (!read_mesh_mask_render_style(
                    obj,
                    node_id,
                    "mesh_region_set",
                    logger,
                    style))
            {
                return false;
            }

            region_set.enabled = style.enabled;
            region_set.mesh_input = style.mesh_input;
            region_set.source_field_ref = std::move(style.source_field_ref);
            region_set.mask = std::move(style.mask);
            region_set.source_field_asset = style.source_field_asset;
            if (auto enabled = read_bool(obj, "enabled")) {
                region_set.enabled = *enabled;
                region_set.mask.enabled = *enabled;
            }

            if (auto region_set_id = read_string(obj, "region_set_id")) {
                region_set.region_set_id = std::string(*region_set_id);
            }
            if (region_set.region_set_id.empty()) {
                logger.error("mesh_region_set on node '" + node_id
                    + "' has empty region_set_id");
                return false;
            }
            if (auto intent = read_string(obj, "intent")) {
                const auto parsed = parse_mesh_region_set_intent(*intent);
                if (!parsed) {
                    logger.error("mesh_region_set on node '" + node_id
                        + "' has unknown intent '" + std::string(*intent)
                        + "'");
                    return false;
                }
                region_set.intent = *parsed;
            }
            return true;
        }

        std::optional<SceneMeshDerivedFieldSourceKind>
        parse_mesh_derived_field_source_kind(std::string_view text)
        {
            if (text == "constant") {
                return SceneMeshDerivedFieldSourceKind::Constant;
            }
            if (text == "position_gradient") {
                return SceneMeshDerivedFieldSourceKind::PositionGradient;
            }
            if (text == "vertex_index_gradient") {
                return SceneMeshDerivedFieldSourceKind::VertexIndexGradient;
            }
            if (text == "triangle_corner_count"
                || text == "triangle_count"
                || text == "valence")
            {
                return SceneMeshDerivedFieldSourceKind::TriangleCornerCount;
            }
            if (text == "vertex_area"
                || text == "incident_triangle_area")
            {
                return SceneMeshDerivedFieldSourceKind::VertexArea;
            }
            if (text == "triangle_area"
                || text == "face_area")
            {
                return SceneMeshDerivedFieldSourceKind::TriangleArea;
            }
            if (text == "mean_edge_length") {
                return SceneMeshDerivedFieldSourceKind::MeanEdgeLength;
            }
            if (text == "inverse_area_density"
                || text == "inverse_density")
            {
                return SceneMeshDerivedFieldSourceKind::InverseAreaDensity;
            }
            if (text == "log_density"
                || text == "log_area_density")
            {
                return SceneMeshDerivedFieldSourceKind::LogDensity;
            }
            return std::nullopt;
        }

        std::optional<SceneMeshDerivedFieldComponent>
        parse_mesh_derived_field_component(std::string_view text)
        {
            if (text == "x") {
                return SceneMeshDerivedFieldComponent::X;
            }
            if (text == "y") {
                return SceneMeshDerivedFieldComponent::Y;
            }
            if (text == "z") {
                return SceneMeshDerivedFieldComponent::Z;
            }
            return std::nullopt;
        }

        std::optional<MeshComputeInput> parse_mesh_compute_input(
            std::string_view text)
        {
            if (text == "positions") {
                return MeshComputeInput::Positions;
            }
            if (text == "normals") {
                return MeshComputeInput::Normals;
            }
            if (text == "uv0") {
                return MeshComputeInput::UV0;
            }
            if (text == "indices") {
                return MeshComputeInput::Indices;
            }
            if (text == "vertices") {
                return MeshComputeInput::Vertices;
            }
            return std::nullopt;
        }

        std::optional<MeshDerivedFieldValueType>
        parse_mesh_derived_field_value_type(std::string_view text)
        {
            if (text == "float1") {
                return MeshDerivedFieldValueType::Float1;
            }
            if (text == "float2") {
                return MeshDerivedFieldValueType::Float2;
            }
            if (text == "float3") {
                return MeshDerivedFieldValueType::Float3;
            }
            if (text == "float4") {
                return MeshDerivedFieldValueType::Float4;
            }
            if (text == "uint1") {
                return MeshDerivedFieldValueType::UInt1;
            }
            return std::nullopt;
        }

        std::optional<MeshSparseOperatorKind>
        parse_mesh_sparse_operator_kind(std::string_view text)
        {
            if (text == "uniform_vertex_laplacian"
                || text == "uniform_laplacian"
                || text == "uniform_adjacency"
                || text == "uniform_face_adjacency"
                || text == "face_laplacian")
            {
                return MeshSparseOperatorKind::UniformVertexLaplacian;
            }
            return std::nullopt;
        }

        std::optional<MeshOperatorDomain>
        parse_mesh_operator_domain(std::string_view text)
        {
            if (text == "vertex") {
                return MeshOperatorDomain::Vertex;
            }
            if (text == "edge") {
                return MeshOperatorDomain::Edge;
            }
            if (text == "face") {
                return MeshOperatorDomain::Face;
            }
            if (text == "corner") {
                return MeshOperatorDomain::Corner;
            }
            return std::nullopt;
        }

        std::optional<MeshDerivedFieldDomain>
        parse_mesh_derived_field_domain(std::string_view text)
        {
            if (text == "vertex") {
                return MeshDerivedFieldDomain::Vertex;
            }
            if (text == "face") {
                return MeshDerivedFieldDomain::Face;
            }
            if (text == "edge") {
                return MeshDerivedFieldDomain::Edge;
            }
            if (text == "corner") {
                return MeshDerivedFieldDomain::Corner;
            }
            return std::nullopt;
        }

        std::optional<MeshSparseOperatorValueConvention>
        parse_mesh_sparse_operator_value_convention(std::string_view text)
        {
            if (text == "neighbor_weights") {
                return MeshSparseOperatorValueConvention::NeighborWeights;
            }
            if (text == "full_matrix_entries") {
                return MeshSparseOperatorValueConvention::FullMatrixEntries;
            }
            return std::nullopt;
        }

        std::optional<SceneMeshSparseApplyMode>
        parse_mesh_sparse_apply_mode(std::string_view text)
        {
            if (text == "residual") {
                return SceneMeshSparseApplyMode::Residual;
            }
            return std::nullopt;
        }

        std::optional<SceneMeshSparseDiffusionMode>
        parse_mesh_sparse_diffusion_mode(std::string_view text)
        {
            if (text == "smooth") {
                return SceneMeshSparseDiffusionMode::Smooth;
            }
            if (text == "diffusion_step") {
                return SceneMeshSparseDiffusionMode::DiffusionStep;
            }
            return std::nullopt;
        }

        void read_mesh_render_layer(
            const wz::json::JSONValue& obj,
            const char* field_name,
            SceneMeshRenderLayerAsset& layer)
        {
            const auto* layer_value = find_member(obj, field_name);
            if (!layer_value
                || layer_value->kind != wz::json::JSONValueKind::Object)
            {
                return;
            }

            auto enabled = read_bool(*layer_value, "enabled");
            if (enabled) {
                layer.enabled = *enabled;
            }
            read_float4(*layer_value, "color", layer.color);
            auto emissive_strength =
                read_number(*layer_value, "emissive_strength");
            if (emissive_strength) {
                layer.emissive_strength =
                    static_cast<float>(*emissive_strength);
            }
        }

        // Raw MeshRenderStyleData layer reader (issue #213) — the
        // MeshRenderLayerStyle counterpart of read_mesh_render_layer (which reads
        // the SceneMeshRenderLayerAsset mirror). Mirrors that reader field-for-
        // field; missing members leave the layer at its default.
        void read_mesh_render_layer_style(
            const wz::json::JSONValue& obj,
            const char* field_name,
            MeshRenderLayerStyle& layer)
        {
            const auto* layer_value = find_member(obj, field_name);
            if (!layer_value
                || layer_value->kind != wz::json::JSONValueKind::Object)
            {
                return;
            }
            if (auto enabled = read_bool(*layer_value, "enabled")) {
                layer.enabled = *enabled;
            }
            read_float4(*layer_value, "color", layer.color);
            if (auto emissive = read_number(*layer_value, "emissive_strength")) {
                layer.emissive_strength = static_cast<float>(*emissive);
            }
        }

        // Raw MeshRenderStyleData reader (issue #213): the parse counterpart of
        // mesh_render_style_data_value. Reads only the persisted visual fields
        // (layers + alpha/depth/sidedness); field_visualization + mask are not
        // persisted by the GLB descriptor (see report) and keep their defaults.
        MeshRenderStyleData read_mesh_render_style_data(
            const wz::json::JSONValue& obj)
        {
            MeshRenderStyleData style{};
            read_mesh_render_layer_style(obj, "wireframe", style.wireframe);
            read_mesh_render_layer_style(obj, "surface", style.surface);
            if (auto alpha = read_number(obj, "alpha")) {
                style.alpha =
                    static_cast<float>((std::clamp)(*alpha, 0.0, 1.0));
            }
            if (auto depth_test = read_bool(obj, "depth_test")) {
                style.depth_test = *depth_test;
            }
            if (auto depth_write = read_bool(obj, "depth_write")) {
                style.depth_write = *depth_write;
            }
            if (auto double_sided = read_bool(obj, "double_sided")) {
                style.double_sided = *double_sided;
            }
            if (auto hidden_line = read_bool(obj, "hidden_line_prepass")) {
                style.hidden_line_prepass = *hidden_line;
            }
            return style;
        }

        bool apply_legacy_mesh_render_style_kind(
            std::string_view text,
            SceneMeshRenderStyleAsset& style,
            const wz::json::JSONValue& obj)
        {
            float color[4]{
                style.wireframe.color[0],
                style.wireframe.color[1],
                style.wireframe.color[2],
                style.wireframe.color[3],
            };
            read_float4(obj, "color", color);

            float emissive_strength = style.wireframe.emissive_strength;
            if (auto value = read_number(obj, "emissive_strength")) {
                emissive_strength = static_cast<float>(*value);
            }

            if (text == "wireframe" || text == "vector_wireframe") {
                style.wireframe.enabled = true;
                std::copy(color, color + 4, style.wireframe.color);
                style.wireframe.emissive_strength = emissive_strength;
                style.surface.enabled = false;
                return true;
            }
            if (text == "opaque_surface" || text == "transparent_surface") {
                style.wireframe.enabled = false;
                style.surface.enabled = true;
                std::copy(color, color + 4, style.surface.color);
                style.surface.emissive_strength = emissive_strength;
                return true;
            }
            return false;
        }

        std::optional<SceneTerrainRenderPath> parse_terrain_render_path(
            std::string_view text)
        {
            if (text == "auto") {
                return SceneTerrainRenderPath::Auto;
            }
            if (text == "surface") {
                return SceneTerrainRenderPath::Surface;
            }
            if (text == "debug_wireframe") {
                return SceneTerrainRenderPath::DebugWireframe;
            }
            if (text == "none") {
                return SceneTerrainRenderPath::None;
            }
            return std::nullopt;
        }

        std::optional<SceneTerrainLightingSource>
        parse_terrain_lighting_source(std::string_view text)
        {
            if (text == "explicit_nodes") {
                return SceneTerrainLightingSource::ExplicitNodes;
            }
            if (text == "scene_default") {
                return SceneTerrainLightingSource::SceneDefault;
            }
            if (text == "environment_node") {
                return SceneTerrainLightingSource::EnvironmentNode;
            }
            if (text == "hybrid") {
                return SceneTerrainLightingSource::Hybrid;
            }
            return std::nullopt;
        }

        std::optional<DirectLightKind> parse_direct_light_kind(
            std::string_view text)
        {
            if (text == "directional") {
                return DirectLightKind::Directional;
            }
            if (text == "point") {
                return DirectLightKind::Point;
            }
            if (text == "spot") {
                return DirectLightKind::Spot;
            }
            return std::nullopt;
        }

        std::optional<wz::scene::LightType> parse_scene_light_type(
            std::string_view text)
        {
            if (text == "directional") {
                return wz::scene::LightType::Directional;
            }
            if (text == "point") {
                return wz::scene::LightType::Point;
            }
            if (text == "spot") {
                return wz::scene::LightType::Spot;
            }
            if (text == "ambient") {
                return wz::scene::LightType::Ambient;
            }
            return std::nullopt;
        }

        std::optional<AmbientLightingMode> parse_ambient_lighting_mode(
            std::string_view text)
        {
            if (text == "constant") {
                return AmbientLightingMode::Constant;
            }
            if (text == "field_modulated") {
                return AmbientLightingMode::FieldModulated;
            }
            return std::nullopt;
        }

        std::optional<AmbientLightingDomainMapping>
        parse_ambient_lighting_domain_mapping(std::string_view text)
        {
            if (text == "terrain_uv") {
                return AmbientLightingDomainMapping::TerrainUV;
            }
            if (text == "world_xz") {
                return AmbientLightingDomainMapping::WorldXZ;
            }
            return std::nullopt;
        }

        std::optional<HDRIEnvironmentFormat> parse_hdri_environment_format(
            std::string_view text)
        {
            if (text == "auto") {
                return HDRIEnvironmentFormat::Auto;
            }
            if (text == "radiance_hdr" || text == "hdr") {
                return HDRIEnvironmentFormat::RadianceHDR;
            }
            if (text == "openexr" || text == "exr") {
                return HDRIEnvironmentFormat::OpenEXR;
            }
            return std::nullopt;
        }

        std::optional<SceneSkyVisualKind> parse_sky_visual_kind(
            std::string_view text)
        {
            if (text == "none") {
                return SceneSkyVisualKind::None;
            }
            if (text == "solid_color") {
                return SceneSkyVisualKind::SolidColor;
            }
            if (text == "direction_debug") {
                return SceneSkyVisualKind::DirectionDebug;
            }
            if (text == "gradient") {
                return SceneSkyVisualKind::Gradient;
            }
            if (text == "equirectangular_texture") {
                return SceneSkyVisualKind::EquirectangularTexture;
            }
            if (text == "scalar_field") {
                return SceneSkyVisualKind::ScalarField;
            }
            if (text == "vector_field") {
                return SceneSkyVisualKind::VectorField;
            }
            return std::nullopt;
        }

        std::optional<SceneSkyProjection> parse_sky_projection(
            std::string_view text)
        {
            if (text == "sphere") {
                return SceneSkyProjection::Sphere;
            }
            return std::nullopt;
        }

        std::optional<SceneScalarFieldSourceKind>
        parse_scalar_field_source_kind(std::string_view text)
        {
            if (text == "raw_f32") {
                return SceneScalarFieldSourceKind::RawF32;
            }
            if (text == "procedural_gradient_x") {
                return SceneScalarFieldSourceKind::ProceduralGradientX;
            }
            if (text == "procedural_gradient_y") {
                return SceneScalarFieldSourceKind::ProceduralGradientY;
            }
            if (text == "procedural_radial_gradient") {
                return SceneScalarFieldSourceKind::ProceduralRadialGradient;
            }
            if (text == "procedural_checkerboard") {
                return SceneScalarFieldSourceKind::ProceduralCheckerboard;
            }
            if (text == "procedural_sine_waves") {
                return SceneScalarFieldSourceKind::ProceduralSineWaves;
            }
            return std::nullopt;
        }

        std::optional<SceneVectorFieldSourceKind>
        parse_vector_field_source_kind(std::string_view text)
        {
            if (text == "raw_f32") {
                return SceneVectorFieldSourceKind::RawF32;
            }
            return std::nullopt;
        }

        std::optional<SceneComputeKernelPortKind>
        parse_compute_kernel_port_kind(std::string_view text)
        {
            if (text == "structured_buffer") {
                return SceneComputeKernelPortKind::StructuredBuffer;
            }
            if (text == "u32") {
                return SceneComputeKernelPortKind::U32;
            }
            if (text == "f32") {
                return SceneComputeKernelPortKind::F32;
            }
            return std::nullopt;
        }

        std::optional<SceneComputeKernelPortDirection>
        parse_compute_kernel_port_direction(std::string_view text)
        {
            if (text == "input") {
                return SceneComputeKernelPortDirection::Input;
            }
            if (text == "output") {
                return SceneComputeKernelPortDirection::Output;
            }
            return std::nullopt;
        }

        std::optional<SceneComputeKernelBindingKind>
        parse_compute_kernel_binding_kind(std::string_view text)
        {
            if (text == "srv") {
                return SceneComputeKernelBindingKind::SRV;
            }
            if (text == "uav") {
                return SceneComputeKernelBindingKind::UAV;
            }
            return std::nullopt;
        }

        std::optional<SceneTerrainMeshHeightPolicy>
        parse_terrain_mesh_height_policy(std::string_view text)
        {
            if (text == "highest_accepted_surface") {
                return SceneTerrainMeshHeightPolicy::HighestAcceptedSurface;
            }
            return std::nullopt;
        }

        std::optional<SceneTerrainMeshSourceMode>
        parse_terrain_mesh_source_mode(std::string_view text)
        {
            if (text == "mesh_asset") {
                return SceneTerrainMeshSourceMode::MeshAsset;
            }
            if (text == "scene_node") {
                return SceneTerrainMeshSourceMode::SceneNode;
            }
            return std::nullopt;
        }

        std::optional<SceneTerrainHeightFieldSourceMode>
        parse_terrain_height_field_source_mode(std::string_view text)
        {
            if (text == "scalar_field_asset") {
                return SceneTerrainHeightFieldSourceMode::ScalarFieldAsset;
            }
            if (text == "scene_node") {
                return SceneTerrainHeightFieldSourceMode::SceneNode;
            }
            return std::nullopt;
        }

        bool read_float2(
            const wz::json::JSONValue& obj,
            const char* field_name,
            float out[2])
        {
            const auto* value = find_member(obj, field_name);
            if (!value) {
                return false;
            }
            if (value->kind != wz::json::JSONValueKind::Array
                || value->array_values.size() < 2)
            {
                return false;
            }
            for (size_t i = 0; i < 2; ++i) {
                const auto& element = value->array_values[i];
                if (!element
                    || element->kind != wz::json::JSONValueKind::Number)
                {
                    return false;
                }
                const float v = static_cast<float>(element->number_value);
                if (!std::isfinite(v)) {
                    return false;
                }
                out[i] = v;
            }
            return true;
        }

        using SceneAssetReferenceMap =
            std::unordered_map<std::string, wz::asset::AssetKey>;

        std::optional<wz::asset::AssetKey> find_glb_renderable_binding(
            const SceneFromGLBCompileDesc& desc,
            uint32_t mesh_index)
        {
            const auto it = std::find_if(
                desc.mesh_renderables.begin(), desc.mesh_renderables.end(),
                [&](const auto& binding) {
                    return binding.mesh_index == mesh_index
                        && !(binding.renderable_asset == wz::asset::AssetKey{});
                });
            if (it == desc.mesh_renderables.end()) {
                return std::nullopt;
            }
            return it->renderable_asset;
        }

        bool parse_asset_reference_object(
            const wz::json::JSONValue& obj,
            const std::string& node_id,
            std::string_view field_name,
            wz::Logger& logger,
            const SceneAssetReferenceMap& asset_references,
            std::optional<wz::asset::AssetKey>& out)
        {
            const auto* value = find_member(obj, field_name);
            if (!value) {
                return true;
            }
            if (value->kind != wz::json::JSONValueKind::Object) {
                logger.error(std::string(field_name) + " on node '" + node_id
                    + "' is not an object");
                return false;
            }

            auto asset = read_string(*value, "asset");
            if (!asset || asset->empty()) {
                logger.error(std::string(field_name) + " on node '" + node_id
                    + "' missing 'asset'");
                return false;
            }

            auto key = parse_asset_key_string(*asset);
            if (!key) {
                const auto it = asset_references.find(std::string(*asset));
                if (it == asset_references.end()) {
                    logger.error(std::string(field_name) + ".asset on node '" + node_id
                        + "' could not be resolved: " + std::string(*asset));
                    return false;
                }
                key = it->second;
            }
            if (*key == wz::asset::AssetKey{}) {
                logger.error(std::string(field_name) + ".asset on node '" + node_id
                    + "' is empty");
                return false;
            }

            out = *key;
            return true;
        }

        bool read_behavior_events(
            const wz::json::JSONValue& value,
            const std::string& field_name,
            const std::string& node_id,
            wz::Logger& logger,
            std::vector<std::string>& out)
        {
            const auto* events = find_member(value, "events");
            if (!events) {
                return true;
            }
            if (events->kind != wz::json::JSONValueKind::Array) {
                logger.error(field_name + ".events on node '" + node_id
                    + "' is not an array");
                return false;
            }

            for (const auto& event : events->array_values) {
                if (event
                    && event->kind == wz::json::JSONValueKind::String
                    && !event->string_value.empty())
                {
                    out.push_back(event->string_value);
                }
            }
            return true;
        }

        bool read_behavior_config(
            const wz::json::JSONValue& value,
            const std::string& field_name,
            const std::string& node_id,
            wz::Logger& logger,
            std::vector<SceneBehaviorConfigValue>& out)
        {
            const auto* config = find_member(value, "config");
            if (!config) {
                return true;
            }
            if (config->kind != wz::json::JSONValueKind::Object) {
                logger.error(field_name + ".config on node '" + node_id
                    + "' is not an object");
                return false;
            }

            for (const auto& member : config->object_members) {
                if (!member.value) {
                    continue;
                }

                SceneBehaviorConfigValue entry{};
                entry.key = member.key;
                switch (member.value->kind) {
                case wz::json::JSONValueKind::Bool:
                    entry.kind = SceneBehaviorConfigValueKind::Bool;
                    entry.bool_value = member.value->bool_value;
                    break;
                case wz::json::JSONValueKind::Number:
                    entry.kind = SceneBehaviorConfigValueKind::Number;
                    entry.number_value = member.value->number_value;
                    break;
                case wz::json::JSONValueKind::String:
                    entry.kind = SceneBehaviorConfigValueKind::String;
                    entry.string_value = member.value->string_value;
                    break;
                default:
                    logger.error(field_name + ".config." + member.key
                        + " on node '" + node_id
                        + "' must be bool, number, or string");
                    return false;
                }
                out.push_back(std::move(entry));
            }
            return true;
        }

        std::optional<SceneBehaviorAsset> parse_behavior_component(
            const wz::json::JSONValue& value,
            const std::string& field_name,
            const std::string& node_id,
            wz::Logger& logger)
        {
            if (value.kind != wz::json::JSONValueKind::Object) {
                logger.error(field_name + " on node '" + node_id
                    + "' is not an object");
                return std::nullopt;
            }

            auto module = read_string(value, "module");
            if (!module || module->empty()) {
                logger.error(field_name + " on node '" + node_id
                    + "' missing non-empty module");
                return std::nullopt;
            }

            SceneBehaviorAsset component{};
            component.module = std::string(*module);

            auto id = read_string(value, "id");
            if (id) {
                component.id = std::string(*id);
            }

            auto label = read_string(value, "label");
            if (label) {
                component.label = std::string(*label);
            }

            auto name = read_string(value, "name");
            if (name) {
                component.name = std::string(*name);
            }

            auto enabled = read_bool(value, "enabled");
            if (enabled) {
                component.enabled = *enabled;
            }

            auto apply_in_editor = read_bool(value, "apply_in_editor");
            if (apply_in_editor) {
                component.apply_in_editor = *apply_in_editor;
            }

            if (!read_behavior_events(
                    value,
                    field_name,
                    node_id,
                    logger,
                    component.events)
                || !read_behavior_config(
                    value,
                    field_name,
                    node_id,
                    logger,
                    component.config))
            {
                return std::nullopt;
            }

            return component;
        }

        std::optional<SceneNodeAsset> parse_node(
            const wz::json::JSONValue& node_val,
            wz::Logger& logger,
            const SceneAssetReferenceMap& renderable_asset_references,
            const SceneAssetReferenceMap& collision_asset_references,
            const SceneAssetReferenceMap& terrain_asset_references,
            const SceneAssetReferenceMap& mesh_asset_references,
            const SceneAssetReferenceMap& scalar_field_asset_references,
            const SceneAssetReferenceMap& vector_field_asset_references)
        {
            if (node_val.kind != wz::json::JSONValueKind::Object)
                return std::nullopt;

            SceneNodeAsset node{};

            auto id = read_string(node_val, "id");
            if (!id) {
                logger.error("scene node missing 'id' field");
                return std::nullopt;
            }
            node.id = *id;
            node.name = read_string(node_val, "name").value_or(*id);

            const auto* parent_v = find_member(node_val, "parent");
            if (parent_v && parent_v->kind == wz::json::JSONValueKind::String) {
                node.parent_id = parent_v->string_value;
            }

            const auto* transform = find_member(node_val, "transform");
            if (transform && transform->kind == wz::json::JSONValueKind::Object) {
                node.local = parse_transform(*transform);
            }

            auto vis = read_bool(node_val, "visible");
            if (vis) node.visible = *vis;

            auto motion = read_string(node_val, "motion_type");
            if (motion && *motion == "Animated") {
                node.motion_type = wz::scene::TransformNode::MotionType::Animated;
            }

            node.renderable = parse_debug_renderable(node_val);
            if (const auto* renderable =
                    find_member(node_val, "renderable"))
            {
                if (renderable->kind != wz::json::JSONValueKind::Object) {
                    logger.error(
                        "renderable on node '" + node.id
                        + "' is not an object");
                    return std::nullopt;
                }
                const auto node_id =
                    read_number(*renderable, "asset_graph_node_id");
                if (node_id && *node_id > 0.0) {
                    node.renderable_asset_node_id =
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            *node_id);
                    node.renderable_asset.reset();
                }
                else if (!parse_asset_reference_object(
                             node_val,
                             node.id,
                             "renderable",
                             logger,
                             renderable_asset_references,
                             node.renderable_asset))
                {
                    return std::nullopt;
                }
            }

            // Scene-source reference (issue #213): mirror the renderable node-id
            // read. Only the authored asset-graph node id is persisted; the
            // resolved key (scene_source) is re-bridged on (re)bind.
            if (const auto* scene_source =
                    find_member(node_val, "scene_source"))
            {
                if (scene_source->kind != wz::json::JSONValueKind::Object) {
                    logger.error(
                        "scene_source on node '" + node.id
                        + "' is not an object");
                    return std::nullopt;
                }
                const auto node_id =
                    read_number(*scene_source, "asset_graph_node_id");
                if (node_id && *node_id > 0.0) {
                    node.scene_source_node_id =
                        static_cast<wz::asset::AssetGraphDraftNodeId>(
                            *node_id);
                    node.scene_source.reset();
                }
            }

            // GLB scene-source DESCRIPTOR (issue #213): the asset-graph-
            // independent route. Self-contained authored data (path +
            // scene_index + consume_mode + per-component style mapping)
            // re-resolved at materialization (resolve_glb_scene_sources). Mirrors
            // mesh_source's parse (kind/path/mesh_index style) but for a whole
            // sub-scene + styles.
            if (const auto* glb =
                    find_member(node_val, "glb_scene_source"))
            {
                if (glb->kind != wz::json::JSONValueKind::Object) {
                    logger.error(
                        "glb_scene_source on node '" + node.id
                        + "' is not an object");
                    return std::nullopt;
                }
                SceneGLBSceneSource source{};
                if (auto path = read_string(*glb, "path")) {
                    source.path = std::string(*path);
                }
                if (source.path.empty()) {
                    logger.error(
                        "glb_scene_source on node '" + node.id
                        + "' missing 'path'");
                    return std::nullopt;
                }
                if (auto scene_index = read_number(*glb, "scene_index")) {
                    if (*scene_index < 0.0 || !std::isfinite(*scene_index)) {
                        logger.error(
                            "glb_scene_source on node '" + node.id
                            + "' has invalid scene_index");
                        return std::nullopt;
                    }
                    source.scene_index =
                        static_cast<uint32_t>(*scene_index);
                }
                // consume_mode: "flatten" => persistent bake; anything else
                // (incl. default/"instance") => live instance graft. Mirrors
                // scene_from_glb_desc_from_params' convention.
                if (auto consume_mode = read_string(*glb, "consume_mode")) {
                    source.consume_mode = (*consume_mode == "flatten")
                        ? SceneSourceConsumeMode::Flatten
                        : SceneSourceConsumeMode::Instance;
                }
                if (const auto* base_style =
                        find_member(*glb, "base_style");
                    base_style
                    && base_style->kind == wz::json::JSONValueKind::Object)
                {
                    source.base_style =
                        read_mesh_render_style_data(*base_style);
                }
                if (const auto* overrides =
                        find_member(*glb, "style_overrides");
                    overrides
                    && overrides->kind == wz::json::JSONValueKind::Array)
                {
                    for (const auto& entry_ptr : overrides->array_values) {
                        if (!entry_ptr
                            || entry_ptr->kind
                                != wz::json::JSONValueKind::Object)
                        {
                            continue;
                        }
                        const wz::json::JSONValue& entry = *entry_ptr;
                        SceneGLBSceneSourceStyleOverride ov{};
                        if (auto mesh_index =
                                read_number(entry, "mesh_index"))
                        {
                            if (*mesh_index < 0.0
                                || !std::isfinite(*mesh_index))
                            {
                                logger.error(
                                    "glb_scene_source style override on node '"
                                    + node.id + "' has invalid mesh_index");
                                return std::nullopt;
                            }
                            ov.mesh_index =
                                static_cast<uint32_t>(*mesh_index);
                        }
                        if (const auto* style = find_member(entry, "style");
                            style
                            && style->kind == wz::json::JSONValueKind::Object)
                        {
                            ov.style = read_mesh_render_style_data(*style);
                        }
                        source.style_overrides.push_back(std::move(ov));
                    }
                }
                // Descriptor route is exclusive of the asset-graph-node route
                // (attach_glb_scene_source contract); clear the latter.
                node.scene_source_node_id.reset();
                node.scene_source.reset();
                node.glb_scene_source = std::move(source);
            }

            const auto* asset_reference =
                find_member(node_val, "asset_reference");
            if (asset_reference
                && asset_reference->kind == wz::json::JSONValueKind::Object)
            {
                SceneAssetReferenceAsset reference{};
                if (auto asset = read_string(*asset_reference, "asset")) {
                    if (!asset->empty()) {
                        auto key = parse_asset_key_string(*asset);
                        if (!key) {
                            const auto it =
                                renderable_asset_references.find(
                                    std::string(*asset));
                            if (it == renderable_asset_references.end()) {
                                logger.error(
                                    "asset_reference.asset on node '"
                                    + node.id
                                    + "' could not be resolved: "
                                    + std::string(*asset));
                                return std::nullopt;
                            }
                            key = it->second;
                        }
                        reference.asset = *key;
                    }
                }
                if (auto stable_asset_id =
                        read_string(*asset_reference, "stable_asset_id"))
                {
                    reference.stable_asset_id =
                        std::string(*stable_asset_id);
                }
                if (auto expected_type =
                        read_number(*asset_reference, "expected_type"))
                {
                    if (!std::isfinite(*expected_type)
                        || *expected_type < 0.0
                        || *expected_type > 65535.0)
                    {
                        logger.error("asset_reference on node '" + node.id
                            + "' has invalid expected_type");
                        return std::nullopt;
                    }
                    reference.expected_type =
                        static_cast<wz::asset::AssetType>(
                            static_cast<uint16_t>(*expected_type));
                }
                if (auto label = read_string(*asset_reference, "label")) {
                    reference.label = std::string(*label);
                }
                node.asset_reference = std::move(reference);
            }

            const auto* cam = find_member(node_val, "camera");
            if (cam && cam->kind == wz::json::JSONValueKind::Object) {
                SceneCameraAsset camera{};
                auto fov = read_number(*cam, "fov_y");
                if (fov) camera.fov_y = static_cast<float>(*fov);
                auto near_p = read_number(*cam, "near");
                if (near_p) camera.near_plane = static_cast<float>(*near_p);
                auto far_p = read_number(*cam, "far");
                if (far_p) camera.far_plane = static_cast<float>(*far_p);
                auto asp = read_number(*cam, "aspect");
                if (asp) camera.aspect = static_cast<float>(*asp);
                node.camera = camera;
            }

            const auto* dls = find_member(node_val, "direct_light_source");
            if (dls && dls->kind == wz::json::JSONValueKind::Object) {
                SceneDirectLightSourceAsset light{};
                auto asset = read_string(*dls, "asset");
                if (asset && !asset->empty()) {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        logger.error("direct_light_source.asset on node '"
                            + node.id + "' could not be parsed: "
                            + std::string(*asset));
                        return std::nullopt;
                    }
                    light.light_asset = *key;
                }
                auto kind = read_string(*dls, "kind");
                if (kind) {
                    auto parsed_kind = parse_direct_light_kind(*kind);
                    if (!parsed_kind) {
                        logger.error("direct_light_source on node '"
                            + node.id + "' has unknown kind '"
                            + std::string(*kind) + "'");
                        return std::nullopt;
                    }
                    light.kind = *parsed_kind;
                }
                read_float3(*dls, "color", light.color);
                auto intensity = read_number(*dls, "intensity");
                if (intensity) {
                    light.intensity = static_cast<float>(*intensity);
                }
                auto range = read_number(*dls, "range");
                if (range) {
                    light.range = static_cast<float>(*range);
                }
                auto inner = read_number(*dls, "inner_cone_radians");
                if (inner) {
                    light.inner_cone_radians = static_cast<float>(*inner);
                }
                auto outer = read_number(*dls, "outer_cone_radians");
                if (outer) {
                    light.outer_cone_radians = static_cast<float>(*outer);
                }
                node.direct_light_source = light;
            }

            const auto* ambient = find_member(node_val, "ambient_lighting");
            if (ambient && ambient->kind == wz::json::JSONValueKind::Object) {
                SceneAmbientLightingAsset lighting{};
                auto asset = read_string(*ambient, "asset");
                if (asset && !asset->empty()) {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        logger.error("ambient_lighting.asset on node '"
                            + node.id + "' could not be parsed: "
                            + std::string(*asset));
                        return std::nullopt;
                    }
                    lighting.lighting_asset = *key;
                }
                auto mode = read_string(*ambient, "mode");
                if (mode) {
                    auto parsed_mode = parse_ambient_lighting_mode(*mode);
                    if (!parsed_mode) {
                        logger.error("ambient_lighting on node '"
                            + node.id + "' has unknown mode '"
                            + std::string(*mode) + "'");
                        return std::nullopt;
                    }
                    lighting.mode = *parsed_mode;
                }
                read_float3(*ambient, "color", lighting.color);
                auto intensity = read_number(*ambient, "intensity");
                if (intensity) {
                    lighting.intensity = static_cast<float>(*intensity);
                }
                auto intensity_field =
                    read_string(*ambient, "intensity_field");
                if (intensity_field && !intensity_field->empty()) {
                    auto key = parse_asset_key_string(*intensity_field);
                    if (!key) {
                        const auto it = scalar_field_asset_references.find(
                            std::string(*intensity_field));
                        if (it == scalar_field_asset_references.end()) {
                            logger.error(
                                "ambient_lighting.intensity_field on node '"
                                + node.id + "' could not be resolved: "
                                + std::string(*intensity_field));
                            return std::nullopt;
                        }
                        key = it->second;
                    }
                    lighting.intensity_field = *key;
                }
                auto color_field = read_string(*ambient, "color_field");
                if (color_field && !color_field->empty()) {
                    auto key = parse_asset_key_string(*color_field);
                    if (!key) {
                        const auto it = vector_field_asset_references.find(
                            std::string(*color_field));
                        if (it == vector_field_asset_references.end()) {
                            logger.error(
                                "ambient_lighting.color_field on node '"
                                + node.id + "' could not be resolved: "
                                + std::string(*color_field));
                            return std::nullopt;
                        }
                        key = it->second;
                    }
                    lighting.color_field = *key;
                }
                auto mapping = read_string(*ambient, "domain_mapping");
                if (mapping) {
                    auto parsed_mapping =
                        parse_ambient_lighting_domain_mapping(*mapping);
                    if (!parsed_mapping) {
                        logger.error("ambient_lighting on node '"
                            + node.id + "' has unknown domain_mapping '"
                            + std::string(*mapping) + "'");
                        return std::nullopt;
                    }
                    lighting.domain_mapping = *parsed_mapping;
                }
                node.ambient_lighting = lighting;
            }

            const auto* hdri =
                find_member(node_val, "hdri_environment");
            if (hdri && hdri->kind == wz::json::JSONValueKind::Object) {
                SceneHDRIEnvironmentAsset environment{};
                auto asset = read_string(*hdri, "asset");
                if (asset && !asset->empty()) {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        logger.error("hdri_environment.asset on node '"
                            + node.id + "' could not be parsed: "
                            + std::string(*asset));
                        return std::nullopt;
                    }
                    environment.environment_asset = *key;
                }
                auto path = read_string(*hdri, "path");
                if (path) {
                    environment.path = std::string(*path);
                }
                auto format = read_string(*hdri, "format");
                if (format) {
                    auto parsed_format =
                        parse_hdri_environment_format(*format);
                    if (!parsed_format) {
                        logger.error("hdri_environment on node '"
                            + node.id + "' has unknown format '"
                            + std::string(*format) + "'");
                        return std::nullopt;
                    }
                    environment.format = *parsed_format;
                }
                auto exposure = read_number(*hdri, "exposure");
                if (exposure) {
                    environment.exposure = static_cast<float>(*exposure);
                }
                auto rotation_x =
                    read_number(*hdri, "rotation_x_radians");
                if (rotation_x) {
                    environment.rotation_x_radians =
                        static_cast<float>(*rotation_x);
                }
                auto rotation_y =
                    read_number(*hdri, "rotation_y_radians");
                if (rotation_y) {
                    environment.rotation_y_radians =
                        static_cast<float>(*rotation_y);
                }
                auto rotation_z =
                    read_number(*hdri, "rotation_z_radians");
                if (rotation_z) {
                    environment.rotation_z_radians =
                        static_cast<float>(*rotation_z);
                }
                auto lighting_intensity =
                    read_number(*hdri, "lighting_intensity");
                if (lighting_intensity) {
                    environment.lighting_intensity =
                        static_cast<float>(*lighting_intensity);
                }
                auto reflection_intensity =
                    read_number(*hdri, "reflection_intensity");
                if (reflection_intensity) {
                    environment.reflection_intensity =
                        static_cast<float>(*reflection_intensity);
                }
                auto background_intensity =
                    read_number(*hdri, "background_intensity");
                if (background_intensity) {
                    environment.background_intensity =
                        static_cast<float>(*background_intensity);
                }
                auto lighting_sample_resolution =
                    read_number(*hdri, "lighting_sample_resolution");
                if (lighting_sample_resolution) {
                    environment.lighting_sample_resolution =
                        static_cast<uint32_t>(
                            (std::max)(
                                1.0,
                                *lighting_sample_resolution));
                }
                read_float3(
                    *hdri,
                    "environment_light_color",
                    environment.environment_light_color);
                auto environment_light_intensity =
                    read_number(*hdri, "environment_light_intensity");
                if (environment_light_intensity) {
                    environment.environment_light_intensity =
                        static_cast<float>(*environment_light_intensity);
                }
                read_float3(
                    *hdri,
                    "dominant_light_direction",
                    environment.dominant_light_direction);
                read_float3(
                    *hdri,
                    "dominant_light_color",
                    environment.dominant_light_color);
                auto dominant_light_intensity =
                    read_number(*hdri, "dominant_light_intensity");
                if (dominant_light_intensity) {
                    environment.dominant_light_intensity =
                        static_cast<float>(*dominant_light_intensity);
                }
                auto dominant_light_confidence =
                    read_number(*hdri, "dominant_light_confidence");
                if (dominant_light_confidence) {
                    environment.dominant_light_confidence =
                        static_cast<float>(*dominant_light_confidence);
                }
                node.hdri_environment = environment;
            }

            const auto* sky_visual =
                find_member(node_val, "sky_visual");
            if (sky_visual
                && sky_visual->kind == wz::json::JSONValueKind::Object)
            {
                SceneSkyVisualAsset visual{};
                auto kind = read_string(*sky_visual, "kind");
                if (kind) {
                    auto parsed_kind = parse_sky_visual_kind(*kind);
                    if (!parsed_kind) {
                        logger.error("sky_visual on node '"
                            + node.id + "' has unknown kind '"
                            + std::string(*kind) + "'");
                        return std::nullopt;
                    }
                    visual.kind = *parsed_kind;
                }
                read_float3(*sky_visual, "solid_color", visual.solid_color);
                read_float3(
                    *sky_visual,
                    "gradient_top_color",
                    visual.gradient_top_color);
                read_float3(
                    *sky_visual,
                    "gradient_bottom_color",
                    visual.gradient_bottom_color);

                auto texture_asset =
                    read_string(*sky_visual, "texture_asset");
                if (texture_asset && !texture_asset->empty()) {
                    auto key = parse_asset_key_string(*texture_asset);
                    if (!key) {
                        logger.error("sky_visual.texture_asset on node '"
                            + node.id + "' could not be parsed: "
                            + std::string(*texture_asset));
                        return std::nullopt;
                    }
                    visual.texture_asset = *key;
                }
                auto texture_path =
                    read_string(*sky_visual, "texture_path");
                if (texture_path) {
                    visual.texture_path = std::string(*texture_path);
                }
                auto texture_format =
                    read_string(*sky_visual, "texture_format");
                if (texture_format) {
                    auto parsed_format =
                        parse_hdri_environment_format(*texture_format);
                    if (!parsed_format) {
                        logger.error("sky_visual.texture_format on node '"
                            + node.id + "' has unknown format '"
                            + std::string(*texture_format) + "'");
                        return std::nullopt;
                    }
                    visual.texture_format = *parsed_format;
                }

                auto scalar_field_asset =
                    read_string(*sky_visual, "scalar_field_asset");
                if (scalar_field_asset && !scalar_field_asset->empty()) {
                    auto key = parse_asset_key_string(*scalar_field_asset);
                    if (!key) {
                        logger.error(
                            "sky_visual.scalar_field_asset on node '"
                            + node.id + "' could not be parsed: "
                            + std::string(*scalar_field_asset));
                        return std::nullopt;
                    }
                    visual.scalar_field_asset = *key;
                }

                auto scalar_field_node =
                    read_string(*sky_visual, "scalar_field_node");
                if (scalar_field_node) {
                    visual.scalar_field_node = std::string(*scalar_field_node);
                }

                auto vector_field_asset =
                    read_string(*sky_visual, "vector_field_asset");
                if (vector_field_asset && !vector_field_asset->empty()) {
                    auto key = parse_asset_key_string(*vector_field_asset);
                    if (!key) {
                        const auto it = vector_field_asset_references.find(
                            std::string(*vector_field_asset));
                        if (it == vector_field_asset_references.end()) {
                            logger.error(
                                "sky_visual.vector_field_asset on node '"
                                + node.id + "' could not be resolved: "
                                + std::string(*vector_field_asset));
                            return std::nullopt;
                        }
                        key = it->second;
                    }
                    visual.vector_field_asset = *key;
                }

                auto vector_field_node =
                    read_string(*sky_visual, "vector_field_node");
                if (vector_field_node) {
                    visual.vector_field_node = std::string(*vector_field_node);
                }

                auto exposure = read_number(*sky_visual, "exposure");
                if (exposure) {
                    visual.exposure = static_cast<float>(*exposure);
                }
                auto rotation_x =
                    read_number(*sky_visual, "rotation_x_radians");
                if (rotation_x) {
                    visual.rotation_x_radians =
                        static_cast<float>(*rotation_x);
                }
                auto rotation_y =
                    read_number(*sky_visual, "rotation_y_radians");
                if (rotation_y) {
                    visual.rotation_y_radians =
                        static_cast<float>(*rotation_y);
                }
                auto rotation_z =
                    read_number(*sky_visual, "rotation_z_radians");
                if (rotation_z) {
                    visual.rotation_z_radians =
                        static_cast<float>(*rotation_z);
                }
                node.sky_visual = visual;
            }

            const auto* sky_surface =
                find_member(node_val, "sky_surface");
            if (sky_surface
                && sky_surface->kind == wz::json::JSONValueKind::Object)
            {
                SceneSkySurfaceAsset surface{};
                auto visual_node = read_string(*sky_surface, "visual_node");
                if (visual_node) {
                    surface.visual_node = std::string(*visual_node);
                }

                auto projection = read_string(*sky_surface, "projection");
                if (projection) {
                    auto parsed_projection =
                        parse_sky_projection(*projection);
                    if (!parsed_projection) {
                        logger.error("sky_surface on node '"
                            + node.id + "' has unknown projection '"
                            + std::string(*projection) + "'");
                        return std::nullopt;
                    }
                    surface.projection = *parsed_projection;
                }

                auto radius = read_number(*sky_surface, "radius");
                if (radius) {
                    surface.radius = static_cast<float>(*radius);
                }
                auto visible_to_camera =
                    read_bool(*sky_surface, "visible_to_camera");
                if (visible_to_camera) {
                    surface.visible_to_camera = *visible_to_camera;
                }
                node.sky_surface = surface;
            }

            // ── Non-render component descriptors ──────────────────────

            const auto* ir = find_member(node_val, "input_receiver");
            if (ir && ir->kind == wz::json::JSONValueKind::Object) {
                auto map_uri = read_string(*ir, "input_map");
                if (!map_uri || map_uri->empty()) {
                    logger.error("input_receiver on node '" + node.id
                        + "' missing 'input_map'");
                    return std::nullopt;
                }
                auto log_input = read_bool(*ir, "log_input");
                node.input_receiver = SceneInputReceiverAsset{
                    .input_map = std::string(*map_uri),
                    .log_input = log_input.value_or(false),
                };
            }

            const auto* fcc = find_member(node_val, "flying_camera_controller");
            if (fcc && fcc->kind == wz::json::JSONValueKind::Object) {
                SceneFlyingCameraControllerAsset ctrl{};
                auto ms = read_number(*fcc, "move_speed");
                if (ms) ctrl.move_speed = static_cast<float>(*ms);
                auto ls = read_number(*fcc, "look_speed");
                if (ls) ctrl.look_speed = static_cast<float>(*ls);
                auto bm = read_number(*fcc, "boost_multiplier");
                if (bm) ctrl.boost_multiplier = static_cast<float>(*bm);
                auto rs = read_number(*fcc, "roll_speed");
                if (rs) ctrl.roll_speed = static_cast<float>(*rs);

                if (ctrl.move_speed < 0.0f || ctrl.look_speed < 0.0f
                    || ctrl.boost_multiplier < 0.0f || ctrl.roll_speed < 0.0f)
                {
                    logger.error("flying_camera_controller on node '"
                        + node.id + "' has negative speed value");
                    return std::nullopt;
                }

                node.flying_camera_controller = ctrl;
            }

            const auto* amc = find_member(
                node_val,
                "actor_movement_controller");
            if (amc && amc->kind == wz::json::JSONValueKind::Object) {
                SceneActorMovementControllerAsset ctrl{};
                auto ms = read_number(*amc, "move_speed");
                if (ms) ctrl.move_speed = static_cast<float>(*ms);
                auto bm = read_number(*amc, "boost_multiplier");
                if (bm) ctrl.boost_multiplier = static_cast<float>(*bm);
                auto movement_space = read_string(*amc, "movement_space");
                if (movement_space) {
                    auto parsed_space =
                        parse_actor_movement_space(*movement_space);
                    if (!parsed_space) {
                        logger.error("actor_movement_controller on node '"
                            + node.id + "' has unknown movement_space '"
                            + std::string(*movement_space) + "'");
                        return std::nullopt;
                    }
                    ctrl.movement_space = *parsed_space;
                }

                if (ctrl.move_speed < 0.0f
                    || ctrl.boost_multiplier < 0.0f)
                {
                    logger.error("actor_movement_controller on node '"
                        + node.id + "' has negative speed value");
                    return std::nullopt;
                }

                node.actor_movement_controller = ctrl;
            }

            const auto* gb = find_member(node_val, "ground_boundary");
            if (gb && gb->kind == wz::json::JSONValueKind::Object) {
                SceneGroundBoundaryAsset boundary{};
                if (!read_float3(*gb, "min", boundary.min)
                    || !read_float3(*gb, "max", boundary.max))
                {
                    logger.error("ground_boundary on node '" + node.id
                        + "' must provide min and max float3 bounds");
                    return std::nullopt;
                }

                if (boundary.min[0] > boundary.max[0]
                    || boundary.min[1] > boundary.max[1]
                    || boundary.min[2] > boundary.max[2])
                {
                    logger.error("ground_boundary on node '" + node.id
                        + "' has min greater than max");
                    return std::nullopt;
                }

                auto constrain_vertical =
                    read_bool(*gb, "constrain_vertical");
                if (constrain_vertical) {
                    boundary.constrain_vertical = *constrain_vertical;
                }
                auto enabled = read_bool(*gb, "enabled");
                if (enabled) {
                    boundary.enabled = *enabled;
                }

                node.ground_boundary = boundary;
            }

            const auto* import_source =
                find_member(node_val, "scene_import_source");
            if (import_source
                && import_source->kind == wz::json::JSONValueKind::Object)
            {
                SceneImportSourceAsset source{};

                if (auto kind_str = read_string(*import_source, "kind")) {
                    auto kind = parse_scene_import_source_kind(*kind_str);
                    if (!kind) {
                        logger.error("scene_import_source on node '"
                            + node.id + "' has unknown kind '"
                            + std::string(*kind_str) + "'");
                        return std::nullopt;
                    }
                    source.kind = *kind;
                }

                if (auto path = read_string(*import_source, "path")) {
                    source.path = std::string(*path);
                }
                if (auto prefix =
                        read_string(*import_source, "import_prefix"))
                {
                    source.import_prefix = std::string(*prefix);
                }
                if (auto scene_index =
                        read_number(*import_source, "scene_index"))
                {
                    if (*scene_index < 0.0 || !std::isfinite(*scene_index)) {
                        logger.error("scene_import_source on node '"
                            + node.id + "' has invalid scene_index");
                        return std::nullopt;
                    }
                    source.scene_index =
                        static_cast<uint32_t>(*scene_index);
                }

                if (source.kind == SceneImportSourceKind::GLB
                    && source.path.empty())
                {
                    logger.error("scene_import_source on node '" + node.id
                        + "' with kind 'glb' missing 'path'");
                    return std::nullopt;
                }

                node.scene_import_source = std::move(source);
            }

            const auto* imported_node =
                find_member(node_val, "imported_node");
            if (imported_node
                && imported_node->kind == wz::json::JSONValueKind::Object)
            {
                SceneImportedNodeAsset imported{};
                if (auto anchor = read_string(*imported_node, "anchor_node")) {
                    imported.anchor_node = std::string(*anchor);
                }
                if (auto prefix =
                        read_string(*imported_node, "import_prefix"))
                {
                    imported.import_prefix = std::string(*prefix);
                }
                if (auto source_node =
                        read_string(*imported_node, "source_node"))
                {
                    imported.source_node_id = std::string(*source_node);
                }
                if (auto missing =
                        read_bool(*imported_node, "missing_source"))
                {
                    imported.missing_source = *missing;
                }

                if (imported.anchor_node.empty()
                    || imported.import_prefix.empty()
                    || imported.source_node_id.empty())
                {
                    logger.error("imported_node on node '" + node.id
                        + "' missing anchor_node, import_prefix, or source_node");
                    return std::nullopt;
                }

                node.imported_node = std::move(imported);
            }

            const auto* ms = find_member(node_val, "mesh_source");
            if (ms && ms->kind == wz::json::JSONValueKind::Object) {
                auto kind_str = read_string(*ms, "kind");
                if (!kind_str) {
                    logger.error("mesh_source on node '" + node.id
                        + "' missing 'kind'");
                    return std::nullopt;
                }

                auto kind = parse_mesh_source_kind(*kind_str);
                if (!kind) {
                    logger.error("mesh_source on node '" + node.id
                        + "' has unknown kind '" + std::string(*kind_str) + "'");
                    return std::nullopt;
                }

                SceneMeshSourceAsset source{};
                source.kind = *kind;

                auto path = read_string(*ms, "path");
                if (path) {
                    source.path = std::string(*path);
                }

                auto mesh_index = read_number(*ms, "mesh_index");
                if (mesh_index) {
                    if (*mesh_index < 0.0 || !std::isfinite(*mesh_index)) {
                        logger.error("mesh_source on node '" + node.id
                            + "' has invalid mesh_index");
                        return std::nullopt;
                    }
                    source.mesh_index = static_cast<uint32_t>(*mesh_index);
                }

                if (source.kind == SceneMeshSourceKind::GLB
                    && source.path.empty())
                {
                    logger.error("mesh_source on node '" + node.id
                        + "' with kind 'glb' missing 'path'");
                    return std::nullopt;
                }

                node.mesh_source = std::move(source);
            }

            const auto* mp = find_member(node_val, "mesh_processing");
            if (mp && mp->kind == wz::json::JSONValueKind::Object) {
                SceneMeshProcessingAsset processing{};

                if (auto enabled = read_bool(*mp, "enabled")) {
                    processing.enabled = *enabled;
                }
                if (auto operation = read_string(*mp, "operation")) {
                    const auto parsed =
                        parse_mesh_processing_operation(*operation);
                    if (!parsed) {
                        logger.error("mesh_processing on node '" + node.id
                            + "' has unknown operation '"
                            + std::string(*operation) + "'");
                        return std::nullopt;
                    }
                    processing.operation = *parsed;
                }
                if (auto region_set_ref =
                        read_string(*mp, "region_set_ref"))
                {
                    processing.region_set_ref =
                        std::string(*region_set_ref);
                }
                if (auto target_vertex_count =
                        read_number(*mp, "target_vertex_count"))
                {
                    if (*target_vertex_count < 0.0
                        || !std::isfinite(*target_vertex_count))
                    {
                        logger.error("mesh_processing on node '" + node.id
                            + "' has invalid target_vertex_count");
                        return std::nullopt;
                    }
                    processing.target_vertex_count =
                        static_cast<uint32_t>(*target_vertex_count);
                }
                if (auto target_triangle_count =
                        read_number(*mp, "target_triangle_count"))
                {
                    if (*target_triangle_count < 0.0
                        || !std::isfinite(*target_triangle_count))
                    {
                        logger.error("mesh_processing on node '" + node.id
                            + "' has invalid target_triangle_count");
                        return std::nullopt;
                    }
                    processing.target_triangle_count =
                        static_cast<uint32_t>(*target_triangle_count);
                }
                if (auto target_ratio = read_number(*mp, "target_ratio")) {
                    if (!std::isfinite(*target_ratio)) {
                        logger.error("mesh_processing on node '" + node.id
                            + "' has invalid target_ratio");
                        return std::nullopt;
                    }
                    processing.target_ratio = static_cast<float>(
                        (std::clamp)(*target_ratio, 0.0, 1.0));
                }
                if (auto preserve_boundary =
                        read_bool(*mp, "preserve_boundary"))
                {
                    processing.preserve_boundary = *preserve_boundary;
                }
                if (auto aspect_ratio = read_number(*mp, "aspect_ratio")) {
                    processing.aspect_ratio =
                        static_cast<float>((std::max)(0.0, *aspect_ratio));
                }
                if (auto edge_length = read_number(*mp, "edge_length")) {
                    processing.edge_length =
                        static_cast<float>((std::max)(0.0, *edge_length));
                }
                if (auto max_valence = read_number(*mp, "max_valence")) {
                    if (*max_valence < 0.0 || !std::isfinite(*max_valence)) {
                        logger.error("mesh_processing on node '" + node.id
                            + "' has invalid max_valence");
                        return std::nullopt;
                    }
                    processing.max_valence =
                        static_cast<uint32_t>(*max_valence);
                }
                if (auto normal_deviation =
                        read_number(*mp, "normal_deviation"))
                {
                    processing.normal_deviation =
                        static_cast<float>(
                            (std::max)(0.0, *normal_deviation));
                }
                if (auto hausdorff_error =
                        read_number(*mp, "hausdorff_error"))
                {
                    processing.hausdorff_error =
                        static_cast<float>(
                            (std::max)(0.0, *hausdorff_error));
                }
                if (auto preview_level_index =
                        read_number(*mp, "preview_level_index"))
                {
                    if (*preview_level_index < 0.0
                        || !std::isfinite(*preview_level_index)
                        || *preview_level_index
                            > static_cast<double>(
                                (std::numeric_limits<uint32_t>::max)()))
                    {
                        logger.error("mesh_processing on node '" + node.id
                            + "' has invalid preview_level_index");
                        return std::nullopt;
                    }
                    processing.preview_level_index =
                        static_cast<uint32_t>(*preview_level_index);
                }
                if (auto asset = read_string(*mp, "source_mesh_asset");
                    asset && !asset->empty())
                {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        logger.error("mesh_processing.source_mesh_asset on node '"
                            + node.id + "' could not be parsed");
                        return std::nullopt;
                    }
                    processing.source_mesh_asset = *key;
                }
                if (auto asset = read_string(*mp, "processed_mesh_asset");
                    asset && !asset->empty())
                {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        logger.error(
                            "mesh_processing.processed_mesh_asset on node '"
                            + node.id + "' could not be parsed");
                        return std::nullopt;
                    }
                    processing.processed_mesh_asset = *key;
                }
                if (auto asset = read_string(*mp, "hierarchy_asset");
                    asset && !asset->empty())
                {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        logger.error("mesh_processing.hierarchy_asset on node '"
                            + node.id + "' could not be parsed");
                        return std::nullopt;
                    }
                    processing.hierarchy_asset = *key;
                }

                node.mesh_processing = processing;
            }

            const auto* mrs = find_member(node_val, "mesh_render_style");
            if (mrs && mrs->kind == wz::json::JSONValueKind::Object) {
                SceneMeshRenderStyleAsset style{};
                auto asset = read_string(*mrs, "asset");
                if (asset) {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        logger.error("mesh_render_style.asset on node '"
                            + node.id + "' could not be parsed");
                        return std::nullopt;
                    }
                    style.style_asset = *key;
                }
                read_mesh_render_layer(*mrs, "wireframe", style.wireframe);
                read_mesh_render_layer(*mrs, "surface", style.surface);

                if (auto kind_str = read_string(*mrs, "kind")) {
                    if (!apply_legacy_mesh_render_style_kind(
                            *kind_str,
                            style,
                            *mrs))
                    {
                        logger.error("mesh_render_style on node '" + node.id
                            + "' has unknown kind '"
                            + std::string(*kind_str) + "'");
                        return std::nullopt;
                    }
                }
                auto alpha = read_number(*mrs, "alpha");
                if (alpha) {
                    style.alpha = static_cast<float>(
                        (std::clamp)(*alpha, 0.0, 1.0));
                }
                auto depth_test = read_bool(*mrs, "depth_test");
                if (depth_test) {
                    style.depth_test = *depth_test;
                }
                auto depth_write = read_bool(*mrs, "depth_write");
                if (depth_write) {
                    style.depth_write = *depth_write;
                }
                auto double_sided = read_bool(*mrs, "double_sided");
                if (double_sided) {
                    style.double_sided = *double_sided;
                }
                auto hidden_line_prepass =
                    read_bool(*mrs, "hidden_line_prepass");
                if (hidden_line_prepass) {
                    style.hidden_line_prepass = *hidden_line_prepass;
                }
                auto field_visualization_enabled =
                    read_bool(*mrs, "field_visualization_enabled");
                if (field_visualization_enabled) {
                    style.field_visualization_enabled =
                        *field_visualization_enabled;
                }
                bool has_explicit_field_visualization_binding = false;
                auto field_channel =
                    read_number(*mrs, "field_visualization_channel_id");
                if (!field_channel) {
                    field_channel = read_number(*mrs, "channel_id");
                    has_explicit_field_visualization_binding =
                        has_explicit_field_visualization_binding
                        || field_channel.has_value();
                }
                if (field_channel) {
                    style.field_visualization_channel_id =
                        static_cast<uint32_t>(
                            (std::max)(0.0, *field_channel));
                }
                auto field_min =
                    read_number(*mrs, "field_visualization_value_min");
                if (!field_min) {
                    field_min = read_number(*mrs, "value_min");
                    has_explicit_field_visualization_binding =
                        has_explicit_field_visualization_binding
                        || field_min.has_value();
                }
                if (field_min) {
                    style.field_visualization_value_min =
                        static_cast<float>(*field_min);
                }
                auto field_max =
                    read_number(*mrs, "field_visualization_value_max");
                if (!field_max) {
                    field_max = read_number(*mrs, "value_max");
                    has_explicit_field_visualization_binding =
                        has_explicit_field_visualization_binding
                        || field_max.has_value();
                }
                if (field_max) {
                    style.field_visualization_value_max =
                        static_cast<float>(*field_max);
                }
                auto field_gamma =
                    read_number(*mrs, "field_visualization_gamma");
                if (!field_gamma) {
                    field_gamma = read_number(*mrs, "gamma");
                    has_explicit_field_visualization_binding =
                        has_explicit_field_visualization_binding
                        || field_gamma.has_value();
                }
                if (field_gamma) {
                    style.field_visualization_gamma =
                        static_cast<float>(
                            (std::max)(0.0001, *field_gamma));
                }
                if (auto palette = read_string(*mrs, "palette")) {
                    has_explicit_field_visualization_binding = true;
                    const auto parsed =
                        parse_mesh_field_visualization_palette(*palette);
                    if (!parsed) {
                        logger.error("mesh_render_style on node '" + node.id
                            + "' has unknown field visualization palette '"
                            + std::string(*palette) + "'");
                        return std::nullopt;
                    }
                    style.field_visualization_palette = *parsed;
                }
                if (auto field_ref = read_string(*mrs, "field_ref")) {
                    has_explicit_field_visualization_binding =
                        has_explicit_field_visualization_binding
                        || !field_ref->empty();
                    style.field_visualization_field_ref =
                        std::string(*field_ref);
                }
                if (auto field_ref = read_string(
                        *mrs,
                        "field_visualization_field_ref"))
                {
                    style.field_visualization_field_ref =
                        std::string(*field_ref);
                }
                if (const auto* mask = find_member(*mrs, "mask");
                    mask && mask->kind == wz::json::JSONValueKind::Object)
                {
                    if (auto enabled = read_bool(*mask, "enabled")) {
                        style.mask.enabled = *enabled;
                    }
                    if (auto domain = read_string(*mask, "domain")) {
                        const auto parsed = parse_mesh_mask_domain(*domain);
                        if (!parsed) {
                            logger.error("mesh_render_style.mask on node '"
                                + node.id + "' has unknown domain '"
                                + std::string(*domain) + "'");
                            return std::nullopt;
                        }
                        style.mask.domain = *parsed;
                    }
                    if (auto projection =
                            read_string(*mask, "projection_mode"))
                    {
                        const auto parsed =
                            parse_mesh_mask_projection_mode(*projection);
                        if (!parsed) {
                            logger.error("mesh_render_style.mask on node '"
                                + node.id
                                + "' has unknown projection_mode '"
                                + std::string(*projection) + "'");
                            return std::nullopt;
                        }
                        style.mask.projection_mode = *parsed;
                    }
                    if (auto overlap = read_string(*mask, "overlap_mode")) {
                        const auto parsed =
                            parse_mesh_mask_overlap_mode(*overlap);
                        if (!parsed) {
                            logger.error("mesh_render_style.mask on node '"
                                + node.id + "' has unknown overlap_mode '"
                                + std::string(*overlap) + "'");
                            return std::nullopt;
                        }
                        style.mask.overlap_mode = *parsed;
                    }
                    if (auto show = read_bool(*mask, "show_unmatched")) {
                        style.mask.show_unmatched = *show;
                    }
                    read_float4(
                        *mask,
                        "unmatched_color",
                        style.mask.unmatched_color);
                    if (auto source_ref =
                            read_string(*mask, "source_field_ref"))
                    {
                        style.mask_source_field_ref =
                            std::string(*source_ref);
                    }
                    if (style.mask_source_field_ref.empty()) {
                        if (auto source_ref =
                                read_string(*mask, "field_ref"))
                        {
                            style.mask_source_field_ref =
                                std::string(*source_ref);
                        }
                    }
                    if (const auto* rules = find_member(*mask, "rules")) {
                        if (rules->kind != wz::json::JSONValueKind::Array) {
                            logger.error("mesh_render_style.mask on node '"
                                + node.id + "' has non-array rules");
                            return std::nullopt;
                        }
                        style.mask.rules.clear();
                        for (const auto& rule_value : rules->array_values) {
                            if (!rule_value
                                || rule_value->kind
                                    != wz::json::JSONValueKind::Object)
                            {
                                logger.error(
                                    "mesh_render_style.mask on node '"
                                    + node.id
                                    + "' has non-object rule");
                                return std::nullopt;
                            }
                            MeshMaskRule rule{};
                            if (auto enabled =
                                    read_bool(*rule_value, "enabled"))
                            {
                                rule.enabled = *enabled;
                            }
                            if (auto channel =
                                    read_number(
                                        *rule_value,
                                        "input_channel_id"))
                            {
                                rule.input_channel_id =
                                    static_cast<uint32_t>(
                                        (std::max)(0.0, *channel));
                            }
                            if (auto lo = read_number(*rule_value, "lo")) {
                                rule.lo = static_cast<float>(*lo);
                            }
                            if (auto hi = read_number(*rule_value, "hi")) {
                                rule.hi = static_cast<float>(*hi);
                            }
                            read_float4(*rule_value, "color", rule.color);
                            if (auto priority =
                                    read_number(*rule_value, "priority"))
                            {
                                rule.priority =
                                    static_cast<int32_t>(*priority);
                            }
                            style.mask.rules.push_back(rule);
                        }
                    }
                    if (!style.mask.enabled
                        && (!style.mask_source_field_ref.empty()
                            || !style.mask.rules.empty()))
                    {
                        style.mask.enabled = true;
                    }
                }
                if (!field_visualization_enabled
                    && has_explicit_field_visualization_binding
                    && !style.field_visualization_field_ref.empty())
                {
                    style.field_visualization_enabled = true;
                }
                if (style.mask.enabled
                    || !style.mask_source_field_ref.empty()
                    || !style.mask.rules.empty())
                {
                    SceneMeshMaskRenderStyleAsset mask_style{};
                    mask_style.enabled = style.mask.enabled;
                    mask_style.source_field_ref =
                        style.mask_source_field_ref;
                    mask_style.source_field_asset =
                        style.mask_source_field_asset;
                    mask_style.mask = style.mask;
                    node.mesh_mask_render_style = std::move(mask_style);

                    style.mask = {};
                    style.mask_source_field_ref.clear();
                    style.mask_source_field_asset = {};
                }
                node.mesh_render_style = style;
            }

            const auto* mmrs =
                find_member(node_val, "mesh_mask_render_style");
            if (mmrs && mmrs->kind == wz::json::JSONValueKind::Object) {
                SceneMeshMaskRenderStyleAsset style{};
                if (!read_mesh_mask_render_style(
                        *mmrs,
                        node.id,
                        "mesh_mask_render_style",
                        logger,
                        style))
                {
                    return std::nullopt;
                }
                node.mesh_mask_render_style = std::move(style);
            }

            const auto* mregion =
                find_member(node_val, "mesh_region_set");
            if (mregion && mregion->kind == wz::json::JSONValueKind::Object) {
                SceneMeshRegionSetAsset region_set{};
                if (!read_mesh_region_set(
                        *mregion,
                        node.id,
                        logger,
                        region_set))
                {
                    return std::nullopt;
                }
                node.mesh_region_set = std::move(region_set);
            }

            const auto* mdfs =
                find_member(node_val, "mesh_derived_field_source");
            if (mdfs && mdfs->kind == wz::json::JSONValueKind::Object) {
                SceneMeshDerivedFieldSourceAsset source{};

                if (auto enabled = read_bool(*mdfs, "enabled")) {
                    source.enabled = *enabled;
                }
                if (auto field_id = read_string(*mdfs, "field_id")) {
                    source.field_id = std::string(*field_id);
                }
                if (source.field_id.empty()) {
                    logger.error("mesh_derived_field_source on node '"
                        + node.id + "' has empty field_id");
                    return std::nullopt;
                }

                if (auto source_kind = read_string(*mdfs, "source_kind")) {
                    auto parsed_source_kind =
                        parse_mesh_derived_field_source_kind(*source_kind);
                    if (!parsed_source_kind) {
                        logger.error("mesh_derived_field_source on node '"
                            + node.id + "' has unknown source_kind '"
                            + std::string(*source_kind) + "'");
                        return std::nullopt;
                    }
                    source.source_kind = *parsed_source_kind;
                }
                else if (auto kind = read_string(*mdfs, "kind")) {
                    auto parsed_kind =
                        parse_mesh_derived_field_source_kind(*kind);
                    if (!parsed_kind) {
                        logger.error("mesh_derived_field_source on node '"
                            + node.id + "' has unknown kind '"
                            + std::string(*kind) + "'");
                        return std::nullopt;
                    }
                    source.source_kind = *parsed_kind;
                }

                if (auto domain = read_string(*mdfs, "domain")) {
                    auto parsed_domain =
                        parse_mesh_derived_field_domain(*domain);
                    if (!parsed_domain) {
                        logger.error("mesh_derived_field_source on node '"
                            + node.id
                            + "' only supports vertex or face domain");
                        return std::nullopt;
                    }
                    source.domain = *parsed_domain;
                }

                if (auto channel_id = read_number(*mdfs, "channel_id")) {
                    if (!std::isfinite(*channel_id)
                        || *channel_id <= 0.0
                        || *channel_id > 4294967295.0
                        || static_cast<double>(
                            static_cast<uint32_t>(*channel_id))
                            != *channel_id)
                    {
                        logger.error("mesh_derived_field_source on node '"
                            + node.id + "' has invalid channel_id");
                        return std::nullopt;
                    }
                    source.channel_id =
                        static_cast<uint32_t>(*channel_id);
                }

                if (auto value_type = read_string(*mdfs, "value_type")) {
                    auto parsed_value_type =
                        parse_mesh_derived_field_value_type(*value_type);
                    if (!parsed_value_type) {
                        logger.error("mesh_derived_field_source on node '"
                            + node.id + "' has unknown value_type '"
                            + std::string(*value_type) + "'");
                        return std::nullopt;
                    }
                    source.value_type = *parsed_value_type;
                }
                if (source.value_type != MeshDerivedFieldValueType::Float1) {
                    logger.error("mesh_derived_field_source on node '"
                        + node.id + "' only supports float1 in v0");
                    return std::nullopt;
                }

                if (auto component = read_string(*mdfs, "component")) {
                    auto parsed_component =
                        parse_mesh_derived_field_component(*component);
                    if (!parsed_component) {
                        logger.error("mesh_derived_field_source on node '"
                            + node.id + "' has unknown component '"
                            + std::string(*component) + "'");
                        return std::nullopt;
                    }
                    source.component = *parsed_component;
                }

                if (auto normalize = read_bool(*mdfs, "normalize")) {
                    source.normalize = *normalize;
                }
                if (auto constant = read_number(*mdfs, "constant_value")) {
                    if (!std::isfinite(*constant)) {
                        logger.error("mesh_derived_field_source on node '"
                            + node.id + "' has invalid constant_value");
                        return std::nullopt;
                    }
                    source.constant_value = static_cast<float>(*constant);
                }

                node.mesh_derived_field_source = std::move(source);
            }

            const auto* msos =
                find_member(node_val, "mesh_sparse_operator_source");
            if (msos && msos->kind == wz::json::JSONValueKind::Object) {
                SceneMeshSparseOperatorSourceAsset source{};

                if (auto enabled = read_bool(*msos, "enabled")) {
                    source.enabled = *enabled;
                }
                if (auto operator_id = read_string(*msos, "operator_id")) {
                    source.operator_id = std::string(*operator_id);
                }
                if (source.operator_id.empty()) {
                    logger.error("mesh_sparse_operator_source on node '"
                        + node.id + "' has empty operator_id");
                    return std::nullopt;
                }

                if (auto kind = read_string(*msos, "kind")) {
                    auto parsed_kind =
                        parse_mesh_sparse_operator_kind(*kind);
                    if (!parsed_kind) {
                        logger.error("mesh_sparse_operator_source on node '"
                            + node.id + "' has unknown kind '"
                            + std::string(*kind) + "'");
                        return std::nullopt;
                    }
                    source.kind = *parsed_kind;
                }
                if (source.kind
                    != MeshSparseOperatorKind::UniformVertexLaplacian)
                {
                    logger.error("mesh_sparse_operator_source on node '"
                        + node.id + "' only supports "
                        "uniform_vertex_laplacian in v0");
                    return std::nullopt;
                }

                if (auto domain = read_string(*msos, "domain")) {
                    auto parsed_domain = parse_mesh_operator_domain(*domain);
                    if (!parsed_domain) {
                        logger.error("mesh_sparse_operator_source on node '"
                            + node.id + "' has unknown domain '"
                            + std::string(*domain) + "'");
                        return std::nullopt;
                    }
                    source.domain = *parsed_domain;
                }
                if (source.domain != MeshOperatorDomain::Vertex
                    && source.domain != MeshOperatorDomain::Face)
                {
                    logger.error("mesh_sparse_operator_source on node '"
                        + node.id + "' only supports vertex or face domain");
                    return std::nullopt;
                }

                if (auto convention =
                        read_string(*msos, "value_convention"))
                {
                    auto parsed_convention =
                        parse_mesh_sparse_operator_value_convention(
                            *convention);
                    if (!parsed_convention) {
                        logger.error("mesh_sparse_operator_source on node '"
                            + node.id + "' has unknown value_convention '"
                            + std::string(*convention) + "'");
                        return std::nullopt;
                    }
                    source.value_convention = *parsed_convention;
                }
                if (source.value_convention
                    != MeshSparseOperatorValueConvention::NeighborWeights)
                {
                    logger.error("mesh_sparse_operator_source on node '"
                        + node.id + "' only supports neighbor_weights in v0");
                    return std::nullopt;
                }

                node.mesh_sparse_operator_source = std::move(source);
            }

            const auto* msaf =
                find_member(node_val, "mesh_sparse_apply_field");
            if (msaf && msaf->kind == wz::json::JSONValueKind::Object) {
                SceneMeshSparseApplyFieldAsset apply{};

                if (auto enabled = read_bool(*msaf, "enabled")) {
                    apply.enabled = *enabled;
                }
                if (auto operator_ref =
                        read_string(*msaf, "operator_ref"))
                {
                    apply.operator_ref = std::string(*operator_ref);
                }
                if (apply.operator_ref.empty()) {
                    logger.error("mesh_sparse_apply_field on node '"
                        + node.id + "' has empty operator_ref");
                    return std::nullopt;
                }
                if (auto input_field_ref =
                        read_string(*msaf, "input_field_ref"))
                {
                    apply.input_field_ref = std::string(*input_field_ref);
                }
                if (apply.input_field_ref.empty()) {
                    logger.error("mesh_sparse_apply_field on node '"
                        + node.id + "' has empty input_field_ref");
                    return std::nullopt;
                }
                if (auto input_channel_id =
                        read_number(*msaf, "input_channel_id"))
                {
                    if (*input_channel_id <= 0.0
                        || !std::isfinite(*input_channel_id))
                    {
                        logger.error("mesh_sparse_apply_field on node '"
                            + node.id
                            + "' has invalid input_channel_id");
                        return std::nullopt;
                    }
                    apply.input_channel_id =
                        static_cast<uint32_t>(*input_channel_id);
                }
                if (apply.input_channel_id == 0u) {
                    logger.error("mesh_sparse_apply_field on node '"
                        + node.id + "' requires input_channel_id");
                    return std::nullopt;
                }
                if (auto output_channel_id =
                        read_number(*msaf, "output_channel_id"))
                {
                    if (*output_channel_id <= 0.0
                        || !std::isfinite(*output_channel_id))
                    {
                        logger.error("mesh_sparse_apply_field on node '"
                            + node.id
                            + "' has invalid output_channel_id");
                        return std::nullopt;
                    }
                    apply.output_channel_id =
                        static_cast<uint32_t>(*output_channel_id);
                }
                if (apply.output_channel_id == 0u) {
                    logger.error("mesh_sparse_apply_field on node '"
                        + node.id + "' requires output_channel_id");
                    return std::nullopt;
                }
                if (auto apply_mode = read_string(*msaf, "apply_mode")) {
                    auto parsed_mode =
                        parse_mesh_sparse_apply_mode(*apply_mode);
                    if (!parsed_mode) {
                        logger.error("mesh_sparse_apply_field on node '"
                            + node.id + "' has unknown apply_mode '"
                            + std::string(*apply_mode) + "'");
                        return std::nullopt;
                    }
                    apply.apply_mode = *parsed_mode;
                }

                node.mesh_sparse_apply_field = std::move(apply);
            }

            const auto* msdb =
                find_member(node_val, "mesh_sparse_diffusion_bands");
            if (msdb && msdb->kind == wz::json::JSONValueKind::Object) {
                SceneMeshSparseDiffusionBandsAsset bands{};

                if (auto enabled = read_bool(*msdb, "enabled")) {
                    bands.enabled = *enabled;
                }
                if (auto operator_ref =
                        read_string(*msdb, "operator_ref"))
                {
                    bands.operator_ref = std::string(*operator_ref);
                }
                if (bands.operator_ref.empty()) {
                    logger.error("mesh_sparse_diffusion_bands on node '"
                        + node.id + "' has empty operator_ref");
                    return std::nullopt;
                }
                if (auto input_field_ref =
                        read_string(*msdb, "input_field_ref"))
                {
                    bands.input_field_ref = std::string(*input_field_ref);
                }
                if (bands.input_field_ref.empty()) {
                    logger.error("mesh_sparse_diffusion_bands on node '"
                        + node.id + "' has empty input_field_ref");
                    return std::nullopt;
                }
                if (auto input_channel_id =
                        read_number(*msdb, "input_channel_id"))
                {
                    if (*input_channel_id <= 0.0
                        || !std::isfinite(*input_channel_id))
                    {
                        logger.error("mesh_sparse_diffusion_bands on node '"
                            + node.id
                            + "' has invalid input_channel_id");
                        return std::nullopt;
                    }
                    bands.input_channel_id =
                        static_cast<uint32_t>(*input_channel_id);
                }
                if (auto output_base_channel_id =
                        read_number(*msdb, "output_base_channel_id"))
                {
                    if (*output_base_channel_id <= 0.0
                        || !std::isfinite(*output_base_channel_id))
                    {
                        logger.error("mesh_sparse_diffusion_bands on node '"
                            + node.id
                            + "' has invalid output_base_channel_id");
                        return std::nullopt;
                    }
                    bands.output_base_channel_id =
                        static_cast<uint32_t>(*output_base_channel_id);
                }
                if (auto band_count =
                        read_number(*msdb, "band_count"))
                {
                    if (*band_count <= 0.0 || !std::isfinite(*band_count)) {
                        logger.error("mesh_sparse_diffusion_bands on node '"
                            + node.id + "' has invalid band_count");
                        return std::nullopt;
                    }
                    bands.band_count = static_cast<uint32_t>(*band_count);
                }
                if (auto iterations_per_band =
                        read_number(*msdb, "iterations_per_band"))
                {
                    if (*iterations_per_band <= 0.0
                        || !std::isfinite(*iterations_per_band))
                    {
                        logger.error("mesh_sparse_diffusion_bands on node '"
                            + node.id
                            + "' has invalid iterations_per_band");
                        return std::nullopt;
                    }
                    bands.iterations_per_band =
                        static_cast<uint32_t>(*iterations_per_band);
                }
                if (auto mode = read_string(*msdb, "mode")) {
                    auto parsed_mode =
                        parse_mesh_sparse_diffusion_mode(*mode);
                    if (!parsed_mode) {
                        logger.error("mesh_sparse_diffusion_bands on node '"
                            + node.id + "' has unknown mode '"
                            + std::string(*mode) + "'");
                        return std::nullopt;
                    }
                    bands.mode = *parsed_mode;
                }
                if (auto tau = read_number(*msdb, "tau")) {
                    if (*tau < 0.0 || !std::isfinite(*tau)) {
                        logger.error("mesh_sparse_diffusion_bands on node '"
                            + node.id + "' has invalid tau");
                        return std::nullopt;
                    }
                    bands.tau = static_cast<float>(*tau);
                }

                node.mesh_sparse_diffusion_bands = std::move(bands);
            }

            const auto* mlms =
                find_member(node_val, "mesh_level_mask_source");
            if (mlms && mlms->kind == wz::json::JSONValueKind::Object) {
                SceneMeshLevelMaskSourceAsset masks{};
                masks.regions.clear();

                if (auto enabled = read_bool(*mlms, "enabled")) {
                    masks.enabled = *enabled;
                }
                if (auto input_field_ref =
                        read_string(*mlms, "input_field_ref"))
                {
                    masks.input_field_ref = std::string(*input_field_ref);
                }
                if (masks.input_field_ref.empty()) {
                    logger.error("mesh_level_mask_source on node '"
                        + node.id + "' has empty input_field_ref");
                    return std::nullopt;
                }
                if (auto output_field_id =
                        read_string(*mlms, "output_field_id"))
                {
                    masks.output_field_id = std::string(*output_field_id);
                }
                if (masks.output_field_id.empty()) {
                    logger.error("mesh_level_mask_source on node '"
                        + node.id + "' has empty output_field_id");
                    return std::nullopt;
                }
                if (auto domain = read_string(*mlms, "domain")) {
                    auto parsed_domain =
                        parse_mesh_derived_field_domain(*domain);
                    if (!parsed_domain) {
                        logger.error("mesh_level_mask_source on node '"
                            + node.id
                            + "' only supports vertex or face domain");
                        return std::nullopt;
                    }
                    masks.domain = *parsed_domain;
                }

                const auto* regions = find_member(*mlms, "regions");
                if (regions) {
                    if (regions->kind != wz::json::JSONValueKind::Array) {
                        logger.error("mesh_level_mask_source on node '"
                            + node.id + "' has non-array regions");
                        return std::nullopt;
                    }
                    for (const auto& region_ptr : regions->array_values) {
                        if (!region_ptr
                            || region_ptr->kind
                                != wz::json::JSONValueKind::Object)
                        {
                            logger.error("mesh_level_mask_source on node '"
                                + node.id
                                + "' has non-object region");
                            return std::nullopt;
                        }

                        const wz::json::JSONValue& region_value =
                            *region_ptr;
                        SceneMeshLevelMaskRegionAsset region{};
                        if (auto input_channel_id =
                                read_number(
                                    region_value,
                                    "input_channel_id"))
                        {
                            if (*input_channel_id <= 0.0
                                || !std::isfinite(*input_channel_id))
                            {
                                logger.error(
                                    "mesh_level_mask_source on node '"
                                    + node.id
                                    + "' has invalid input_channel_id");
                                return std::nullopt;
                            }
                            region.input_channel_id =
                                static_cast<uint32_t>(*input_channel_id);
                        }
                        if (auto output_channel_id =
                                read_number(
                                    region_value,
                                    "output_channel_id"))
                        {
                            if (*output_channel_id <= 0.0
                                || !std::isfinite(*output_channel_id))
                            {
                                logger.error(
                                    "mesh_level_mask_source on node '"
                                    + node.id
                                    + "' has invalid output_channel_id");
                                return std::nullopt;
                            }
                            region.output_channel_id =
                                static_cast<uint32_t>(*output_channel_id);
                        }

                        std::optional<double> min_value =
                            read_number(region_value, "min_value");
                        if (!min_value) {
                            min_value = read_number(region_value, "min");
                        }
                        if (!min_value) {
                            min_value = read_number(region_value, "lo");
                        }
                        if (min_value) {
                            if (!std::isfinite(*min_value)) {
                                logger.error(
                                    "mesh_level_mask_source on node '"
                                    + node.id
                                    + "' has invalid min_value");
                                return std::nullopt;
                            }
                            region.min_value =
                                static_cast<float>(*min_value);
                        }

                        std::optional<double> max_value =
                            read_number(region_value, "max_value");
                        if (!max_value) {
                            max_value = read_number(region_value, "max");
                        }
                        if (!max_value) {
                            max_value = read_number(region_value, "hi");
                        }
                        if (max_value) {
                            if (!std::isfinite(*max_value)) {
                                logger.error(
                                    "mesh_level_mask_source on node '"
                                    + node.id
                                    + "' has invalid max_value");
                                return std::nullopt;
                            }
                            region.max_value =
                                static_cast<float>(*max_value);
                        }

                        if (region.input_channel_id == 0u
                            || region.output_channel_id == 0u)
                        {
                            logger.error("mesh_level_mask_source on node '"
                                + node.id
                                + "' requires nonzero region channels");
                            return std::nullopt;
                        }
                        masks.regions.push_back(region);
                    }
                }
                if (masks.regions.empty()) {
                    masks.regions.push_back(
                        SceneMeshLevelMaskRegionAsset{});
                }

                node.mesh_level_mask_source = std::move(masks);
            }

            const auto* mwa = find_member(node_val, "mesh_wavelet_analysis");
            if (mwa && mwa->kind == wz::json::JSONValueKind::Object) {
                SceneMeshWaveletAnalysisAsset analysis{};
                if (auto enabled = read_bool(*mwa, "enabled")) {
                    analysis.enabled = *enabled;
                }
                if (auto function = read_string(*mwa, "function")) {
                    auto parsed_function =
                        parse_mesh_wavelet_analysis_function(*function);
                    if (!parsed_function) {
                        logger.error("mesh_wavelet_analysis on node '"
                            + node.id + "' has unknown function '"
                            + std::string(*function) + "'");
                        return std::nullopt;
                    }
                    analysis.function = *parsed_function;
                }
                if (auto scale_count = read_number(*mwa, "scale_count")) {
                    analysis.scale_count = static_cast<uint32_t>(
                        (std::clamp)(*scale_count, 1.0, 8.0));
                }
                if (auto lambda =
                        read_number(*mwa, "lambda_max_estimate"))
                {
                    analysis.lambda_max_estimate = static_cast<float>(
                        (std::max)(0.0001, *lambda));
                }
                if (auto gamma = read_number(*mwa, "gamma")) {
                    analysis.gamma = static_cast<float>(
                        (std::max)(0.0001, *gamma));
                }
                node.mesh_wavelet_analysis = analysis;
            }

            const auto* mcf = find_member(node_val, "mesh_compute_field");
            if (mcf && mcf->kind == wz::json::JSONValueKind::Object) {
                SceneMeshComputeFieldAsset component{};

                if (auto enabled = read_bool(*mcf, "enabled")) {
                    component.enabled = *enabled;
                }

                auto kernel_id = read_string(*mcf, "kernel_id");
                if (!kernel_id || kernel_id->empty()) {
                    logger.error("mesh_compute_field on node '" + node.id
                        + "' missing kernel_id");
                    return std::nullopt;
                }
                component.kernel_id = std::string(*kernel_id);

                auto hlsl_path = read_string(*mcf, "hlsl_path");
                if (!hlsl_path || hlsl_path->empty()) {
                    logger.error("mesh_compute_field on node '" + node.id
                        + "' missing hlsl_path");
                    return std::nullopt;
                }
                component.hlsl_path = std::string(*hlsl_path);

                if (auto entry = read_string(*mcf, "entry")) {
                    component.entry = std::string(*entry);
                }
                if (component.entry.empty()) {
                    logger.error("mesh_compute_field on node '" + node.id
                        + "' has empty entry");
                    return std::nullopt;
                }

                if (auto target = read_string(*mcf, "target")) {
                    component.target = std::string(*target);
                }
                if (component.target.empty()) {
                    logger.error("mesh_compute_field on node '" + node.id
                        + "' has empty target");
                    return std::nullopt;
                }

                const auto* group =
                    find_member(*mcf, "thread_group_size");
                if (group) {
                    if (group->kind != wz::json::JSONValueKind::Array
                        || group->array_values.size() < 3)
                    {
                        logger.error("mesh_compute_field on node '" + node.id
                            + "' has invalid thread_group_size");
                        return std::nullopt;
                    }
                    uint32_t* out[3]{
                        &component.thread_group_size_x,
                        &component.thread_group_size_y,
                        &component.thread_group_size_z,
                    };
                    for (size_t i = 0; i < 3; ++i) {
                        const auto& element = group->array_values[i];
                        if (!element
                            || element->kind
                                != wz::json::JSONValueKind::Number
                            || !std::isfinite(element->number_value)
                            || element->number_value <= 0.0
                            || element->number_value > 4294967295.0
                            || static_cast<double>(
                                static_cast<uint32_t>(element->number_value))
                                != element->number_value)
                        {
                            logger.error("mesh_compute_field on node '"
                                + node.id
                                + "' has invalid thread_group_size");
                            return std::nullopt;
                        }
                        *out[i] =
                            static_cast<uint32_t>(element->number_value);
                    }
                }

                const auto* inputs = find_member(*mcf, "inputs");
                if (inputs) {
                    if (inputs->kind != wz::json::JSONValueKind::Array) {
                        logger.error("mesh_compute_field on node '" + node.id
                            + "' has invalid inputs");
                        return std::nullopt;
                    }
                    for (const auto& input_value : inputs->array_values) {
                        if (!input_value
                            || input_value->kind
                                != wz::json::JSONValueKind::String)
                        {
                            logger.error("mesh_compute_field on node '"
                                + node.id + "' has invalid input");
                            return std::nullopt;
                        }
                        auto parsed_input = parse_mesh_compute_input(
                            input_value->string_value);
                        if (!parsed_input) {
                            logger.error("mesh_compute_field on node '"
                                + node.id + "' has unknown input '"
                                + input_value->string_value + "'");
                            return std::nullopt;
                        }
                        component.inputs.push_back(*parsed_input);
                    }
                }

                const auto* channels = find_member(*mcf, "channels");
                if (!channels
                    || channels->kind != wz::json::JSONValueKind::Array
                    || channels->array_values.empty())
                {
                    logger.error("mesh_compute_field on node '" + node.id
                        + "' missing channels");
                    return std::nullopt;
                }
                for (const auto& channel_value : channels->array_values) {
                    if (!channel_value
                        || channel_value->kind
                            != wz::json::JSONValueKind::Object)
                    {
                        logger.error("mesh_compute_field on node '" + node.id
                            + "' has invalid channel");
                        return std::nullopt;
                    }

                    SceneMeshComputeFieldChannelAsset channel{};
                    auto channel_id =
                        read_number(*channel_value, "channel_id");
                    if (!channel_id
                        || !std::isfinite(*channel_id)
                        || *channel_id <= 0.0
                        || *channel_id > 4294967295.0
                        || static_cast<double>(
                            static_cast<uint32_t>(*channel_id))
                            != *channel_id)
                    {
                        logger.error("mesh_compute_field on node '" + node.id
                            + "' has invalid channel_id");
                        return std::nullopt;
                    }
                    channel.channel_id =
                        static_cast<uint32_t>(*channel_id);

                    if (auto value_type =
                            read_string(*channel_value, "value_type"))
                    {
                        auto parsed_value_type =
                            parse_mesh_derived_field_value_type(*value_type);
                        if (!parsed_value_type) {
                            logger.error("mesh_compute_field on node '"
                                + node.id + "' has unknown value_type '"
                                + std::string(*value_type) + "'");
                            return std::nullopt;
                        }
                        channel.value_type = *parsed_value_type;
                    }

                    if (std::ranges::any_of(
                            component.channels,
                            [&](const auto& existing)
                            {
                                return existing.channel_id
                                    == channel.channel_id;
                            }))
                    {
                        logger.error("mesh_compute_field on node '"
                            + node.id + "' has duplicate channel_id");
                        return std::nullopt;
                    }
                    component.channels.push_back(channel);
                }

                const auto* params = find_member(*mcf, "params");
                if (params) {
                    if (params->kind != wz::json::JSONValueKind::Array) {
                        logger.error("mesh_compute_field on node '" + node.id
                            + "' has invalid params");
                        return std::nullopt;
                    }
                    for (const auto& param_value : params->array_values) {
                        if (!param_value
                            || param_value->kind
                                != wz::json::JSONValueKind::Number
                            || !std::isfinite(param_value->number_value)
                            || param_value->number_value < 0.0
                            || param_value->number_value > 4294967295.0
                            || static_cast<double>(
                                static_cast<uint32_t>(
                                    param_value->number_value))
                                != param_value->number_value)
                        {
                            logger.error("mesh_compute_field on node '"
                                + node.id + "' has invalid param");
                            return std::nullopt;
                        }
                        component.params.push_back(
                            static_cast<uint32_t>(
                                param_value->number_value));
                    }
                }

                node.mesh_compute_field = std::move(component);
            }

            const auto* sfs = find_member(node_val, "scalar_field_source");
            if (sfs && sfs->kind == wz::json::JSONValueKind::Object) {
                auto kind_str = read_string(*sfs, "kind");
                if (!kind_str) {
                    logger.error("scalar_field_source on node '" + node.id
                        + "' missing 'kind'");
                    return std::nullopt;
                }

                auto kind = parse_scalar_field_source_kind(*kind_str);
                if (!kind) {
                    logger.error("scalar_field_source on node '" + node.id
                        + "' has unknown kind '" + std::string(*kind_str)
                        + "'");
                    return std::nullopt;
                }

                SceneScalarFieldSourceAsset source{};
                source.kind = *kind;

                auto asset = read_string(*sfs, "asset");
                if (asset && !asset->empty()) {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        const auto it = scalar_field_asset_references.find(
                            std::string(*asset));
                        if (it == scalar_field_asset_references.end()) {
                            logger.error("scalar_field_source.asset on node '"
                                + node.id + "' could not be resolved: "
                                + std::string(*asset));
                            return std::nullopt;
                        }
                        key = it->second;
                    }
                    source.scalar_field_asset = *key;
                }

                auto path = read_string(*sfs, "path");
                if (path) {
                    source.path = std::string(*path);
                }
                if (source.kind == SceneScalarFieldSourceKind::RawF32
                    && source.path.empty()
                    && source.scalar_field_asset == wz::asset::AssetKey{})
                {
                    logger.error("scalar_field_source on node '" + node.id
                        + "' with kind 'raw_f32' missing 'path'");
                    return std::nullopt;
                }

                auto read_dimension =
                    [&](const char* field_name, uint32_t& out) -> bool
                {
                    auto value = read_number(*sfs, field_name);
                    if (!value) {
                        return true;
                    }
                    if (*value < 1.0 || !std::isfinite(*value)) {
                        logger.error("scalar_field_source on node '" + node.id
                            + "' has invalid " + field_name);
                        return false;
                    }
                    out = static_cast<uint32_t>(*value);
                    return true;
                };
                if (!read_dimension("width", source.width)
                    || !read_dimension("height", source.height)
                    || !read_dimension("depth", source.depth))
                {
                    return std::nullopt;
                }

                auto frequency = read_number(*sfs, "frequency");
                if (frequency) {
                    const float value = static_cast<float>(*frequency);
                    if (!std::isfinite(value)) {
                        logger.error("scalar_field_source on node '" + node.id
                            + "' has invalid frequency");
                        return std::nullopt;
                    }
                    source.frequency = value;
                }
                auto amplitude = read_number(*sfs, "amplitude");
                if (amplitude) {
                    const float value = static_cast<float>(*amplitude);
                    if (!std::isfinite(value)) {
                        logger.error("scalar_field_source on node '" + node.id
                            + "' has invalid amplitude");
                        return std::nullopt;
                    }
                    source.amplitude = value;
                }

                node.scalar_field_source = source;
            }

            const auto* vfs = find_member(node_val, "vector_field_source");
            if (vfs && vfs->kind == wz::json::JSONValueKind::Object) {
                auto kind_str = read_string(*vfs, "kind");
                if (!kind_str) {
                    logger.error("vector_field_source on node '" + node.id
                        + "' missing 'kind'");
                    return std::nullopt;
                }

                auto kind = parse_vector_field_source_kind(*kind_str);
                if (!kind) {
                    logger.error("vector_field_source on node '" + node.id
                        + "' has unknown kind '" + std::string(*kind_str)
                        + "'");
                    return std::nullopt;
                }

                SceneVectorFieldSourceAsset source{};
                source.kind = *kind;

                auto asset = read_string(*vfs, "asset");
                if (asset && !asset->empty()) {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        const auto it = vector_field_asset_references.find(
                            std::string(*asset));
                        if (it == vector_field_asset_references.end()) {
                            logger.error("vector_field_source.asset on node '"
                                + node.id + "' could not be resolved: "
                                + std::string(*asset));
                            return std::nullopt;
                        }
                        key = it->second;
                    }
                    source.vector_field_asset = *key;
                }

                auto path = read_string(*vfs, "path");
                if (path) {
                    source.path = std::string(*path);
                }
                if (source.kind == SceneVectorFieldSourceKind::RawF32
                    && source.path.empty()
                    && source.vector_field_asset == wz::asset::AssetKey{})
                {
                    logger.error("vector_field_source on node '" + node.id
                        + "' with kind 'raw_f32' missing 'path'");
                    return std::nullopt;
                }

                auto read_dimension =
                    [&](const char* field_name, uint32_t& out) -> bool
                {
                    auto value = read_number(*vfs, field_name);
                    if (!value) {
                        return true;
                    }
                    if (*value < 1.0 || !std::isfinite(*value)) {
                        logger.error("vector_field_source on node '" + node.id
                            + "' has invalid " + field_name);
                        return false;
                    }
                    out = static_cast<uint32_t>(*value);
                    return true;
                };
                if (!read_dimension("width", source.width)
                    || !read_dimension("height", source.height)
                    || !read_dimension("depth", source.depth))
                {
                    return std::nullopt;
                }

                auto components =
                    read_number(*vfs, "components_per_channel");
                if (components) {
                    if (*components < 2.0
                        || *components > 4.0
                        || !std::isfinite(*components))
                    {
                        logger.error("vector_field_source on node '" + node.id
                            + "' has invalid components_per_channel");
                        return std::nullopt;
                    }
                    source.components_per_channel =
                        static_cast<uint32_t>(*components);
                }

                const auto* channels = find_member(*vfs, "channels");
                if (channels) {
                    if (channels->kind != wz::json::JSONValueKind::Array) {
                        logger.error("vector_field_source on node '" + node.id
                            + "' has invalid channels");
                        return std::nullopt;
                    }

                    source.channels.clear();
                    for (const auto& channel : channels->array_values) {
                        if (!channel
                            || channel->kind != wz::json::JSONValueKind::String
                            || channel->string_value.empty())
                        {
                            logger.error("vector_field_source on node '"
                                + node.id + "' has invalid channel name");
                            return std::nullopt;
                        }
                        source.channels.push_back(VectorFieldChannelDesc{
                            .name = channel->string_value,
                        });
                    }
                    if (source.channels.empty()) {
                        logger.error("vector_field_source on node '" + node.id
                            + "' has no channels");
                        return std::nullopt;
                    }
                }

                node.vector_field_source = source;
            }

            const auto* compute_kernel =
                find_member(node_val, "compute_kernel");
            if (compute_kernel
                && compute_kernel->kind == wz::json::JSONValueKind::Object)
            {
                SceneComputeKernelAsset component{};

                auto kernel_id = read_string(*compute_kernel, "kernel_id");
                if (!kernel_id || kernel_id->empty()) {
                    logger.error("compute_kernel on node '" + node.id
                        + "' missing kernel_id");
                    return std::nullopt;
                }
                component.kernel_id = std::string(*kernel_id);

                auto hlsl_path = read_string(*compute_kernel, "hlsl_path");
                if (!hlsl_path || hlsl_path->empty()) {
                    logger.error("compute_kernel on node '" + node.id
                        + "' missing hlsl_path");
                    return std::nullopt;
                }
                component.hlsl_path = std::string(*hlsl_path);

                if (auto entry = read_string(*compute_kernel, "entry")) {
                    component.entry = std::string(*entry);
                }
                if (component.entry.empty()) {
                    logger.error("compute_kernel on node '" + node.id
                        + "' has empty entry");
                    return std::nullopt;
                }

                if (auto target = read_string(*compute_kernel, "target")) {
                    component.target = std::string(*target);
                }
                if (component.target.empty()) {
                    logger.error("compute_kernel on node '" + node.id
                        + "' has empty target");
                    return std::nullopt;
                }

                auto read_u32 =
                    [&](const wz::json::JSONValue& obj,
                        const char* field_name,
                        uint32_t& out,
                        bool require_positive) -> bool
                {
                    auto value = read_number(obj, field_name);
                    if (!value) {
                        return true;
                    }
                    if (!std::isfinite(*value)
                        || *value < 0.0
                        || *value > 4294967295.0)
                    {
                        logger.error("compute_kernel on node '" + node.id
                            + "' has invalid " + std::string(field_name));
                        return false;
                    }
                    const uint32_t parsed = static_cast<uint32_t>(*value);
                    if (static_cast<double>(parsed) != *value
                        || (require_positive && parsed == 0u))
                    {
                        logger.error("compute_kernel on node '" + node.id
                            + "' has invalid " + std::string(field_name));
                        return false;
                    }
                    out = parsed;
                    return true;
                };

                const auto* group =
                    find_member(*compute_kernel, "thread_group_size");
                if (group) {
                    if (group->kind != wz::json::JSONValueKind::Array
                        || group->array_values.size() < 3)
                    {
                        logger.error("compute_kernel on node '" + node.id
                            + "' has invalid thread_group_size");
                        return std::nullopt;
                    }
                    uint32_t* out[3]{
                        &component.thread_group_size_x,
                        &component.thread_group_size_y,
                        &component.thread_group_size_z,
                    };
                    for (size_t i = 0; i < 3; ++i) {
                        const auto& element = group->array_values[i];
                        if (!element
                            || element->kind
                                != wz::json::JSONValueKind::Number
                            || !std::isfinite(element->number_value)
                            || element->number_value <= 0.0
                            || element->number_value > 4294967295.0)
                        {
                            logger.error("compute_kernel on node '" + node.id
                                + "' has invalid thread_group_size");
                            return std::nullopt;
                        }
                        const uint32_t parsed =
                            static_cast<uint32_t>(element->number_value);
                        if (static_cast<double>(parsed)
                            != element->number_value)
                        {
                            logger.error("compute_kernel on node '" + node.id
                                + "' has invalid thread_group_size");
                            return std::nullopt;
                        }
                        *out[i] = parsed;
                    }
                }
                else {
                    if (!read_u32(
                            *compute_kernel,
                            "thread_group_size_x",
                            component.thread_group_size_x,
                            true)
                        || !read_u32(
                            *compute_kernel,
                            "thread_group_size_y",
                            component.thread_group_size_y,
                            true)
                        || !read_u32(
                            *compute_kernel,
                            "thread_group_size_z",
                            component.thread_group_size_z,
                            true))
                    {
                        return std::nullopt;
                    }
                }

                const auto* ports = find_member(*compute_kernel, "ports");
                if (ports) {
                    if (ports->kind != wz::json::JSONValueKind::Array) {
                        logger.error("compute_kernel on node '" + node.id
                            + "' has invalid ports");
                        return std::nullopt;
                    }
                    for (const auto& port_value : ports->array_values) {
                        if (!port_value
                            || port_value->kind
                                != wz::json::JSONValueKind::Object)
                        {
                            logger.error("compute_kernel on node '" + node.id
                                + "' has invalid port");
                            return std::nullopt;
                        }

                        SceneComputeKernelPortAsset port{};
                        auto name = read_string(*port_value, "name");
                        if (!name || name->empty()) {
                            logger.error("compute_kernel on node '" + node.id
                                + "' has port missing name");
                            return std::nullopt;
                        }
                        port.name = std::string(*name);

                        auto kind = read_string(*port_value, "kind");
                        if (!kind) {
                            logger.error("compute_kernel port '" + port.name
                                + "' on node '" + node.id
                                + "' missing kind");
                            return std::nullopt;
                        }
                        auto parsed_kind =
                            parse_compute_kernel_port_kind(*kind);
                        if (!parsed_kind) {
                            logger.error("compute_kernel port '" + port.name
                                + "' on node '" + node.id
                                + "' has unknown kind '"
                                + std::string(*kind) + "'");
                            return std::nullopt;
                        }
                        port.kind = *parsed_kind;

                        auto direction =
                            read_string(*port_value, "direction");
                        if (!direction) {
                            logger.error("compute_kernel port '" + port.name
                                + "' on node '" + node.id
                                + "' missing direction");
                            return std::nullopt;
                        }
                        auto parsed_direction =
                            parse_compute_kernel_port_direction(*direction);
                        if (!parsed_direction) {
                            logger.error("compute_kernel port '" + port.name
                                + "' on node '" + node.id
                                + "' has unknown direction '"
                                + std::string(*direction) + "'");
                            return std::nullopt;
                        }
                        port.direction = *parsed_direction;

                        const bool is_buffer =
                            port.kind
                            == SceneComputeKernelPortKind::StructuredBuffer;
                        if (is_buffer) {
                            if (auto binding_kind =
                                    read_string(*port_value, "binding_kind"))
                            {
                                auto parsed_binding =
                                    parse_compute_kernel_binding_kind(
                                        *binding_kind);
                                if (!parsed_binding) {
                                    logger.error("compute_kernel port '"
                                        + port.name + "' on node '" + node.id
                                        + "' has unknown binding_kind '"
                                        + std::string(*binding_kind) + "'");
                                    return std::nullopt;
                                }
                                port.binding_kind = *parsed_binding;
                            }
                            if (!read_u32(
                                    *port_value,
                                    "shader_register",
                                    port.shader_register,
                                    false)
                                || !read_u32(
                                    *port_value,
                                    "register_space",
                                    port.register_space,
                                    false)
                                || !read_u32(
                                    *port_value,
                                    "stride_bytes",
                                    port.stride_bytes,
                                    false))
                            {
                                return std::nullopt;
                            }
                            if (find_member(
                                    *port_value,
                                    "root_constant_offset")
                                || find_member(
                                    *port_value,
                                    "root_constant_dwords")
                                || find_member(*port_value, "offset")
                                || find_member(*port_value, "dword_count"))
                            {
                                logger.error("compute_kernel buffer port '"
                                    + port.name + "' on node '" + node.id
                                    + "' has root constant fields");
                                return std::nullopt;
                            }
                        }
                        else {
                            if (find_member(*port_value, "binding_kind")
                                || find_member(
                                    *port_value,
                                    "shader_register")
                                || find_member(
                                    *port_value,
                                    "register_space")
                                || find_member(*port_value, "stride_bytes"))
                            {
                                logger.error("compute_kernel root constant port '"
                                    + port.name + "' on node '" + node.id
                                    + "' has buffer binding fields");
                                return std::nullopt;
                            }
                            if (!read_u32(
                                    *port_value,
                                    "root_constant_offset",
                                    port.root_constant_offset,
                                    false)
                                || !read_u32(
                                    *port_value,
                                    "root_constant_dwords",
                                    port.root_constant_dwords,
                                    true))
                            {
                                return std::nullopt;
                            }
                            if (auto offset =
                                    read_number(*port_value, "offset"))
                            {
                                if (!std::isfinite(*offset)
                                    || *offset < 0.0
                                    || *offset > 4294967295.0)
                                {
                                    logger.error("compute_kernel port '"
                                        + port.name + "' on node '" + node.id
                                        + "' has invalid offset");
                                    return std::nullopt;
                                }
                                const uint32_t parsed_offset =
                                    static_cast<uint32_t>(*offset);
                                if (static_cast<double>(parsed_offset)
                                    != *offset)
                                {
                                    logger.error("compute_kernel port '"
                                        + port.name + "' on node '" + node.id
                                        + "' has invalid offset");
                                    return std::nullopt;
                                }
                                port.root_constant_offset = parsed_offset;
                            }
                            if (auto dwords =
                                    read_number(*port_value, "dword_count"))
                            {
                                if (!std::isfinite(*dwords)
                                    || *dwords <= 0.0
                                    || *dwords > 4294967295.0)
                                {
                                    logger.error("compute_kernel port '"
                                        + port.name + "' on node '" + node.id
                                        + "' has invalid dword_count");
                                    return std::nullopt;
                                }
                                const uint32_t parsed_dwords =
                                    static_cast<uint32_t>(*dwords);
                                if (static_cast<double>(parsed_dwords)
                                    != *dwords)
                                {
                                    logger.error("compute_kernel port '"
                                        + port.name + "' on node '" + node.id
                                        + "' has invalid dword_count");
                                    return std::nullopt;
                                }
                                port.root_constant_dwords = parsed_dwords;
                            }
                            if (port.root_constant_dwords == 0u) {
                                logger.error("compute_kernel port '"
                                    + port.name + "' on node '" + node.id
                                    + "' missing root constant dword count");
                                return std::nullopt;
                            }
                            if (port.root_constant_dwords > 4u) {
                                logger.error("compute_kernel port '"
                                    + port.name + "' on node '" + node.id
                                    + "' has root constant dword count above 4");
                                return std::nullopt;
                            }
                        }

                        component.ports.push_back(std::move(port));
                    }
                }

                node.compute_kernel = std::move(component);
            }

            const auto* render_shader =
                find_member(node_val, "render_shader");
            if (render_shader
                && render_shader->kind == wz::json::JSONValueKind::Object)
            {
                SceneRenderShaderAsset component{};

                auto program_id = read_string(*render_shader, "program_id");
                if (!program_id || program_id->empty()) {
                    logger.error("render_shader on node '" + node.id
                        + "' missing program_id");
                    return std::nullopt;
                }
                component.program_id = std::string(*program_id);

                auto vertex_hlsl_path =
                    read_string(*render_shader, "vertex_hlsl_path");
                if (!vertex_hlsl_path || vertex_hlsl_path->empty()) {
                    logger.error("render_shader on node '" + node.id
                        + "' missing vertex_hlsl_path");
                    return std::nullopt;
                }
                component.vertex_hlsl_path = std::string(*vertex_hlsl_path);

                auto pixel_hlsl_path =
                    read_string(*render_shader, "pixel_hlsl_path");
                if (!pixel_hlsl_path || pixel_hlsl_path->empty()) {
                    logger.error("render_shader on node '" + node.id
                        + "' missing pixel_hlsl_path");
                    return std::nullopt;
                }
                component.pixel_hlsl_path = std::string(*pixel_hlsl_path);

                if (auto v = read_string(*render_shader, "vertex_entry")) {
                    component.vertex_entry = std::string(*v);
                }
                if (auto v = read_string(*render_shader, "pixel_entry")) {
                    component.pixel_entry = std::string(*v);
                }
                if (auto v = read_string(*render_shader, "vertex_target")) {
                    component.vertex_target = std::string(*v);
                }
                if (auto v = read_string(*render_shader, "pixel_target")) {
                    component.pixel_target = std::string(*v);
                }
                if (auto v = read_string(*render_shader, "binding_model")) {
                    component.binding_model = std::string(*v);
                }
                if (auto v = read_string(*render_shader, "input_layout")) {
                    component.input_layout = std::string(*v);
                }
                if (auto v = read_string(*render_shader, "blend")) {
                    component.blend = std::string(*v);
                }
                if (auto v = read_string(*render_shader, "depth")) {
                    component.depth = std::string(*v);
                }
                if (auto v = read_string(*render_shader, "raster")) {
                    component.raster = std::string(*v);
                }

                const auto* descriptor_bindings =
                    find_member(*render_shader, "descriptor_bindings");
                if (descriptor_bindings) {
                    if (descriptor_bindings->kind
                        != wz::json::JSONValueKind::Array)
                    {
                        logger.error("render_shader on node '" + node.id
                            + "' has non-array descriptor_bindings");
                        return std::nullopt;
                    }

                    for (const auto& binding_value :
                         descriptor_bindings->array_values)
                    {
                        if (!binding_value
                            || binding_value->kind
                                != wz::json::JSONValueKind::Object)
                        {
                            logger.error("render_shader on node '" + node.id
                                + "' has non-object descriptor binding");
                            return std::nullopt;
                        }

                        SceneDescriptorBindingAsset binding{};
                        if (auto v = read_string(*binding_value, "kind")) {
                            binding.kind = std::string(*v);
                        }
                        if (binding.kind.empty()) {
                            logger.error("render_shader on node '" + node.id
                                + "' descriptor binding missing kind");
                            return std::nullopt;
                        }

                        if (auto v = read_string(*binding_value, "visibility")) {
                            binding.visibility = std::string(*v);
                        }
                        if (binding.visibility.empty()) {
                            logger.error("render_shader on node '" + node.id
                                + "' descriptor binding missing visibility");
                            return std::nullopt;
                        }

                        if (auto v = read_string(*binding_value, "semantic")) {
                            binding.semantic = std::string(*v);
                        }
                        if (binding.semantic.empty()) {
                            logger.error("render_shader on node '" + node.id
                                + "' descriptor binding missing semantic");
                            return std::nullopt;
                        }

                        auto read_u32 = [&](const char* field,
                            uint32_t& out) -> bool
                        {
                            auto value = read_number(*binding_value, field);
                            if (!value) {
                                return true;
                            }
                            if (*value < 0.0
                                || *value > 4294967295.0
                                || !std::isfinite(*value))
                            {
                                logger.error("render_shader on node '"
                                    + node.id
                                    + "' descriptor binding has invalid "
                                    + field);
                                return false;
                            }
                            out = static_cast<uint32_t>(*value);
                            return true;
                        };

                        if (!read_u32(
                                "shader_register",
                                binding.shader_register)
                            || !read_u32(
                                "register_space",
                                binding.register_space)
                            || !read_u32(
                                "descriptor_count",
                                binding.descriptor_count))
                        {
                            return std::nullopt;
                        }
                        if (binding.descriptor_count == 0) {
                            logger.error("render_shader on node '" + node.id
                                + "' descriptor binding has zero descriptor_count");
                            return std::nullopt;
                        }

                        component.descriptor_bindings.push_back(
                            std::move(binding));
                    }
                }

                node.render_shader = std::move(component);
            }

            std::optional<wz::asset::AssetKey> collision_asset;
            if (!parse_asset_reference_object(
                    node_val,
                    node.id,
                    "collision",
                    logger,
                    collision_asset_references,
                    collision_asset))
            {
                return std::nullopt;
            }
            if (collision_asset) {
                const auto* collision = find_member(node_val, "collision");
                SceneCollisionAsset component{};
                component.collision_asset = *collision_asset;

                auto layer_mask = read_number(*collision, "layer_mask");
                if (layer_mask) {
                    if (*layer_mask < 0.0
                        || *layer_mask > 4294967295.0
                        || !std::isfinite(*layer_mask))
                    {
                        logger.error("collision on node '" + node.id
                            + "' has invalid layer_mask");
                        return std::nullopt;
                    }
                    component.layer_mask =
                        static_cast<uint32_t>(*layer_mask);
                }

                auto collides_with_mask =
                    read_number(*collision, "collides_with_mask");
                if (collides_with_mask) {
                    if (*collides_with_mask < 0.0
                        || *collides_with_mask > 4294967295.0
                        || !std::isfinite(*collides_with_mask))
                    {
                        logger.error("collision on node '" + node.id
                            + "' has invalid collides_with_mask");
                        return std::nullopt;
                    }
                    component.collides_with_mask =
                        static_cast<uint32_t>(*collides_with_mask);
                }

                auto is_trigger = read_bool(*collision, "is_trigger");
                if (is_trigger) {
                    component.is_trigger = *is_trigger;
                }
                auto enabled = read_bool(*collision, "enabled");
                if (enabled) {
                    component.enabled = *enabled;
                }

                node.collision = component;
            }

            const auto* proximity =
                find_member(node_val, "proximity");
            if (proximity
                && proximity->kind == wz::json::JSONValueKind::Object)
            {
                SceneProximityAsset component{};
                auto radius = read_number(*proximity, "radius");
                if (radius) {
                    if (*radius <= 0.0 || !std::isfinite(*radius)) {
                        logger.error("proximity on node '" + node.id
                            + "' has invalid radius");
                        return std::nullopt;
                    }
                    component.radius = static_cast<float>(*radius);
                }

                auto layer_mask = read_number(*proximity, "layer_mask");
                if (layer_mask) {
                    if (*layer_mask < 0.0
                        || *layer_mask > 4294967295.0
                        || !std::isfinite(*layer_mask))
                    {
                        logger.error("proximity on node '" + node.id
                            + "' has invalid layer_mask");
                        return std::nullopt;
                    }
                    component.layer_mask =
                        static_cast<uint32_t>(*layer_mask);
                }

                auto detects_with_mask =
                    read_number(*proximity, "detects_with_mask");
                if (detects_with_mask) {
                    if (*detects_with_mask < 0.0
                        || *detects_with_mask > 4294967295.0
                        || !std::isfinite(*detects_with_mask))
                    {
                        logger.error("proximity on node '" + node.id
                            + "' has invalid detects_with_mask");
                        return std::nullopt;
                    }
                    component.detects_with_mask =
                        static_cast<uint32_t>(*detects_with_mask);
                }

                auto enabled = read_bool(*proximity, "enabled");
                if (enabled) {
                    component.enabled = *enabled;
                }

                node.proximity = component;
            }

            const auto* motion_component = find_member(node_val, "motion");
            if (motion_component
                && motion_component->kind == wz::json::JSONValueKind::Object)
            {
                SceneMotionAsset component{};
                if (find_member(*motion_component, "linear_velocity")) {
                    if (!read_float3(
                            *motion_component,
                            "linear_velocity",
                            component.linear_velocity))
                    {
                        logger.error("motion on node '" + node.id
                            + "' has invalid linear_velocity");
                        return std::nullopt;
                    }
                }
                if (find_member(*motion_component, "angular_velocity")) {
                    if (!read_float3(
                            *motion_component,
                            "angular_velocity",
                            component.angular_velocity))
                    {
                        logger.error("motion on node '" + node.id
                            + "' has invalid angular_velocity");
                        return std::nullopt;
                    }
                }
                if (auto space = read_string(*motion_component, "space")) {
                    if (*space == "world") {
                        component.space = SceneMotionSpace::World;
                    }
                    else if (*space == "local") {
                        component.space = SceneMotionSpace::Local;
                    }
                    else {
                        logger.error("motion on node '" + node.id
                            + "' has invalid space");
                        return std::nullopt;
                    }
                }
                auto terrain_constrained =
                    read_bool(*motion_component, "terrain_constrained");
                if (terrain_constrained) {
                    component.terrain_constrained = *terrain_constrained;
                }
                auto terrain_ride_height =
                    read_number(*motion_component, "terrain_ride_height");
                if (terrain_ride_height) {
                    component.terrain_ride_height =
                        static_cast<float>(*terrain_ride_height);
                }
                auto terrain_footprint_radius =
                    read_number(
                        *motion_component,
                        "terrain_footprint_radius");
                if (terrain_footprint_radius) {
                    component.terrain_footprint_radius =
                        static_cast<float>(*terrain_footprint_radius);
                }
                auto terrain_align_to_surface =
                    read_bool(*motion_component, "terrain_align_to_surface");
                if (terrain_align_to_surface) {
                    component.terrain_align_to_surface =
                        *terrain_align_to_surface;
                }
                auto terrain_alignment_strength =
                    read_number(
                        *motion_component,
                        "terrain_alignment_strength");
                if (terrain_alignment_strength) {
                    component.terrain_alignment_strength =
                        static_cast<float>(*terrain_alignment_strength);
                }
                auto enabled = read_bool(*motion_component, "enabled");
                if (enabled) {
                    component.enabled = *enabled;
                }
                node.motion = component;
            }

            std::optional<wz::asset::AssetKey> terrain_asset;
            if (!parse_asset_reference_object(
                    node_val,
                    node.id,
                    "terrain",
                    logger,
                    terrain_asset_references,
                    terrain_asset))
            {
                return std::nullopt;
            }
            if (terrain_asset) {
                const auto* terrain = find_member(node_val, "terrain");
                SceneTerrainAsset component{};
                component.terrain_asset = *terrain_asset;
                std::optional<wz::asset::AssetKey> visual_proxy_asset;
                if (!parse_asset_reference_object(
                        *terrain,
                        node.id,
                        "visual_proxy",
                        logger,
                        terrain_asset_references,
                        visual_proxy_asset))
                {
                    return std::nullopt;
                }
                if (visual_proxy_asset) {
                    component.visual_proxy_asset = *visual_proxy_asset;
                }
                std::optional<wz::asset::AssetKey>
                    constraint_surface_asset;
                if (!parse_asset_reference_object(
                        *terrain,
                        node.id,
                        "constraint_surface",
                        logger,
                        collision_asset_references,
                        constraint_surface_asset))
                {
                    return std::nullopt;
                }
                if (constraint_surface_asset) {
                    component.constraint_surface_asset =
                        *constraint_surface_asset;
                }
                auto calculate_constraint_surface =
                    read_bool(*terrain, "calculate_constraint_surface");
                if (calculate_constraint_surface) {
                    component.calculate_constraint_surface =
                        *calculate_constraint_surface;
                }
                auto visible = read_bool(*terrain, "visible");
                if (visible) {
                    component.visible = *visible;
                }
                auto queryable = read_bool(*terrain, "queryable");
                if (queryable) {
                    component.queryable = *queryable;
                }
                auto constrain_movement =
                    read_bool(*terrain, "constrain_movement");
                if (constrain_movement) {
                    component.constrain_movement = *constrain_movement;
                }
                node.terrain = component;
            }

            const auto* terrain_render_style =
                find_member(node_val, "terrain_render_style");
            if (terrain_render_style
                && terrain_render_style->kind == wz::json::JSONValueKind::Object)
            {
                SceneTerrainRenderStyleAsset style{};
                auto path = read_string(*terrain_render_style, "path");
                if (path) {
                    auto parsed_path = parse_terrain_render_path(*path);
                    if (!parsed_path) {
                        logger.error("terrain_render_style on node '"
                            + node.id + "' has unknown path '"
                            + std::string(*path) + "'");
                        return std::nullopt;
                    }
                    style.path = *parsed_path;
                }
                auto depth_test = read_bool(*terrain_render_style, "depth_test");
                if (depth_test) {
                    style.depth_test = *depth_test;
                }
                auto depth_write = read_bool(*terrain_render_style, "depth_write");
                if (depth_write) {
                    style.depth_write = *depth_write;
                }
                auto lighting_source = read_string(
                    *terrain_render_style,
                    "lighting_source");
                if (lighting_source) {
                    auto parsed_lighting_source =
                        parse_terrain_lighting_source(*lighting_source);
                    if (!parsed_lighting_source) {
                        logger.error("terrain_render_style on node '"
                            + node.id + "' has unknown lighting_source '"
                            + std::string(*lighting_source) + "'");
                        return std::nullopt;
                    }
                    style.lighting_source = *parsed_lighting_source;
                }
                auto directional_light_node = read_string(
                    *terrain_render_style,
                    "directional_light_node");
                if (directional_light_node) {
                    style.directional_light_node =
                        std::string(*directional_light_node);
                }
                auto ambient_light_node = read_string(
                    *terrain_render_style,
                    "ambient_light_node");
                if (ambient_light_node) {
                    style.ambient_light_node =
                        std::string(*ambient_light_node);
                }
                auto environment_node = read_string(
                    *terrain_render_style,
                    "environment_node");
                if (environment_node) {
                    style.environment_node =
                        std::string(*environment_node);
                }
                auto ambient_strength = read_number(
                    *terrain_render_style,
                    "ambient_strength");
                if (ambient_strength) {
                    style.ambient_strength =
                        static_cast<float>(*ambient_strength);
                }
                auto sky_visibility_strength = read_number(
                    *terrain_render_style,
                    "sky_visibility_strength");
                if (sky_visibility_strength) {
                    style.sky_visibility_strength =
                        static_cast<float>(*sky_visibility_strength);
                }
                auto normal_lighting_strength = read_number(
                    *terrain_render_style,
                    "normal_lighting_strength");
                if (normal_lighting_strength) {
                    style.normal_lighting_strength =
                        static_cast<float>(*normal_lighting_strength);
                }
                auto terrain_bounce_strength = read_number(
                    *terrain_render_style,
                    "terrain_bounce_strength");
                if (terrain_bounce_strength) {
                    style.terrain_bounce_strength =
                        static_cast<float>(*terrain_bounce_strength);
                }
                auto target_pixels_per_triangle = read_number(
                    *terrain_render_style,
                    "target_pixels_per_triangle");
                if (target_pixels_per_triangle) {
                    if (*target_pixels_per_triangle < 0.0
                        || !std::isfinite(*target_pixels_per_triangle))
                    {
                        logger.error("terrain_render_style on node '"
                            + node.id
                            + "' has invalid target_pixels_per_triangle");
                        return std::nullopt;
                    }
                    style.target_pixels_per_triangle =
                        static_cast<float>(*target_pixels_per_triangle);
                }
                auto enable_surfel_lods = read_bool(
                    *terrain_render_style,
                    "enable_surfel_lods");
                if (enable_surfel_lods) {
                    style.enable_surfel_lods = *enable_surfel_lods;
                }
                auto surfel_target_coverage_px = read_number(
                    *terrain_render_style,
                    "surfel_target_coverage_px");
                if (surfel_target_coverage_px) {
                    if (*surfel_target_coverage_px < 1.0
                        || !std::isfinite(*surfel_target_coverage_px))
                    {
                        logger.error("terrain_render_style on node '"
                            + node.id
                            + "' has invalid surfel_target_coverage_px");
                        return std::nullopt;
                    }
                    style.surfel_target_coverage_px =
                        static_cast<float>(*surfel_target_coverage_px);
                }
                auto max_asset_triangle_density = read_number(
                    *terrain_render_style,
                    "max_asset_triangle_density");
                if (max_asset_triangle_density) {
                    if (*max_asset_triangle_density < 0.0
                        || !std::isfinite(*max_asset_triangle_density))
                    {
                        logger.error("terrain_render_style on node '"
                            + node.id
                            + "' has invalid max_asset_triangle_density");
                        return std::nullopt;
                    }
                    style.max_asset_triangle_density =
                        static_cast<float>(*max_asset_triangle_density);
                }
                auto max_screen_triangle_density = read_number(
                    *terrain_render_style,
                    "max_screen_triangle_density");
                if (max_screen_triangle_density) {
                    if (*max_screen_triangle_density < 0.0
                        || !std::isfinite(*max_screen_triangle_density))
                    {
                        logger.error("terrain_render_style on node '"
                            + node.id
                            + "' has invalid max_screen_triangle_density");
                        return std::nullopt;
                    }
                    style.max_screen_triangle_density =
                        static_cast<float>(*max_screen_triangle_density);
                }
                auto visual_chunk_count = read_number(
                    *terrain_render_style,
                    "visual_chunk_count");
                if (visual_chunk_count) {
                    const double value = *visual_chunk_count;
                    if (!std::isfinite(value) || value < 0.0) {
                        logger.error("terrain_render_style on node '"
                            + node.id
                            + "' has invalid visual_chunk_count");
                        return std::nullopt;
                    }
                    const uint32_t chunks =
                        static_cast<uint32_t>(value);
                    const bool exact_integer =
                        static_cast<double>(chunks) == value;
                    const bool allowed =
                        chunks == 512u
                        || chunks == 1024u
                        || chunks == 2048u
                        || chunks == 4096u;
                    if (!exact_integer || !allowed) {
                        logger.error("terrain_render_style on node '"
                            + node.id
                            + "' has invalid visual_chunk_count");
                        return std::nullopt;
                    }
                    style.visual_chunk_count = chunks;
                }

                node.terrain_render_style = style;
            }

            const auto* source = find_member(node_val, "terrain_mesh_source");
            if (source && source->kind == wz::json::JSONValueKind::Object) {
                SceneTerrainMeshSourceAsset component{};

                auto mode = read_string(*source, "mode");
                if (mode) {
                    auto parsed_mode = parse_terrain_mesh_source_mode(*mode);
                    if (!parsed_mode) {
                        logger.error("terrain_mesh_source on node '"
                            + node.id + "' has unknown mode '"
                            + std::string(*mode) + "'");
                        return std::nullopt;
                    }
                    component.mode = *parsed_mode;
                }

                auto source_node = read_string(*source, "source_node");
                if (source_node) {
                    component.source_node = std::string(*source_node);
                    component.mode = SceneTerrainMeshSourceMode::SceneNode;
                }

                auto asset = read_string(*source, "asset");
                if (asset && !asset->empty()) {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        const auto it = mesh_asset_references.find(
                            std::string(*asset));
                        if (it == mesh_asset_references.end()) {
                            logger.error("terrain_mesh_source.asset on node '"
                                + node.id + "' could not be resolved: "
                                + std::string(*asset));
                            return std::nullopt;
                        }
                        key = it->second;
                    }
                    component.mesh_asset = *key;
                    if (component.mode != SceneTerrainMeshSourceMode::SceneNode) {
                        component.mode = SceneTerrainMeshSourceMode::MeshAsset;
                    }
                }

                auto policy = read_string(*source, "height_policy");
                if (policy) {
                    auto parsed_policy =
                        parse_terrain_mesh_height_policy(*policy);
                    if (!parsed_policy) {
                        logger.error("terrain_mesh_source on node '"
                            + node.id + "' has unknown height_policy '"
                            + std::string(*policy) + "'");
                        return std::nullopt;
                    }
                    component.height_policy = *parsed_policy;
                }

                auto min_normal = read_number(
                    *source,
                    "min_surface_normal_y");
                if (min_normal) {
                    const float value = static_cast<float>(*min_normal);
                    if (!std::isfinite(value)
                        || value < -1.0f
                        || value > 1.0f)
                    {
                        logger.error("terrain_mesh_source on node '"
                            + node.id
                            + "' has invalid min_surface_normal_y");
                        return std::nullopt;
                    }
                    component.min_surface_normal_y = value;
                }

                auto include_backfaces = read_bool(
                    *source,
                    "include_backfaces");
                if (include_backfaces) {
                    component.include_backfaces = *include_backfaces;
                }

                node.terrain_mesh_source = component;
            }

            const auto* height_source = find_member(
                node_val,
                "terrain_height_field_source");
            if (height_source
                && height_source->kind == wz::json::JSONValueKind::Object)
            {
                SceneTerrainHeightFieldSourceAsset component{};

                auto mode = read_string(*height_source, "mode");
                if (mode) {
                    auto parsed_mode =
                        parse_terrain_height_field_source_mode(*mode);
                    if (!parsed_mode) {
                        logger.error("terrain_height_field_source on node '"
                            + node.id + "' has unknown mode '"
                            + std::string(*mode) + "'");
                        return std::nullopt;
                    }
                    component.mode = *parsed_mode;
                }

                auto source_node = read_string(*height_source, "source_node");
                if (source_node) {
                    component.source_node = std::string(*source_node);
                    component.mode = SceneTerrainHeightFieldSourceMode::SceneNode;
                }

                auto asset = read_string(*height_source, "asset");
                if (asset && !asset->empty()) {
                    auto key = parse_asset_key_string(*asset);
                    if (!key) {
                        const auto it = scalar_field_asset_references.find(
                            std::string(*asset));
                        if (it == scalar_field_asset_references.end()) {
                            logger.error(
                                "terrain_height_field_source.asset on node '"
                                + node.id + "' could not be resolved: "
                                + std::string(*asset));
                            return std::nullopt;
                        }
                        key = it->second;
                    }
                    component.scalar_field_asset = *key;
                    if (component.mode
                        != SceneTerrainHeightFieldSourceMode::SceneNode)
                    {
                        component.mode =
                            SceneTerrainHeightFieldSourceMode::ScalarFieldAsset;
                    }
                }

                if (const auto* origin = find_member(*height_source, "origin")) {
                    (void)origin;
                    if (!read_float2(*height_source, "origin", component.origin)) {
                        logger.error("terrain_height_field_source on node '"
                            + node.id + "' has invalid origin");
                        return std::nullopt;
                    }
                }
                if (const auto* size = find_member(*height_source, "size")) {
                    (void)size;
                    if (!read_float2(*height_source, "size", component.size)
                        || component.size[0] <= 0.0f
                        || component.size[1] <= 0.0f)
                    {
                        logger.error("terrain_height_field_source on node '"
                            + node.id + "' has invalid size");
                        return std::nullopt;
                    }
                }

                auto vertical_scale =
                    read_number(*height_source, "vertical_scale");
                if (vertical_scale) {
                    const float value = static_cast<float>(*vertical_scale);
                    if (!std::isfinite(value)) {
                        logger.error("terrain_height_field_source on node '"
                            + node.id + "' has invalid vertical_scale");
                        return std::nullopt;
                    }
                    component.vertical_scale = value;
                }
                auto base_height = read_number(*height_source, "base_height");
                if (base_height) {
                    const float value = static_cast<float>(*base_height);
                    if (!std::isfinite(value)) {
                        logger.error("terrain_height_field_source on node '"
                            + node.id + "' has invalid base_height");
                        return std::nullopt;
                    }
                    component.base_height = value;
                }

                node.terrain_height_field_source = component;
            }

            const auto* al = find_member(node_val, "audio_listener");
            if (al && al->kind == wz::json::JSONValueKind::Object) {
                SceneAudioListenerAsset listener{};
                auto active = read_bool(*al, "active");
                if (active) listener.active = *active;
                node.audio_listener = listener;
            }

            const auto* el = find_member(node_val, "event_listener");
            if (el && el->kind == wz::json::JSONValueKind::Object) {
                const auto* channels = find_member(*el, "channels");
                if (channels
                    && channels->kind == wz::json::JSONValueKind::Array)
                {
                    SceneEventListenerAsset evt{};
                    for (const auto& ch : channels->array_values) {
                        if (ch->kind == wz::json::JSONValueKind::String
                            && !ch->string_value.empty())
                        {
                            evt.channels.push_back(ch->string_value);
                        }
                    }
                    if (evt.channels.empty()) {
                        logger.error("event_listener on node '" + node.id
                            + "' has no valid channel names");
                        return std::nullopt;
                    }
                    node.event_listener = evt;
                }
            }

            const auto* et = find_member(node_val, "event_trigger");
            if (et && et->kind == wz::json::JSONValueKind::Object) {
                SceneEventTriggerAsset trigger{};
                auto event = read_string(*et, "event");
                if (event && !event->empty()) {
                    trigger.event = std::string(*event);
                }
                node.event_trigger = std::move(trigger);
            }

            // ── Debug/editor visual descriptor ───────────────────────

            const auto* behavior = find_member(node_val, "behavior");
            if (behavior
                && behavior->kind == wz::json::JSONValueKind::Object)
            {
                auto component = parse_behavior_component(
                    *behavior,
                    "behavior",
                    node.id,
                    logger);
                if (!component) {
                    return std::nullopt;
                }

                node.behavior = std::move(*component);
            }
            const auto* behaviors = find_member(node_val, "behaviors");
            if (behaviors) {
                if (behaviors->kind != wz::json::JSONValueKind::Array) {
                    logger.error("behaviors on node '" + node.id
                        + "' is not an array");
                    return std::nullopt;
                }
                for (const auto& entry : behaviors->array_values) {
                    if (!entry) {
                        continue;
                    }
                    auto component = parse_behavior_component(
                        *entry,
                        "behaviors[]",
                        node.id,
                        logger);
                    if (!component) {
                        return std::nullopt;
                    }
                    node.behaviors.push_back(std::move(*component));
                }
            }

            const auto* dv = find_member(node_val, "auxiliary_visual");
            const char* visual_field = "auxiliary_visual";
            if (!dv) {
                dv = find_member(node_val, "debug_visual");
                visual_field = "debug_visual";
            }
            if (dv && dv->kind == wz::json::JSONValueKind::Object) {
                auto kind_str = read_string(*dv, "kind");
                if (!kind_str) {
                    logger.error(std::string(visual_field) + " on node '"
                        + node.id + "' missing 'kind'");
                    return std::nullopt;
                }

                SceneAuxiliaryVisualAsset dbg{};

                if (*kind_str == "axes") {
                    dbg.kind = SceneAuxiliaryVisualKind::Axes;
                }
                else {
                    logger.error(std::string(visual_field) + " on node '"
                        + node.id + "' has unknown kind '"
                        + std::string(*kind_str) + "'");
                    return std::nullopt;
                }

                auto scale = read_number(*dv, "scale");
                if (scale) {
                    float s = static_cast<float>(*scale);
                    if (s < 0.0f || !std::isfinite(s)) {
                        logger.error(std::string(visual_field) + " on node '"
                            + node.id + "' has invalid scale");
                        return std::nullopt;
                    }
                    dbg.scale = s;
                }

                auto vis = read_bool(*dv, "visible");
                if (vis) dbg.visible = *vis;

                node.debug_visual = dbg;
            }

            const auto* eh = find_member(node_val, "editor_handle");
            if (eh && eh->kind == wz::json::JSONValueKind::Object) {
                auto kind_str = read_string(*eh, "kind");
                if (!kind_str) {
                    logger.error("editor_handle on node '" + node.id
                        + "' missing 'kind'");
                    return std::nullopt;
                }

                SceneEditorHandleAsset handle{};

                if (*kind_str == "translate") {
                    handle.kind = SceneEditorHandleKind::Translate;
                }
                else if (*kind_str == "rotate") {
                    handle.kind = SceneEditorHandleKind::Rotate;
                }
                else if (*kind_str == "scale") {
                    handle.kind = SceneEditorHandleKind::Scale;
                }
                else if (*kind_str == "transform") {
                    handle.kind = SceneEditorHandleKind::Transform;
                }
                else {
                    logger.error("editor_handle on node '" + node.id
                        + "' has unknown kind '" + std::string(*kind_str) + "'");
                    return std::nullopt;
                }

                auto enabled = read_bool(*eh, "enabled");
                if (enabled) handle.enabled = *enabled;

                auto visible = read_bool(*eh, "visible");
                if (visible) handle.visible = *visible;

                auto size = read_number(*eh, "size");
                if (size) {
                    float s = static_cast<float>(*size);
                    if (s < 0.0f || !std::isfinite(s)) {
                        logger.error("editor_handle on node '" + node.id
                            + "' has invalid size");
                        return std::nullopt;
                    }
                    handle.size = s;
                }

                node.editor_handle = handle;
            }

            return node;
        }

        std::optional<SceneAssetData> parse_scene_json(
            const wz::json::JSONDocument& doc,
            wz::Logger& logger,
            const SceneAssetReferenceMap& renderable_asset_references,
            const SceneAssetReferenceMap& collision_asset_references,
            const SceneAssetReferenceMap& terrain_asset_references,
            const SceneAssetReferenceMap& mesh_asset_references,
            const SceneAssetReferenceMap& scalar_field_asset_references,
            const SceneAssetReferenceMap& vector_field_asset_references)
        {
            if (!doc.root || doc.root->kind != wz::json::JSONValueKind::Object) {
                logger.error("scene JSON root is not an object");
                return std::nullopt;
            }

            const auto& root = *doc.root;

            auto schema = read_string(root, "schema");
            if (!schema || *schema != "wozzits.scene.v0") {
                logger.error("scene JSON missing or unrecognized 'schema' field");
                return std::nullopt;
            }

            SceneAssetData scene{};
            scene.name = read_string(root, "name").value_or("unnamed_scene");

            const auto* nodes = find_member(root, "nodes");
            if (!nodes || nodes->kind != wz::json::JSONValueKind::Array) {
                logger.error("scene JSON missing 'nodes' array");
                return std::nullopt;
            }

            for (const auto& nv : nodes->array_values) {
                auto node = parse_node(
                    *nv,
                    logger,
                    renderable_asset_references,
                    collision_asset_references,
                    terrain_asset_references,
                    mesh_asset_references,
                    scalar_field_asset_references,
                    vector_field_asset_references);
                if (!node) return std::nullopt;
                scene.nodes.push_back(std::move(*node));
            }

            const auto* lights = find_member(root, "lights");
            if (lights && lights->kind == wz::json::JSONValueKind::Array) {
                for (const auto& lv : lights->array_values) {
                    if (lv->kind != wz::json::JSONValueKind::Object)
                        continue;
                    SceneLightAsset light{};
                    light.node_id = read_string(*lv, "node_id").value_or("");
                    auto* lr = find_member(*lv, "light");
                    if (lr && lr->kind == wz::json::JSONValueKind::Object) {
                        float pos[3]{}, dir[3]{}, col[3]{ 1.f, 1.f, 1.f };
                        read_float3(*lr, "position", pos);
                        read_float3(*lr, "direction", dir);
                        read_float3(*lr, "color", col);
                        light.light.position = { pos[0], pos[1], pos[2] };
                        light.light.direction = { dir[0], dir[1], dir[2] };
                        light.light.color = { col[0], col[1], col[2] };
                        auto type = read_string(*lr, "type");
                        if (type) {
                            auto parsed_type = parse_scene_light_type(*type);
                            if (!parsed_type) {
                                logger.error("scene light has unknown type '"
                                    + std::string(*type) + "'");
                                return std::nullopt;
                            }
                            light.light.type = *parsed_type;
                        }
                        auto intens = read_number(*lr, "intensity");
                        if (intens) light.light.intensity = static_cast<float>(*intens);
                        auto range = read_number(*lr, "range");
                        if (range) light.light.range = static_cast<float>(*range);
                    }
                    scene.lights.push_back(std::move(light));
                }
            }

            const auto* defaults = find_member(root, "defaults");
            if (defaults && defaults->kind == wz::json::JSONValueKind::Object) {
                auto cam = read_string(*defaults, "active_camera");
                if (cam) scene.defaults.active_camera_node = std::string(*cam);
            }

            return scene;
        }

        std::optional<SceneAssetData> scene_from_imported_gltf_scene(
            const ImportedGLTFScene& imported,
            const SceneFromGLBCompileDesc& desc,
            wz::Logger& logger)
        {
            SceneAssetData scene{};
            scene.name = imported.name.empty() ? "glb_scene" : imported.name;
            scene.nodes.reserve(imported.nodes.size());

            for (const auto& imported_node : imported.nodes) {
                SceneNodeAsset node{};
                node.id = imported_node.id;
                if (imported_node.parent_id)
                    node.parent_id = *imported_node.parent_id;
                node.name = imported_node.name;
                node.local = imported_node.local;

                if (imported_node.mesh_index) {
                    // Attach the per-mesh renderable only when the descriptor
                    // supplies a binding. The imperative create_scene_from_glb
                    // path builds one for every mesh; a graph-authored "Scene from
                    // GLB" node supplies none, so the node compiles as bare
                    // structure (no renderable) and the GLB hierarchy is still
                    // produced. Renderables are attached later via the asset graph
                    // (issue #213 submesh referencing).
                    if (auto renderable = find_glb_renderable_binding(
                            desc, *imported_node.mesh_index))
                    {
                        node.renderable_asset = *renderable;
                    }
                }

                scene.nodes.push_back(std::move(node));
            }

            return scene;
        }
    }

    void register_scene_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        JSONTable& json_table,
        SceneAssetTable& scene_table)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kSceneFromJSONSchema,
            .output_type = kAssetTypeScene,
            .input_ports = {
                { "scene_json", kAssetTypeJSONDocument },
            },
            .compile = [&logger, &json_table, &scene_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                if (dep_handles.empty()) {
                    logger.error("scene node has no JSON document dependency");
                    return compile_failed_node(input);
                }

                const JSONData* json_data = json_table.get(dep_handles[0]);

                if (!json_data) {
                    logger.error("scene JSON document dependency is invalid");
                    return compile_failed_node(input);
                }

                SceneAssetReferenceMap renderable_asset_references;
                SceneAssetReferenceMap collision_asset_references;
                SceneAssetReferenceMap terrain_asset_references;
                SceneAssetReferenceMap mesh_asset_references;
                SceneAssetReferenceMap scalar_field_asset_references;
                SceneAssetReferenceMap vector_field_asset_references;
                if (const auto* desc =
                        std::any_cast<SceneFromJSONCompileDesc>(&input.meta))
                {
                    for (const auto& ref :
                        desc->renderable_asset_references)
                    {
                        if (!ref.uri.empty()
                            && !(ref.key == wz::asset::AssetKey{}))
                        {
                            renderable_asset_references[ref.uri] = ref.key;
                        }
                    }
                    for (const auto& ref :
                        desc->collision_asset_references)
                    {
                        if (!ref.uri.empty()
                            && !(ref.key == wz::asset::AssetKey{}))
                        {
                            collision_asset_references[ref.uri] = ref.key;
                        }
                    }
                    for (const auto& ref :
                        desc->terrain_asset_references)
                    {
                        if (!ref.uri.empty()
                            && !(ref.key == wz::asset::AssetKey{}))
                        {
                            terrain_asset_references[ref.uri] = ref.key;
                        }
                    }
                    for (const auto& ref :
                        desc->mesh_asset_references)
                    {
                        if (!ref.uri.empty()
                            && !(ref.key == wz::asset::AssetKey{}))
                        {
                            mesh_asset_references[ref.uri] = ref.key;
                        }
                    }
                    for (const auto& ref :
                        desc->scalar_field_asset_references)
                    {
                        if (!ref.uri.empty()
                            && !(ref.key == wz::asset::AssetKey{}))
                        {
                            scalar_field_asset_references[ref.uri] = ref.key;
                        }
                    }
                    for (const auto& ref :
                        desc->vector_field_asset_references)
                    {
                        if (!ref.uri.empty()
                            && !(ref.key == wz::asset::AssetKey{}))
                        {
                            vector_field_asset_references[ref.uri] = ref.key;
                        }
                    }
                }

                auto scene = parse_scene_json(
                    json_data->document,
                    logger,
                    renderable_asset_references,
                    collision_asset_references,
                    terrain_asset_references,
                    mesh_asset_references,
                    scalar_field_asset_references,
                    vector_field_asset_references);
                if (!scene) {
                    return compile_failed_node(input);
                }

                wz::asset::ResourceHandle handle =
                    scene_table.add(std::move(*scene));

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kSceneFromGLBSchema,
            .output_type = kAssetTypeScene,
            .input_ports = {
                { "source_file", kAssetTypeBinaryBlob },
            },
            .parameters = {
                {
                    .name = "scene_index",
                    .type = wz::asset::ParamType::Int,
                    .label = "Scene index",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 64.0,
                },
            },
            .compile = [&logger, &scene_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>)
                    -> wz::asset::AssetNode
            {
                if (dep_nodes.empty()) {
                    logger.error("GLB scene node has no file dependency");
                    return compile_failed_node(input);
                }

                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);
                if (!bytes || bytes->empty()) {
                    logger.error("GLB scene file dependency is invalid");
                    return compile_failed_node(input);
                }

                SceneFromGLBCompileDesc editor_desc{};
                const auto* desc =
                    std::any_cast<SceneFromGLBCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        editor_desc = scene_from_glb_desc_from_params(*params);
                        desc = &editor_desc;
                    }
                    else {
                        logger.error(
                            "GLB scene node missing compile descriptor");
                        return compile_failed_node(input);
                    }
                }

                ImportedGLTFScene imported{};
                std::string import_error;
                if (!import_gltf_scene(
                        bytes->data(),
                        bytes->size(),
                        GLTFSceneImportOptions{ .scene_index = desc->scene_index },
                        imported,
                        &import_error))
                {
                    logger.error("failed to import GLB scene: " + import_error);
                    return compile_failed_node(input);
                }

                auto scene = scene_from_imported_gltf_scene(
                    imported,
                    *desc,
                    logger);
                if (!scene)
                    return compile_failed_node(input);

                wz::asset::ResourceHandle handle =
                    scene_table.add(std::move(*scene));

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

} // namespace wz::engine::assets::internal
