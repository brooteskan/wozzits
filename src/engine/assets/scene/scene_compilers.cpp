// src/engine/assets/scene/scene_compilers.cpp

#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/json/json.h>

#include <scene/compile/scene_node_class.h>
#include <scene/compile/compiled_scene.h>

#include <external/json/json_document.h>
#include <external/json/json_read_helpers.h>

#include <any>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

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
            // Transitional concrete AssetKey syntax for renderable.asset.
            // Symbolic asset://renderables/... refs are resolved separately
            // through SceneFromJSONCompileDesc metadata.
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

        std::optional<SceneMeshRenderStyleKind> parse_mesh_render_style_kind(
            std::string_view text)
        {
            if (text == "wireframe") {
                return SceneMeshRenderStyleKind::Wireframe;
            }
            return std::nullopt;
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

        std::optional<SceneNodeAsset> parse_node(
            const wz::json::JSONValue& node_val,
            wz::Logger& logger,
            const SceneAssetReferenceMap& renderable_asset_references,
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
            if (!parse_asset_reference_object(
                    node_val,
                    node.id,
                    "renderable",
                    logger,
                    renderable_asset_references,
                    node.renderable_asset))
            {
                return std::nullopt;
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

            const auto* mrs = find_member(node_val, "mesh_render_style");
            if (mrs && mrs->kind == wz::json::JSONValueKind::Object) {
                auto kind_str = read_string(*mrs, "kind");
                if (!kind_str) {
                    logger.error("mesh_render_style on node '" + node.id
                        + "' missing 'kind'");
                    return std::nullopt;
                }

                auto kind = parse_mesh_render_style_kind(*kind_str);
                if (!kind) {
                    logger.error("mesh_render_style on node '" + node.id
                        + "' has unknown kind '" + std::string(*kind_str) + "'");
                    return std::nullopt;
                }

                SceneMeshRenderStyleAsset style{};
                style.kind = *kind;
                auto depth_test = read_bool(*mrs, "depth_test");
                if (depth_test) {
                    style.depth_test = *depth_test;
                }
                auto depth_write = read_bool(*mrs, "depth_write");
                if (depth_write) {
                    style.depth_write = *depth_write;
                }

                node.mesh_render_style = style;
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

            // ── Debug/editor visual descriptor ───────────────────────

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
    }

} // namespace wz::engine::assets::internal
