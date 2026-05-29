#include <engine/assets/scene/scene_json_export.h>

#include <cstddef>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

namespace wz::engine::assets
{
    namespace
    {
        using wz::json::JSONMember;
        using wz::json::JSONValue;
        using wz::json::JSONValueKind;
        using wz::json::JSONValuePtr;

        JSONValuePtr value(JSONValueKind kind)
        {
            auto out = std::make_unique<JSONValue>();
            out->kind = kind;
            return out;
        }

        JSONValuePtr null_value()
        {
            return value(JSONValueKind::Null);
        }

        JSONValuePtr bool_value(bool v)
        {
            auto out = value(JSONValueKind::Bool);
            out->bool_value = v;
            return out;
        }

        JSONValuePtr number_value(double v)
        {
            auto out = value(JSONValueKind::Number);
            out->number_value = v;
            return out;
        }

        JSONValuePtr string_value(const std::string& v)
        {
            auto out = value(JSONValueKind::String);
            out->string_value = v;
            return out;
        }

        JSONValuePtr string_value(const char* v)
        {
            auto out = value(JSONValueKind::String);
            out->string_value = v;
            return out;
        }

        JSONValuePtr array_value()
        {
            return value(JSONValueKind::Array);
        }

        JSONValuePtr object_value()
        {
            return value(JSONValueKind::Object);
        }

        void add_member(
            JSONValue& object,
            std::string key,
            JSONValuePtr member_value)
        {
            object.object_members.push_back(JSONMember{
                .key = std::move(key),
                .value = std::move(member_value),
            });
        }

        JSONValuePtr float_array(const float* values, size_t count)
        {
            auto arr = array_value();
            for (size_t i = 0; i < count; ++i) {
                arr->array_values.push_back(number_value(values[i]));
            }
            return arr;
        }

        const char* debug_visual_kind_name(SceneDebugVisualKind kind)
        {
            switch (kind) {
            case SceneDebugVisualKind::Axes: return "axes";
            case SceneDebugVisualKind::None:
            default: return "";
            }
        }

        const char* editor_handle_kind_name(SceneEditorHandleKind kind)
        {
            switch (kind) {
            case SceneEditorHandleKind::Translate: return "translate";
            case SceneEditorHandleKind::Rotate: return "rotate";
            case SceneEditorHandleKind::Scale: return "scale";
            case SceneEditorHandleKind::Transform: return "transform";
            case SceneEditorHandleKind::None:
            default: return "";
            }
        }

        const char* actor_movement_space_name(SceneActorMovementSpace space)
        {
            switch (space) {
            case SceneActorMovementSpace::World: return "world";
            case SceneActorMovementSpace::Local: return "local";
            }
            return "world";
        }

        const char* mesh_source_kind_name(SceneMeshSourceKind kind)
        {
            switch (kind) {
            case SceneMeshSourceKind::Placeholder:
                return "placeholder";
            case SceneMeshSourceKind::GLB:
                return "glb";
            case SceneMeshSourceKind::ProceduralCube:
                return "procedural_cube";
            case SceneMeshSourceKind::ProceduralQuad:
                return "procedural_quad";
            case SceneMeshSourceKind::ProceduralTriangle:
                return "procedural_triangle";
            }
            return "placeholder";
        }

        const char* mesh_render_style_kind_name(
            SceneMeshRenderStyleKind kind)
        {
            switch (kind) {
            case SceneMeshRenderStyleKind::Wireframe:
                return "wireframe";
            }
            return "wireframe";
        }

        const char* terrain_render_path_name(SceneTerrainRenderPath path)
        {
            switch (path) {
            case SceneTerrainRenderPath::Auto:
                return "auto";
            case SceneTerrainRenderPath::Surface:
                return "surface";
            case SceneTerrainRenderPath::DebugWireframe:
                return "debug_wireframe";
            case SceneTerrainRenderPath::None:
                return "none";
            }
            return "auto";
        }

        const char* terrain_lighting_source_name(
            SceneTerrainLightingSource source)
        {
            switch (source) {
            case SceneTerrainLightingSource::ExplicitNodes:
                return "explicit_nodes";
            case SceneTerrainLightingSource::SceneDefault:
                return "scene_default";
            case SceneTerrainLightingSource::EnvironmentNode:
                return "environment_node";
            case SceneTerrainLightingSource::Hybrid:
                return "hybrid";
            }
            return "explicit_nodes";
        }

        const char* direct_light_kind_name(DirectLightKind kind)
        {
            switch (kind) {
            case DirectLightKind::Directional:
                return "directional";
            case DirectLightKind::Point:
                return "point";
            case DirectLightKind::Spot:
                return "spot";
            }
            return "directional";
        }

        const char* scene_light_type_name(wz::scene::LightType type)
        {
            switch (type) {
            case wz::scene::LightType::Directional:
                return "directional";
            case wz::scene::LightType::Point:
                return "point";
            case wz::scene::LightType::Spot:
                return "spot";
            case wz::scene::LightType::Ambient:
                return "ambient";
            }
            return "point";
        }

        const char* ambient_lighting_mode_name(AmbientLightingMode mode)
        {
            switch (mode) {
            case AmbientLightingMode::Constant:
                return "constant";
            case AmbientLightingMode::FieldModulated:
                return "field_modulated";
            }
            return "constant";
        }

        const char* ambient_lighting_domain_mapping_name(
            AmbientLightingDomainMapping mapping)
        {
            switch (mapping) {
            case AmbientLightingDomainMapping::TerrainUV:
                return "terrain_uv";
            case AmbientLightingDomainMapping::WorldXZ:
                return "world_xz";
            }
            return "terrain_uv";
        }

        const char* hdri_environment_format_name(
            HDRIEnvironmentFormat format)
        {
            switch (format) {
            case HDRIEnvironmentFormat::Auto:
                return "auto";
            case HDRIEnvironmentFormat::RadianceHDR:
                return "radiance_hdr";
            case HDRIEnvironmentFormat::OpenEXR:
                return "openexr";
            }
            return "auto";
        }

        const char* scalar_field_source_kind_name(
            SceneScalarFieldSourceKind kind)
        {
            switch (kind) {
            case SceneScalarFieldSourceKind::RawF32:
                return "raw_f32";
            case SceneScalarFieldSourceKind::ProceduralGradientX:
                return "procedural_gradient_x";
            case SceneScalarFieldSourceKind::ProceduralGradientY:
                return "procedural_gradient_y";
            case SceneScalarFieldSourceKind::ProceduralRadialGradient:
                return "procedural_radial_gradient";
            case SceneScalarFieldSourceKind::ProceduralCheckerboard:
                return "procedural_checkerboard";
            case SceneScalarFieldSourceKind::ProceduralSineWaves:
                return "procedural_sine_waves";
            }
            return "procedural_gradient_x";
        }

        const char* vector_field_source_kind_name(
            SceneVectorFieldSourceKind kind)
        {
            switch (kind) {
            case SceneVectorFieldSourceKind::RawF32:
                return "raw_f32";
            }
            return "raw_f32";
        }

        const char* terrain_mesh_height_policy_name(
            SceneTerrainMeshHeightPolicy policy)
        {
            switch (policy) {
            case SceneTerrainMeshHeightPolicy::HighestAcceptedSurface:
                return "highest_accepted_surface";
            }
            return "highest_accepted_surface";
        }

        const char* terrain_mesh_source_mode_name(
            SceneTerrainMeshSourceMode mode)
        {
            switch (mode) {
            case SceneTerrainMeshSourceMode::MeshAsset:
                return "mesh_asset";
            case SceneTerrainMeshSourceMode::SceneNode:
                return "scene_node";
            }
            return "mesh_asset";
        }

        const char* terrain_height_field_source_mode_name(
            SceneTerrainHeightFieldSourceMode mode)
        {
            switch (mode) {
            case SceneTerrainHeightFieldSourceMode::ScalarFieldAsset:
                return "scalar_field_asset";
            case SceneTerrainHeightFieldSourceMode::SceneNode:
                return "scene_node";
            }
            return "scalar_field_asset";
        }

        const char* pipeline_name(const SceneRenderableBinding& binding)
        {
            if (binding.node_class.default_surface
                == wz::scene::SurfaceClass::Transparent)
            {
                return "TransparentGeometry";
            }
            return "OpaqueGeometry";
        }

        JSONValuePtr transform_value(const AuthoredTransform& transform)
        {
            auto obj = object_value();
            add_member(*obj, "translation",
                float_array(transform.translation, 3));
            add_member(*obj, "rotation_quat",
                float_array(transform.rotation_quat, 4));
            add_member(*obj, "scale",
                float_array(transform.scale, 3));
            return obj;
        }

        std::string asset_key_string(const wz::asset::AssetKey& key)
        {
            // Transitional concrete AssetKey syntax for renderable.asset.
            // This preserves authored data without requiring symbolic asset
            // URI/name resolution in the scene exporter.
            std::ostringstream out;
            out << "asset-key:"
                << std::hex << std::setfill('0')
                << std::setw(16) << key.content_hash.lo << ':'
                << std::setw(16) << key.content_hash.hi << ':'
                << std::setw(16) << key.schema_hash.lo << ':'
                << std::setw(16) << key.schema_hash.hi << ':'
                << std::setw(16) << key.compiler_hash.lo << ':'
                << std::setw(16) << key.compiler_hash.hi << ':'
                << std::setw(16) << key.deps_hash.lo << ':'
                << std::setw(16) << key.deps_hash.hi;
            return out.str();
        }

        JSONValuePtr renderable_value(const SceneRenderableBinding& binding)
        {
            float bounds_min[3]{
                binding.local_bounds.min.x,
                binding.local_bounds.min.y,
                binding.local_bounds.min.z,
            };
            float bounds_max[3]{
                binding.local_bounds.max.x,
                binding.local_bounds.max.y,
                binding.local_bounds.max.z,
            };

            auto obj = object_value();
            add_member(*obj, "pipeline", string_value(pipeline_name(binding)));
            add_member(*obj, "mesh", number_value(binding.mesh));
            add_member(*obj, "material", number_value(binding.material));

            auto bounds = object_value();
            add_member(*bounds, "min", float_array(bounds_min, 3));
            add_member(*bounds, "max", float_array(bounds_max, 3));
            add_member(*obj, "bounds", std::move(bounds));

            add_member(*obj, "visible", bool_value(binding.visible));
            return obj;
        }

        JSONValuePtr renderable_asset_value(const wz::asset::AssetKey& key)
        {
            auto obj = object_value();
            add_member(*obj, "asset", string_value(asset_key_string(key)));
            return obj;
        }

        JSONValuePtr camera_value(const SceneCameraAsset& camera)
        {
            auto obj = object_value();
            add_member(*obj, "fov_y", number_value(camera.fov_y));
            add_member(*obj, "near", number_value(camera.near_plane));
            add_member(*obj, "far", number_value(camera.far_plane));
            add_member(*obj, "aspect", number_value(camera.aspect));
            return obj;
        }

        JSONValuePtr direct_light_source_value(
            const SceneDirectLightSourceAsset& light)
        {
            auto obj = object_value();
            if (!(light.light_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(light.light_asset)));
            }
            add_member(*obj, "kind",
                string_value(direct_light_kind_name(light.kind)));
            add_member(*obj, "color", float_array(light.color, 3));
            add_member(*obj, "intensity", number_value(light.intensity));
            add_member(*obj, "range", number_value(light.range));
            add_member(*obj, "inner_cone_radians",
                number_value(light.inner_cone_radians));
            add_member(*obj, "outer_cone_radians",
                number_value(light.outer_cone_radians));
            return obj;
        }

        JSONValuePtr ambient_lighting_value(
            const SceneAmbientLightingAsset& lighting)
        {
            auto obj = object_value();
            if (!(lighting.lighting_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(lighting.lighting_asset)));
            }
            add_member(*obj, "mode",
                string_value(ambient_lighting_mode_name(lighting.mode)));
            add_member(*obj, "color", float_array(lighting.color, 3));
            add_member(*obj, "intensity", number_value(lighting.intensity));
            if (!(lighting.intensity_field == wz::asset::AssetKey{})) {
                add_member(*obj, "intensity_field",
                    string_value(asset_key_string(lighting.intensity_field)));
            }
            if (!(lighting.color_field == wz::asset::AssetKey{})) {
                add_member(*obj, "color_field",
                    string_value(asset_key_string(lighting.color_field)));
            }
            add_member(*obj, "domain_mapping",
                string_value(ambient_lighting_domain_mapping_name(
                    lighting.domain_mapping)));
            return obj;
        }

        JSONValuePtr hdri_environment_value(
            const SceneHDRIEnvironmentAsset& environment)
        {
            auto obj = object_value();
            if (!(environment.environment_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(
                        environment.environment_asset)));
            }
            add_member(*obj, "path", string_value(environment.path));
            add_member(*obj, "format",
                string_value(hdri_environment_format_name(
                    environment.format)));
            add_member(*obj, "exposure", number_value(environment.exposure));
            add_member(*obj, "rotation_y_radians",
                number_value(environment.rotation_y_radians));
            add_member(*obj, "lighting_intensity",
                number_value(environment.lighting_intensity));
            add_member(*obj, "reflection_intensity",
                number_value(environment.reflection_intensity));
            add_member(*obj, "background_intensity",
                number_value(environment.background_intensity));
            add_member(*obj, "dominant_light_direction",
                float_array(environment.dominant_light_direction, 3));
            add_member(*obj, "dominant_light_color",
                float_array(environment.dominant_light_color, 3));
            add_member(*obj, "dominant_light_intensity",
                number_value(environment.dominant_light_intensity));
            add_member(*obj, "dominant_light_confidence",
                number_value(environment.dominant_light_confidence));
            return obj;
        }

        JSONValuePtr flying_camera_value(
            const SceneFlyingCameraControllerAsset& controller)
        {
            auto obj = object_value();
            add_member(*obj, "move_speed",
                number_value(controller.move_speed));
            add_member(*obj, "look_speed",
                number_value(controller.look_speed));
            add_member(*obj, "boost_multiplier",
                number_value(controller.boost_multiplier));
            add_member(*obj, "roll_speed",
                number_value(controller.roll_speed));
            return obj;
        }

        JSONValuePtr actor_movement_value(
            const SceneActorMovementControllerAsset& controller)
        {
            auto obj = object_value();
            add_member(*obj, "move_speed",
                number_value(controller.move_speed));
            add_member(*obj, "boost_multiplier",
                number_value(controller.boost_multiplier));
            add_member(*obj, "movement_space",
                string_value(
                    actor_movement_space_name(controller.movement_space)));
            return obj;
        }

        JSONValuePtr ground_boundary_value(
            const SceneGroundBoundaryAsset& boundary)
        {
            auto obj = object_value();
            add_member(*obj, "min", float_array(boundary.min, 3));
            add_member(*obj, "max", float_array(boundary.max, 3));
            add_member(*obj, "constrain_vertical",
                bool_value(boundary.constrain_vertical));
            add_member(*obj, "enabled", bool_value(boundary.enabled));
            return obj;
        }

        JSONValuePtr mesh_source_value(const SceneMeshSourceAsset& source)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(mesh_source_kind_name(source.kind)));
            add_member(*obj, "path", string_value(source.path));
            add_member(*obj, "mesh_index", number_value(source.mesh_index));
            return obj;
        }

        JSONValuePtr mesh_render_style_value(
            const SceneMeshRenderStyleAsset& style)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(mesh_render_style_kind_name(style.kind)));
            add_member(*obj, "depth_test", bool_value(style.depth_test));
            add_member(*obj, "depth_write", bool_value(style.depth_write));
            return obj;
        }

        JSONValuePtr scalar_field_source_value(
            const SceneScalarFieldSourceAsset& source)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(scalar_field_source_kind_name(source.kind)));
            if (!(source.scalar_field_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(source.scalar_field_asset)));
            }
            add_member(*obj, "path", string_value(source.path));
            add_member(*obj, "width", number_value(source.width));
            add_member(*obj, "height", number_value(source.height));
            add_member(*obj, "depth", number_value(source.depth));
            add_member(*obj, "frequency", number_value(source.frequency));
            add_member(*obj, "amplitude", number_value(source.amplitude));
            return obj;
        }

        JSONValuePtr vector_field_source_value(
            const SceneVectorFieldSourceAsset& source)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(vector_field_source_kind_name(source.kind)));
            if (!(source.vector_field_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(source.vector_field_asset)));
            }
            add_member(*obj, "path", string_value(source.path));
            add_member(*obj, "width", number_value(source.width));
            add_member(*obj, "height", number_value(source.height));
            add_member(*obj, "depth", number_value(source.depth));
            add_member(*obj, "components_per_channel",
                number_value(source.components_per_channel));

            auto channels = array_value();
            for (const auto& channel : source.channels) {
                channels->array_values.push_back(string_value(channel.name));
            }
            add_member(*obj, "channels", std::move(channels));
            return obj;
        }

        JSONValuePtr terrain_value(const SceneTerrainAsset& terrain)
        {
            auto obj = object_value();
            add_member(*obj, "asset",
                string_value(asset_key_string(terrain.terrain_asset)));
            add_member(*obj, "visible", bool_value(terrain.visible));
            add_member(*obj, "queryable", bool_value(terrain.queryable));
            add_member(*obj, "constrain_movement",
                bool_value(terrain.constrain_movement));
            return obj;
        }

        JSONValuePtr terrain_render_style_value(
            const SceneTerrainRenderStyleAsset& style)
        {
            auto obj = object_value();
            add_member(*obj, "path",
                string_value(terrain_render_path_name(style.path)));
            add_member(*obj, "depth_test", bool_value(style.depth_test));
            add_member(*obj, "depth_write", bool_value(style.depth_write));
            add_member(*obj, "lighting_source",
                string_value(terrain_lighting_source_name(
                    style.lighting_source)));
            if (!style.directional_light_node.empty()) {
                add_member(*obj, "directional_light_node",
                    string_value(style.directional_light_node));
            }
            if (!style.ambient_light_node.empty()) {
                add_member(*obj, "ambient_light_node",
                    string_value(style.ambient_light_node));
            }
            if (!style.environment_node.empty()) {
                add_member(*obj, "environment_node",
                    string_value(style.environment_node));
            }
            add_member(*obj, "ambient_strength",
                number_value(style.ambient_strength));
            add_member(*obj, "sky_visibility_strength",
                number_value(style.sky_visibility_strength));
            add_member(*obj, "normal_lighting_strength",
                number_value(style.normal_lighting_strength));
            add_member(*obj, "terrain_bounce_strength",
                number_value(style.terrain_bounce_strength));
            return obj;
        }

        JSONValuePtr terrain_mesh_source_value(
            const SceneTerrainMeshSourceAsset& source)
        {
            auto obj = object_value();
            add_member(*obj, "mode",
                string_value(terrain_mesh_source_mode_name(source.mode)));
            if (source.mode == SceneTerrainMeshSourceMode::SceneNode) {
                add_member(*obj, "source_node",
                    string_value(source.source_node));
            }
            if (!(source.mesh_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(source.mesh_asset)));
            }
            add_member(*obj, "height_policy",
                string_value(terrain_mesh_height_policy_name(
                    source.height_policy)));
            add_member(*obj, "min_surface_normal_y",
                number_value(source.min_surface_normal_y));
            add_member(*obj, "include_backfaces",
                bool_value(source.include_backfaces));
            return obj;
        }

        JSONValuePtr terrain_height_field_source_value(
            const SceneTerrainHeightFieldSourceAsset& source)
        {
            auto obj = object_value();
            add_member(*obj, "mode",
                string_value(terrain_height_field_source_mode_name(
                    source.mode)));
            if (source.mode == SceneTerrainHeightFieldSourceMode::SceneNode) {
                add_member(*obj, "source_node",
                    string_value(source.source_node));
            }
            if (!(source.scalar_field_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(source.scalar_field_asset)));
            }
            add_member(*obj, "origin", float_array(source.origin, 2));
            add_member(*obj, "size", float_array(source.size, 2));
            add_member(*obj, "vertical_scale",
                number_value(source.vertical_scale));
            add_member(*obj, "base_height", number_value(source.base_height));
            return obj;
        }

        JSONValuePtr event_listener_value(
            const SceneEventListenerAsset& listener)
        {
            auto obj = object_value();
            auto channels = array_value();
            for (const auto& channel : listener.channels) {
                channels->array_values.push_back(string_value(channel));
            }
            add_member(*obj, "channels", std::move(channels));
            return obj;
        }

        JSONValuePtr debug_visual_value(const SceneDebugVisualAsset& visual)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(debug_visual_kind_name(visual.kind)));
            add_member(*obj, "scale", number_value(visual.scale));
            add_member(*obj, "visible", bool_value(visual.visible));
            return obj;
        }

        JSONValuePtr editor_handle_value(const SceneEditorHandleAsset& handle)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(editor_handle_kind_name(handle.kind)));
            add_member(*obj, "enabled", bool_value(handle.enabled));
            add_member(*obj, "visible", bool_value(handle.visible));
            add_member(*obj, "size", number_value(handle.size));
            return obj;
        }

        JSONValuePtr node_value(const SceneNodeAsset& node)
        {
            auto obj = object_value();
            add_member(*obj, "id", string_value(node.id));
            if (node.parent_id) {
                add_member(*obj, "parent", string_value(*node.parent_id));
            }
            else {
                add_member(*obj, "parent", null_value());
            }
            if (!node.name.empty() && node.name != node.id) {
                add_member(*obj, "name", string_value(node.name));
            }
            add_member(*obj, "transform", transform_value(node.local));
            add_member(*obj, "visible", bool_value(node.visible));
            if (node.motion_type
                == wz::scene::TransformNode::MotionType::Animated)
            {
                add_member(*obj, "motion_type", string_value("Animated"));
            }
            if (node.renderable) {
                add_member(*obj, "debug_renderable",
                    renderable_value(*node.renderable));
            }
            if (node.renderable_asset
                && !(*node.renderable_asset == wz::asset::AssetKey{}))
            {
                add_member(*obj, "renderable",
                    renderable_asset_value(*node.renderable_asset));
            }
            if (node.camera) {
                add_member(*obj, "camera", camera_value(*node.camera));
            }
            if (node.direct_light_source) {
                add_member(*obj, "direct_light_source",
                    direct_light_source_value(*node.direct_light_source));
            }
            if (node.ambient_lighting) {
                add_member(*obj, "ambient_lighting",
                    ambient_lighting_value(*node.ambient_lighting));
            }
            if (node.hdri_environment) {
                add_member(*obj, "hdri_environment",
                    hdri_environment_value(*node.hdri_environment));
            }
            if (node.input_receiver) {
                auto input = object_value();
                add_member(*input, "input_map",
                    string_value(node.input_receiver->input_map));
                add_member(*input, "log_input",
                    bool_value(node.input_receiver->log_input));
                add_member(*obj, "input_receiver", std::move(input));
            }
            if (node.flying_camera_controller) {
                add_member(*obj, "flying_camera_controller",
                    flying_camera_value(*node.flying_camera_controller));
            }
            if (node.actor_movement_controller) {
                add_member(*obj, "actor_movement_controller",
                    actor_movement_value(*node.actor_movement_controller));
            }
            if (node.ground_boundary) {
                add_member(*obj, "ground_boundary",
                    ground_boundary_value(*node.ground_boundary));
            }
            if (node.mesh_source) {
                add_member(*obj, "mesh_source",
                    mesh_source_value(*node.mesh_source));
            }
            if (node.mesh_render_style) {
                add_member(*obj, "mesh_render_style",
                    mesh_render_style_value(*node.mesh_render_style));
            }
            if (node.scalar_field_source) {
                add_member(*obj, "scalar_field_source",
                    scalar_field_source_value(*node.scalar_field_source));
            }
            if (node.vector_field_source) {
                add_member(*obj, "vector_field_source",
                    vector_field_source_value(*node.vector_field_source));
            }
            if (node.terrain
                && !(node.terrain->terrain_asset == wz::asset::AssetKey{}))
            {
                add_member(*obj, "terrain", terrain_value(*node.terrain));
            }
            if (node.terrain_render_style) {
                add_member(*obj, "terrain_render_style",
                    terrain_render_style_value(*node.terrain_render_style));
            }
            if (node.terrain_mesh_source) {
                add_member(*obj, "terrain_mesh_source",
                    terrain_mesh_source_value(*node.terrain_mesh_source));
            }
            if (node.terrain_height_field_source) {
                add_member(*obj, "terrain_height_field_source",
                    terrain_height_field_source_value(
                        *node.terrain_height_field_source));
            }
            if (node.audio_listener) {
                auto audio = object_value();
                add_member(*audio, "active",
                    bool_value(node.audio_listener->active));
                add_member(*obj, "audio_listener", std::move(audio));
            }
            if (node.event_listener) {
                add_member(*obj, "event_listener",
                    event_listener_value(*node.event_listener));
            }
            if (node.debug_visual
                && node.debug_visual->kind != SceneDebugVisualKind::None)
            {
                add_member(*obj, "debug_visual",
                    debug_visual_value(*node.debug_visual));
            }
            if (node.editor_handle
                && node.editor_handle->kind != SceneEditorHandleKind::None)
            {
                add_member(*obj, "editor_handle",
                    editor_handle_value(*node.editor_handle));
            }
            return obj;
        }

        JSONValuePtr light_value(const SceneLightAsset& light)
        {
            float pos[3]{
                light.light.position.x,
                light.light.position.y,
                light.light.position.z,
            };
            float dir[3]{
                light.light.direction.x,
                light.light.direction.y,
                light.light.direction.z,
            };
            float color[3]{
                light.light.color.x,
                light.light.color.y,
                light.light.color.z,
            };

            auto obj = object_value();
            add_member(*obj, "node_id", string_value(light.node_id));

            auto record = object_value();
            add_member(*record, "position", float_array(pos, 3));
            add_member(*record, "direction", float_array(dir, 3));
            add_member(*record, "color", float_array(color, 3));
            add_member(*record, "type",
                string_value(scene_light_type_name(light.light.type)));
            add_member(*record, "intensity",
                number_value(light.light.intensity));
            add_member(*record, "range", number_value(light.light.range));

            add_member(*obj, "light", std::move(record));
            return obj;
        }
    }

    wz::json::JSONDocument export_scene_to_json_document(
        const SceneAssetData& scene)
    {
        wz::json::JSONDocument document{};
        auto root = object_value();

        add_member(*root, "schema", string_value("wozzits.scene.v0"));
        add_member(*root, "name", string_value(scene.name));

        auto nodes = array_value();
        for (const auto& node : scene.nodes) {
            nodes->array_values.push_back(node_value(node));
        }
        add_member(*root, "nodes", std::move(nodes));

        if (!scene.lights.empty()) {
            auto lights = array_value();
            for (const auto& light : scene.lights) {
                lights->array_values.push_back(light_value(light));
            }
            add_member(*root, "lights", std::move(lights));
        }

        if (scene.defaults.active_camera_node) {
            auto defaults = object_value();
            add_member(*defaults, "active_camera",
                string_value(*scene.defaults.active_camera_node));
            add_member(*root, "defaults", std::move(defaults));
        }

        document.root = std::move(root);
        return document;
    }

} // namespace wz::engine::assets
