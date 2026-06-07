// src/engine/assets/scene/scene_compilers.cpp

#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/gltf/gltf_importer.h>
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

        std::optional<SceneImportSourceKind> parse_scene_import_source_kind(
            std::string_view text)
        {
            if (text == "glb") {
                return SceneImportSourceKind::GLB;
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
            for (const auto& binding : desc.mesh_renderables) {
                if (binding.mesh_index == mesh_index
                    && !(binding.renderable_asset == wz::asset::AssetKey{}))
                {
                    return binding.renderable_asset;
                }
            }
            return std::nullopt;
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
                    auto renderable = find_glb_renderable_binding(
                        desc,
                        *imported_node.mesh_index);
                    if (!renderable) {
                        logger.error(
                            "GLB scene node '" + node.id
                            + "' references unregistered mesh "
                            + std::to_string(*imported_node.mesh_index));
                        return std::nullopt;
                    }
                    node.renderable_asset = *renderable;
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

                const auto* desc =
                    std::any_cast<SceneFromGLBCompileDesc>(&input.meta);
                if (!desc) {
                    logger.error("GLB scene node missing compile descriptor");
                    return compile_failed_node(input);
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
