#include <engine/assets/scene/scene_json_export.h>

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iterator>
#include <memory>
#include <ranges>
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

        const char* scene_import_source_kind_name(
            SceneImportSourceKind kind)
        {
            switch (kind) {
            case SceneImportSourceKind::GLB:
                return "glb";
            }
            return "glb";
        }

        const char* mesh_processing_operation_name(
            SceneMeshProcessingOperation operation)
        {
            switch (operation) {
            case SceneMeshProcessingOperation::Decimate:
                return "decimate";
            case SceneMeshProcessingOperation::MeshClusterHierarchyPreview:
                return "cluster_hierarchy_preview";
            case SceneMeshProcessingOperation::DebugTriangleStride:
                return "debug_triangle_stride";
            }
            return "decimate";
        }

        const char* mesh_wavelet_analysis_function_name(
            SceneMeshWaveletAnalysisFunction function)
        {
            switch (function) {
            case SceneMeshWaveletAnalysisFunction::BuiltinDetailHeatV0:
                return "builtin_detail_heat_v0";
            }
            return "builtin_detail_heat_v0";
        }

        const char* mesh_derived_field_source_kind_name(
            SceneMeshDerivedFieldSourceKind kind)
        {
            switch (kind) {
            case SceneMeshDerivedFieldSourceKind::Constant:
                return "constant";
            case SceneMeshDerivedFieldSourceKind::PositionGradient:
                return "position_gradient";
            case SceneMeshDerivedFieldSourceKind::VertexIndexGradient:
                return "vertex_index_gradient";
            case SceneMeshDerivedFieldSourceKind::TriangleCornerCount:
                return "triangle_corner_count";
            case SceneMeshDerivedFieldSourceKind::VertexArea:
                return "vertex_area";
            case SceneMeshDerivedFieldSourceKind::TriangleArea:
                return "triangle_area";
            case SceneMeshDerivedFieldSourceKind::MeanEdgeLength:
                return "mean_edge_length";
            case SceneMeshDerivedFieldSourceKind::InverseAreaDensity:
                return "inverse_area_density";
            case SceneMeshDerivedFieldSourceKind::LogDensity:
                return "log_density";
            }
            return "position_gradient";
        }

        const char* mesh_derived_field_component_name(
            SceneMeshDerivedFieldComponent component)
        {
            switch (component) {
            case SceneMeshDerivedFieldComponent::X:
                return "x";
            case SceneMeshDerivedFieldComponent::Y:
                return "y";
            case SceneMeshDerivedFieldComponent::Z:
                return "z";
            }
            return "y";
        }

        const char* mesh_sparse_operator_kind_name(
            MeshSparseOperatorKind kind)
        {
            switch (kind) {
            case MeshSparseOperatorKind::UniformVertexLaplacian:
                return "uniform_vertex_laplacian";
            }
            return "uniform_vertex_laplacian";
        }

        const char* mesh_operator_domain_name(MeshOperatorDomain domain)
        {
            switch (domain) {
            case MeshOperatorDomain::Vertex:
                return "vertex";
            case MeshOperatorDomain::Edge:
                return "edge";
            case MeshOperatorDomain::Face:
                return "face";
            case MeshOperatorDomain::Corner:
                return "corner";
            }
            return "vertex";
        }

        const char* mesh_derived_field_domain_name(
            MeshDerivedFieldDomain domain)
        {
            switch (domain) {
            case MeshDerivedFieldDomain::Vertex:
                return "vertex";
            case MeshDerivedFieldDomain::Face:
                return "face";
            case MeshDerivedFieldDomain::Edge:
                return "edge";
            case MeshDerivedFieldDomain::Corner:
                return "corner";
            }
            return "vertex";
        }

        const char* mesh_sparse_operator_value_convention_name(
            MeshSparseOperatorValueConvention convention)
        {
            switch (convention) {
            case MeshSparseOperatorValueConvention::NeighborWeights:
                return "neighbor_weights";
            case MeshSparseOperatorValueConvention::FullMatrixEntries:
                return "full_matrix_entries";
            }
            return "neighbor_weights";
        }

        const char* mesh_sparse_apply_mode_name(
            SceneMeshSparseApplyMode mode)
        {
            switch (mode) {
            case SceneMeshSparseApplyMode::Residual:
                return "residual";
            }
            return "residual";
        }

        const char* mesh_sparse_diffusion_mode_name(
            SceneMeshSparseDiffusionMode mode)
        {
            switch (mode) {
            case SceneMeshSparseDiffusionMode::Smooth:
                return "smooth";
            case SceneMeshSparseDiffusionMode::DiffusionStep:
                return "diffusion_step";
            }
            return "smooth";
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

        const char* sky_visual_kind_name(SceneSkyVisualKind kind)
        {
            switch (kind) {
            case SceneSkyVisualKind::None:
                return "none";
            case SceneSkyVisualKind::SolidColor:
                return "solid_color";
            case SceneSkyVisualKind::DirectionDebug:
                return "direction_debug";
            case SceneSkyVisualKind::Gradient:
                return "gradient";
            case SceneSkyVisualKind::EquirectangularTexture:
                return "equirectangular_texture";
            case SceneSkyVisualKind::ScalarField:
                return "scalar_field";
            case SceneSkyVisualKind::VectorField:
                return "vector_field";
            }
            return "none";
        }

        const char* sky_projection_name(SceneSkyProjection projection)
        {
            switch (projection) {
            case SceneSkyProjection::Sphere:
                return "sphere";
            }
            return "sphere";
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

        const char* compute_kernel_port_kind_name(
            SceneComputeKernelPortKind kind)
        {
            switch (kind) {
            case SceneComputeKernelPortKind::StructuredBuffer:
                return "structured_buffer";
            case SceneComputeKernelPortKind::U32:
                return "u32";
            case SceneComputeKernelPortKind::F32:
                return "f32";
            }
            return "structured_buffer";
        }

        const char* compute_kernel_port_direction_name(
            SceneComputeKernelPortDirection direction)
        {
            switch (direction) {
            case SceneComputeKernelPortDirection::Input:
                return "input";
            case SceneComputeKernelPortDirection::Output:
                return "output";
            }
            return "input";
        }

        const char* compute_kernel_binding_kind_name(
            SceneComputeKernelBindingKind kind)
        {
            switch (kind) {
            case SceneComputeKernelBindingKind::SRV:
                return "srv";
            case SceneComputeKernelBindingKind::UAV:
                return "uav";
            }
            return "srv";
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
            // Concrete AssetKey syntax used by asset-reference fields.
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

        JSONValuePtr renderable_asset_value(
            wz::asset::AssetGraphDraftNodeId node_id)
        {
            auto obj = object_value();
            add_member(
                *obj,
                "asset_graph_node_id",
                number_value(static_cast<double>(node_id)));
            return obj;
        }

        JSONValuePtr asset_reference_value(
            const SceneAssetReferenceAsset& reference)
        {
            auto obj = object_value();
            if (!(reference.asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(reference.asset)));
            }
            add_member(*obj, "stable_asset_id",
                string_value(reference.stable_asset_id));
            add_member(*obj, "expected_type",
                number_value(static_cast<uint16_t>(reference.expected_type)));
            if (!reference.label.empty()) {
                add_member(*obj, "label", string_value(reference.label));
            }
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
            add_member(*obj, "rotation_x_radians",
                number_value(environment.rotation_x_radians));
            add_member(*obj, "rotation_y_radians",
                number_value(environment.rotation_y_radians));
            add_member(*obj, "rotation_z_radians",
                number_value(environment.rotation_z_radians));
            add_member(*obj, "lighting_intensity",
                number_value(environment.lighting_intensity));
            add_member(*obj, "reflection_intensity",
                number_value(environment.reflection_intensity));
            add_member(*obj, "background_intensity",
                number_value(environment.background_intensity));
            add_member(*obj, "lighting_sample_resolution",
                number_value(environment.lighting_sample_resolution));
            add_member(*obj, "environment_light_color",
                float_array(environment.environment_light_color, 3));
            add_member(*obj, "environment_light_intensity",
                number_value(environment.environment_light_intensity));
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

        JSONValuePtr sky_visual_value(
            const SceneSkyVisualAsset& visual)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(sky_visual_kind_name(visual.kind)));
            add_member(*obj, "solid_color",
                float_array(visual.solid_color, 3));
            add_member(*obj, "gradient_top_color",
                float_array(visual.gradient_top_color, 3));
            add_member(*obj, "gradient_bottom_color",
                float_array(visual.gradient_bottom_color, 3));
            if (!(visual.texture_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "texture_asset",
                    string_value(asset_key_string(visual.texture_asset)));
            }
            if (!visual.texture_path.empty()) {
                add_member(*obj, "texture_path",
                    string_value(visual.texture_path));
                add_member(*obj, "texture_format",
                    string_value(
                        hdri_environment_format_name(visual.texture_format)));
            }
            if (!(visual.scalar_field_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "scalar_field_asset",
                    string_value(asset_key_string(visual.scalar_field_asset)));
            }
            if (!visual.scalar_field_node.empty()) {
                add_member(*obj, "scalar_field_node",
                    string_value(visual.scalar_field_node));
            }
            if (!(visual.vector_field_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "vector_field_asset",
                    string_value(asset_key_string(visual.vector_field_asset)));
            }
            if (!visual.vector_field_node.empty()) {
                add_member(*obj, "vector_field_node",
                    string_value(visual.vector_field_node));
            }
            add_member(*obj, "exposure", number_value(visual.exposure));
            add_member(*obj, "rotation_x_radians",
                number_value(visual.rotation_x_radians));
            add_member(*obj, "rotation_y_radians",
                number_value(visual.rotation_y_radians));
            add_member(*obj, "rotation_z_radians",
                number_value(visual.rotation_z_radians));
            return obj;
        }

        JSONValuePtr sky_surface_value(
            const SceneSkySurfaceAsset& surface)
        {
            auto obj = object_value();
            if (!surface.visual_node.empty()) {
                add_member(*obj, "visual_node",
                    string_value(surface.visual_node));
            }
            add_member(*obj, "projection",
                string_value(sky_projection_name(surface.projection)));
            add_member(*obj, "radius", number_value(surface.radius));
            add_member(*obj, "visible_to_camera",
                bool_value(surface.visible_to_camera));
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

        JSONValuePtr mesh_processing_value(
            const SceneMeshProcessingAsset& processing)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(processing.enabled));
            add_member(*obj, "operation",
                string_value(mesh_processing_operation_name(
                    processing.operation)));
            if (!processing.region_set_ref.empty()) {
                add_member(*obj, "region_set_ref",
                    string_value(processing.region_set_ref));
            }
            add_member(*obj, "target_vertex_count",
                number_value(processing.target_vertex_count));
            add_member(*obj, "target_triangle_count",
                number_value(processing.target_triangle_count));
            add_member(*obj, "target_ratio",
                number_value(processing.target_ratio));
            add_member(*obj, "preserve_boundary",
                bool_value(processing.preserve_boundary));
            add_member(*obj, "aspect_ratio",
                number_value(processing.aspect_ratio));
            add_member(*obj, "edge_length",
                number_value(processing.edge_length));
            add_member(*obj, "max_valence",
                number_value(processing.max_valence));
            add_member(*obj, "normal_deviation",
                number_value(processing.normal_deviation));
            add_member(*obj, "hausdorff_error",
                number_value(processing.hausdorff_error));
            add_member(*obj, "preview_level_index",
                number_value(processing.preview_level_index));
            if (!(processing.source_mesh_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "source_mesh_asset",
                    string_value(asset_key_string(
                        processing.source_mesh_asset)));
            }
            if (!(processing.processed_mesh_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "processed_mesh_asset",
                    string_value(asset_key_string(
                        processing.processed_mesh_asset)));
            }
            if (!(processing.hierarchy_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "hierarchy_asset",
                    string_value(asset_key_string(
                        processing.hierarchy_asset)));
            }
            return obj;
        }

        JSONValuePtr scene_import_source_value(
            const SceneImportSourceAsset& source)
        {
            auto obj = object_value();
            add_member(*obj, "kind",
                string_value(scene_import_source_kind_name(source.kind)));
            add_member(*obj, "path", string_value(source.path));
            add_member(*obj, "import_prefix",
                string_value(source.import_prefix));
            if (source.scene_index) {
                add_member(*obj, "scene_index",
                    number_value(*source.scene_index));
            }
            return obj;
        }

        JSONValuePtr imported_node_value(
            const SceneImportedNodeAsset& imported)
        {
            auto obj = object_value();
            add_member(*obj, "anchor_node",
                string_value(imported.anchor_node));
            add_member(*obj, "import_prefix",
                string_value(imported.import_prefix));
            add_member(*obj, "source_node",
                string_value(imported.source_node_id));
            add_member(*obj, "missing_source",
                bool_value(imported.missing_source));
            return obj;
        }

        JSONValuePtr mesh_render_layer_value(
            const SceneMeshRenderLayerAsset& layer)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(layer.enabled));
            add_member(*obj, "color", float_array(layer.color, 4));
            add_member(*obj, "emissive_strength",
                number_value(layer.emissive_strength));
            return obj;
        }

        const char* mesh_field_visualization_palette_name(
            MeshFieldVisualizationPalette palette)
        {
            switch (palette) {
            case MeshFieldVisualizationPalette::Heat:
                return "heat";
            case MeshFieldVisualizationPalette::Grayscale:
                return "grayscale";
            case MeshFieldVisualizationPalette::Diverging:
                return "diverging";
            }
            return "heat";
        }

        const char* mesh_mask_domain_name(MeshMaskDomain domain)
        {
            switch (domain) {
            case MeshMaskDomain::Face:
                return "face";
            case MeshMaskDomain::Vertex:
                return "vertex";
            case MeshMaskDomain::Edge:
                return "edge";
            }
            return "face";
        }

        const char* mesh_mask_projection_mode_name(
            MeshMaskProjectionMode mode)
        {
            switch (mode) {
            case MeshMaskProjectionMode::Direct:
                return "direct";
            case MeshMaskProjectionMode::Any:
                return "any";
            case MeshMaskProjectionMode::All:
                return "all";
            case MeshMaskProjectionMode::Majority:
                return "majority";
            }
            return "direct";
        }

        const char* mesh_mask_overlap_mode_name(MeshMaskOverlapMode mode)
        {
            switch (mode) {
            case MeshMaskOverlapMode::Priority:
                return "priority";
            case MeshMaskOverlapMode::AlphaBlend:
                return "alpha_blend";
            }
            return "priority";
        }

        const char* mesh_mask_render_mesh_input_name(
            SceneMeshMaskRenderMeshInput input)
        {
            switch (input) {
            case SceneMeshMaskRenderMeshInput::Source:
                return "source";
            case SceneMeshMaskRenderMeshInput::Processed:
                return "processed";
            }
            return "source";
        }

        const char* mesh_region_set_intent_name(
            SceneMeshRegionSetIntent intent)
        {
            switch (intent) {
            case SceneMeshRegionSetIntent::MaterialMask:
                return "material_mask";
            case SceneMeshRegionSetIntent::RemeshSubset:
                return "remesh_subset";
            }
            return "remesh_subset";
        }

        JSONValuePtr mesh_mask_rule_value(const MeshMaskRule& rule)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(rule.enabled));
            add_member(*obj, "input_channel_id",
                number_value(rule.input_channel_id));
            add_member(*obj, "lo", number_value(rule.lo));
            add_member(*obj, "hi", number_value(rule.hi));
            add_member(*obj, "color", float_array(rule.color, 4));
            add_member(*obj, "priority", number_value(rule.priority));
            return obj;
        }

        JSONValuePtr mesh_mask_value(
            const MeshMaskRenderStyleData& mask,
            const std::string& source_field_ref)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(mask.enabled));
            if (!source_field_ref.empty()) {
                add_member(*obj, "source_field_ref",
                    string_value(source_field_ref));
            }
            add_member(*obj, "domain",
                string_value(mesh_mask_domain_name(mask.domain)));
            add_member(*obj, "projection_mode",
                string_value(mesh_mask_projection_mode_name(
                    mask.projection_mode)));
            add_member(*obj, "overlap_mode",
                string_value(mesh_mask_overlap_mode_name(mask.overlap_mode)));
            add_member(*obj, "unmatched_color",
                float_array(mask.unmatched_color, 4));
            add_member(*obj, "show_unmatched",
                bool_value(mask.show_unmatched));

            auto rules = array_value();
            std::ranges::transform(
                mask.rules,
                std::back_inserter(rules->array_values),
                [](const MeshMaskRule& rule) {
                    return mesh_mask_rule_value(rule);
                });
            add_member(*obj, "rules", std::move(rules));
            return obj;
        }

        JSONValuePtr mesh_render_style_value(
            const SceneMeshRenderStyleAsset& style)
        {
            auto obj = object_value();
            if (!(style.style_asset == wz::asset::AssetKey{})) {
                add_member(*obj, "asset",
                    string_value(asset_key_string(style.style_asset)));
            }
            add_member(*obj, "wireframe",
                mesh_render_layer_value(style.wireframe));
            add_member(*obj, "surface",
                mesh_render_layer_value(style.surface));
            add_member(*obj, "alpha", number_value(style.alpha));
            add_member(*obj, "depth_test", bool_value(style.depth_test));
            add_member(*obj, "depth_write", bool_value(style.depth_write));
            add_member(*obj, "double_sided", bool_value(style.double_sided));
            add_member(*obj, "hidden_line_prepass",
                bool_value(style.hidden_line_prepass));
            add_member(*obj, "field_visualization_enabled",
                bool_value(style.field_visualization_enabled));
            add_member(*obj, "channel_id",
                number_value(style.field_visualization_channel_id));
            add_member(*obj, "value_min",
                number_value(style.field_visualization_value_min));
            add_member(*obj, "value_max",
                number_value(style.field_visualization_value_max));
            add_member(*obj, "gamma",
                number_value(style.field_visualization_gamma));
            add_member(*obj, "palette",
                string_value(mesh_field_visualization_palette_name(
                    style.field_visualization_palette)));
            add_member(*obj, "field_visualization_channel_id",
                number_value(style.field_visualization_channel_id));
            add_member(*obj, "field_visualization_value_min",
                number_value(style.field_visualization_value_min));
            add_member(*obj, "field_visualization_value_max",
                number_value(style.field_visualization_value_max));
            add_member(*obj, "field_visualization_gamma",
                number_value(style.field_visualization_gamma));
            if (!style.field_visualization_field_ref.empty()) {
                add_member(*obj, "field_ref",
                    string_value(style.field_visualization_field_ref));
            }
            return obj;
        }

        JSONValuePtr mesh_mask_render_style_value(
            const SceneMeshMaskRenderStyleAsset& style)
        {
            MeshMaskRenderStyleData mask = style.mask;
            mask.enabled = style.enabled;
            auto obj = mesh_mask_value(mask, style.source_field_ref);
            add_member(*obj, "mesh_input",
                string_value(mesh_mask_render_mesh_input_name(
                    style.mesh_input)));
            add_member(
                *obj,
                "wireframe",
                mesh_render_layer_value(style.wireframe));
            return obj;
        }

        JSONValuePtr mesh_region_set_value(
            const SceneMeshRegionSetAsset& region_set)
        {
            MeshMaskRenderStyleData mask = region_set.mask;
            mask.enabled = region_set.enabled;
            auto obj = mesh_mask_value(mask, region_set.source_field_ref);
            add_member(*obj, "region_set_id",
                string_value(region_set.region_set_id));
            add_member(*obj, "intent",
                string_value(mesh_region_set_intent_name(
                    region_set.intent)));
            add_member(*obj, "mesh_input",
                string_value(mesh_mask_render_mesh_input_name(
                    region_set.mesh_input)));
            return obj;
        }

        JSONValuePtr mesh_wavelet_analysis_value(
            const SceneMeshWaveletAnalysisAsset& analysis)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(analysis.enabled));
            add_member(*obj, "function",
                string_value(mesh_wavelet_analysis_function_name(
                    analysis.function)));
            add_member(*obj, "scale_count",
                number_value(analysis.scale_count));
            add_member(*obj, "lambda_max_estimate",
                number_value(analysis.lambda_max_estimate));
            add_member(*obj, "gamma", number_value(analysis.gamma));
            return obj;
        }

        const char* mesh_compute_input_name(MeshComputeInput input)
        {
            switch (input) {
            case MeshComputeInput::Positions:
                return "positions";
            case MeshComputeInput::Normals:
                return "normals";
            case MeshComputeInput::UV0:
                return "uv0";
            case MeshComputeInput::Indices:
                return "indices";
            case MeshComputeInput::Vertices:
                return "vertices";
            }
            return "positions";
        }

        const char* mesh_derived_field_value_type_name(
            MeshDerivedFieldValueType value_type)
        {
            switch (value_type) {
            case MeshDerivedFieldValueType::Float1:
                return "float1";
            case MeshDerivedFieldValueType::Float2:
                return "float2";
            case MeshDerivedFieldValueType::Float3:
                return "float3";
            case MeshDerivedFieldValueType::Float4:
                return "float4";
            case MeshDerivedFieldValueType::UInt1:
                return "uint1";
            }
            return "float1";
        }

        JSONValuePtr mesh_derived_field_source_value(
            const SceneMeshDerivedFieldSourceAsset& source)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(source.enabled));
            add_member(*obj, "field_id", string_value(source.field_id));
            add_member(*obj, "domain",
                string_value(mesh_derived_field_domain_name(source.domain)));
            add_member(*obj, "channel_id", number_value(source.channel_id));
            add_member(*obj, "value_type",
                string_value(mesh_derived_field_value_type_name(
                    source.value_type)));
            add_member(*obj, "source_kind",
                string_value(mesh_derived_field_source_kind_name(
                    source.source_kind)));
            add_member(*obj, "component",
                string_value(mesh_derived_field_component_name(
                    source.component)));
            add_member(*obj, "normalize", bool_value(source.normalize));
            add_member(*obj, "constant_value",
                number_value(source.constant_value));
            return obj;
        }

        JSONValuePtr mesh_sparse_operator_source_value(
            const SceneMeshSparseOperatorSourceAsset& source)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(source.enabled));
            add_member(*obj, "operator_id",
                string_value(source.operator_id));
            add_member(*obj, "kind",
                string_value(mesh_sparse_operator_kind_name(source.kind)));
            add_member(*obj, "domain",
                string_value(mesh_operator_domain_name(source.domain)));
            add_member(*obj, "value_convention",
                string_value(mesh_sparse_operator_value_convention_name(
                    source.value_convention)));
            return obj;
        }

        JSONValuePtr mesh_sparse_apply_field_value(
            const SceneMeshSparseApplyFieldAsset& field)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(field.enabled));
            add_member(*obj, "operator_ref",
                string_value(field.operator_ref));
            add_member(*obj, "input_field_ref",
                string_value(field.input_field_ref));
            add_member(*obj, "input_channel_id",
                number_value(field.input_channel_id));
            add_member(*obj, "output_channel_id",
                number_value(field.output_channel_id));
            add_member(*obj, "apply_mode",
                string_value(mesh_sparse_apply_mode_name(field.apply_mode)));
            return obj;
        }

        JSONValuePtr mesh_sparse_diffusion_bands_value(
            const SceneMeshSparseDiffusionBandsAsset& bands)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(bands.enabled));
            add_member(*obj, "operator_ref",
                string_value(bands.operator_ref));
            add_member(*obj, "input_field_ref",
                string_value(bands.input_field_ref));
            add_member(*obj, "input_channel_id",
                number_value(bands.input_channel_id));
            add_member(*obj, "output_base_channel_id",
                number_value(bands.output_base_channel_id));
            add_member(*obj, "band_count",
                number_value(bands.band_count));
            add_member(*obj, "iterations_per_band",
                number_value(bands.iterations_per_band));
            add_member(*obj, "mode",
                string_value(mesh_sparse_diffusion_mode_name(bands.mode)));
            add_member(*obj, "tau", number_value(bands.tau));
            return obj;
        }

        JSONValuePtr mesh_level_mask_source_value(
            const SceneMeshLevelMaskSourceAsset& masks)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(masks.enabled));
            add_member(*obj, "input_field_ref",
                string_value(masks.input_field_ref));
            add_member(*obj, "output_field_id",
                string_value(masks.output_field_id));
            add_member(*obj, "domain",
                string_value(mesh_derived_field_domain_name(masks.domain)));

            auto regions = array_value();
            for (const SceneMeshLevelMaskRegionAsset& region :
                 masks.regions)
            {
                auto region_obj = object_value();
                add_member(*region_obj, "input_channel_id",
                    number_value(region.input_channel_id));
                add_member(*region_obj, "output_channel_id",
                    number_value(region.output_channel_id));
                add_member(*region_obj, "min_value",
                    number_value(region.min_value));
                add_member(*region_obj, "max_value",
                    number_value(region.max_value));
                regions->array_values.push_back(std::move(region_obj));
            }
            add_member(*obj, "regions", std::move(regions));
            return obj;
        }

        JSONValuePtr mesh_compute_field_value(
            const SceneMeshComputeFieldAsset& field)
        {
            auto obj = object_value();
            add_member(*obj, "enabled", bool_value(field.enabled));
            add_member(*obj, "kernel_id", string_value(field.kernel_id));
            add_member(*obj, "hlsl_path", string_value(field.hlsl_path));
            add_member(*obj, "entry", string_value(field.entry));
            add_member(*obj, "target", string_value(field.target));

            auto thread_group_size = array_value();
            thread_group_size->array_values.push_back(
                number_value(field.thread_group_size_x));
            thread_group_size->array_values.push_back(
                number_value(field.thread_group_size_y));
            thread_group_size->array_values.push_back(
                number_value(field.thread_group_size_z));
            add_member(
                *obj,
                "thread_group_size",
                std::move(thread_group_size));

            if (!field.inputs.empty()) {
                auto inputs = array_value();
                std::ranges::transform(
                    field.inputs,
                    std::back_inserter(inputs->array_values),
                    [](const MeshComputeInput input)
                    {
                        return string_value(mesh_compute_input_name(input));
                    });
                add_member(*obj, "inputs", std::move(inputs));
            }

            auto channels = array_value();
            for (const auto& channel : field.channels) {
                auto channel_obj = object_value();
                add_member(
                    *channel_obj,
                    "channel_id",
                    number_value(channel.channel_id));
                add_member(
                    *channel_obj,
                    "value_type",
                    string_value(mesh_derived_field_value_type_name(
                        channel.value_type)));
                channels->array_values.push_back(std::move(channel_obj));
            }
            add_member(*obj, "channels", std::move(channels));

            if (!field.params.empty()) {
                auto params = array_value();
                std::ranges::transform(
                    field.params,
                    std::back_inserter(params->array_values),
                    [](const uint32_t param)
                    {
                        return number_value(param);
                    });
                add_member(*obj, "params", std::move(params));
            }
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
            std::ranges::transform(
                source.channels,
                std::back_inserter(channels->array_values),
                [](const auto& channel) {
                    return string_value(channel.name);
                });
            add_member(*obj, "channels", std::move(channels));
            return obj;
        }

        JSONValuePtr compute_kernel_value(
            const SceneComputeKernelAsset& kernel)
        {
            auto obj = object_value();
            add_member(*obj, "kernel_id", string_value(kernel.kernel_id));
            add_member(*obj, "hlsl_path", string_value(kernel.hlsl_path));
            add_member(*obj, "entry", string_value(kernel.entry));
            add_member(*obj, "target", string_value(kernel.target));

            auto thread_group_size = array_value();
            thread_group_size->array_values.push_back(
                number_value(kernel.thread_group_size_x));
            thread_group_size->array_values.push_back(
                number_value(kernel.thread_group_size_y));
            thread_group_size->array_values.push_back(
                number_value(kernel.thread_group_size_z));
            add_member(
                *obj,
                "thread_group_size",
                std::move(thread_group_size));

            if (!kernel.ports.empty()) {
                auto ports = array_value();
                for (const auto& port : kernel.ports) {
                    auto port_obj = object_value();
                    add_member(*port_obj, "name", string_value(port.name));
                    add_member(
                        *port_obj,
                        "kind",
                        string_value(compute_kernel_port_kind_name(port.kind)));
                    add_member(
                        *port_obj,
                        "direction",
                        string_value(compute_kernel_port_direction_name(
                            port.direction)));

                    if (port.kind
                        == SceneComputeKernelPortKind::StructuredBuffer)
                    {
                        add_member(
                            *port_obj,
                            "binding_kind",
                            string_value(compute_kernel_binding_kind_name(
                                port.binding_kind)));
                        add_member(
                            *port_obj,
                            "shader_register",
                            number_value(port.shader_register));
                        add_member(
                            *port_obj,
                            "register_space",
                            number_value(port.register_space));
                        add_member(
                            *port_obj,
                            "stride_bytes",
                            number_value(port.stride_bytes));
                    }
                    else {
                        add_member(
                            *port_obj,
                            "root_constant_offset",
                            number_value(port.root_constant_offset));
                        add_member(
                            *port_obj,
                            "root_constant_dwords",
                            number_value(port.root_constant_dwords));
                    }

                    ports->array_values.push_back(std::move(port_obj));
                }
                add_member(*obj, "ports", std::move(ports));
            }
            return obj;
        }

        JSONValuePtr render_shader_value(
            const SceneRenderShaderAsset& shader)
        {
            auto obj = object_value();
            add_member(*obj, "program_id", string_value(shader.program_id));
            add_member(*obj, "vertex_hlsl_path",
                string_value(shader.vertex_hlsl_path));
            add_member(*obj, "pixel_hlsl_path",
                string_value(shader.pixel_hlsl_path));
            add_member(*obj, "vertex_entry",
                string_value(shader.vertex_entry));
            add_member(*obj, "pixel_entry",
                string_value(shader.pixel_entry));
            add_member(*obj, "vertex_target",
                string_value(shader.vertex_target));
            add_member(*obj, "pixel_target",
                string_value(shader.pixel_target));
            add_member(*obj, "binding_model",
                string_value(shader.binding_model));
            add_member(*obj, "input_layout",
                string_value(shader.input_layout));
            add_member(*obj, "blend", string_value(shader.blend));
            add_member(*obj, "depth", string_value(shader.depth));
            add_member(*obj, "raster", string_value(shader.raster));
            if (!shader.descriptor_bindings.empty()) {
                auto descriptor_bindings = array_value();
                for (const auto& binding : shader.descriptor_bindings) {
                    auto binding_obj = object_value();
                    add_member(
                        *binding_obj,
                        "kind",
                        string_value(binding.kind));
                    add_member(
                        *binding_obj,
                        "visibility",
                        string_value(binding.visibility));
                    add_member(
                        *binding_obj,
                        "semantic",
                        string_value(binding.semantic));
                    add_member(
                        *binding_obj,
                        "shader_register",
                        number_value(binding.shader_register));
                    add_member(
                        *binding_obj,
                        "register_space",
                        number_value(binding.register_space));
                    add_member(
                        *binding_obj,
                        "descriptor_count",
                        number_value(binding.descriptor_count));
                    descriptor_bindings->array_values.push_back(
                        std::move(binding_obj));
                }
                add_member(
                    *obj,
                    "descriptor_bindings",
                    std::move(descriptor_bindings));
            }
            return obj;
        }

        JSONValuePtr terrain_value(const SceneTerrainAsset& terrain)
        {
            auto obj = object_value();
            add_member(*obj, "asset",
                string_value(asset_key_string(terrain.terrain_asset)));
            if (!(terrain.visual_proxy_asset == wz::asset::AssetKey{})) {
                auto visual_proxy = object_value();
                add_member(
                    *visual_proxy,
                    "asset",
                    string_value(asset_key_string(terrain.visual_proxy_asset)));
                add_member(*obj, "visual_proxy", std::move(visual_proxy));
            }
            if (!(terrain.constraint_surface_asset == wz::asset::AssetKey{})) {
                auto constraint_surface = object_value();
                add_member(
                    *constraint_surface,
                    "asset",
                    string_value(
                        asset_key_string(
                            terrain.constraint_surface_asset)));
                add_member(
                    *obj,
                    "constraint_surface",
                    std::move(constraint_surface));
            }
            add_member(
                *obj,
                "calculate_constraint_surface",
                bool_value(terrain.calculate_constraint_surface));
            add_member(*obj, "visible", bool_value(terrain.visible));
            add_member(*obj, "queryable", bool_value(terrain.queryable));
            add_member(*obj, "constrain_movement",
                bool_value(terrain.constrain_movement));
            return obj;
        }

        JSONValuePtr collision_value(const SceneCollisionAsset& collision)
        {
            auto obj = object_value();
            add_member(*obj, "asset",
                string_value(asset_key_string(collision.collision_asset)));
            add_member(*obj, "layer_mask",
                number_value(collision.layer_mask));
            add_member(*obj, "collides_with_mask",
                number_value(collision.collides_with_mask));
            add_member(*obj, "is_trigger",
                bool_value(collision.is_trigger));
            add_member(*obj, "enabled",
                bool_value(collision.enabled));
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
            add_member(*obj, "target_pixels_per_triangle",
                number_value(style.target_pixels_per_triangle));
            add_member(*obj, "enable_surfel_lods",
                bool_value(style.enable_surfel_lods));
            add_member(*obj, "surfel_target_coverage_px",
                number_value(style.surfel_target_coverage_px));
            add_member(*obj, "max_asset_triangle_density",
                number_value(style.max_asset_triangle_density));
            add_member(*obj, "max_screen_triangle_density",
                number_value(style.max_screen_triangle_density));
            add_member(*obj, "visual_chunk_count",
                number_value(style.visual_chunk_count));
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
            std::ranges::transform(
                listener.channels,
                std::back_inserter(channels->array_values),
                [](const auto& channel) {
                    return string_value(channel);
                });
            add_member(*obj, "channels", std::move(channels));
            return obj;
        }

        JSONValuePtr event_trigger_value(
            const SceneEventTriggerAsset& trigger)
        {
            auto obj = object_value();
            add_member(*obj, "event", string_value(trigger.event));
            return obj;
        }

        JSONValuePtr proximity_value(const SceneProximityAsset& proximity)
        {
            auto obj = object_value();
            add_member(*obj, "radius", number_value(proximity.radius));
            add_member(*obj, "layer_mask",
                number_value(proximity.layer_mask));
            add_member(*obj, "detects_with_mask",
                number_value(proximity.detects_with_mask));
            add_member(*obj, "enabled", bool_value(proximity.enabled));
            return obj;
        }

        JSONValuePtr motion_value(const SceneMotionAsset& motion)
        {
            auto obj = object_value();
            add_member(*obj, "linear_velocity",
                float_array(motion.linear_velocity, 3));
            add_member(*obj, "angular_velocity",
                float_array(motion.angular_velocity, 3));
            add_member(
                *obj,
                "space",
                string_value(
                    motion.space == SceneMotionSpace::Local
                        ? "local"
                        : "world"));
            add_member(
                *obj,
                "terrain_constrained",
                bool_value(motion.terrain_constrained));
            add_member(
                *obj,
                "terrain_ride_height",
                number_value(motion.terrain_ride_height));
            add_member(
                *obj,
                "terrain_footprint_radius",
                number_value(motion.terrain_footprint_radius));
            add_member(
                *obj,
                "terrain_align_to_surface",
                bool_value(motion.terrain_align_to_surface));
            add_member(
                *obj,
                "terrain_alignment_strength",
                number_value(motion.terrain_alignment_strength));
            add_member(*obj, "enabled", bool_value(motion.enabled));
            return obj;
        }

        JSONValuePtr behavior_value(const SceneBehaviorAsset& behavior)
        {
            auto obj = object_value();
            if (!behavior.id.empty()) {
                add_member(*obj, "id", string_value(behavior.id));
            }
            if (!behavior.label.empty()) {
                add_member(*obj, "label", string_value(behavior.label));
            }
            if (!behavior.module.empty()) {
                add_member(*obj, "module", string_value(behavior.module));
            }
            add_member(*obj, "name", string_value(behavior.name));
            add_member(*obj, "enabled", bool_value(behavior.enabled));
            add_member(
                *obj,
                "apply_in_editor",
                bool_value(behavior.apply_in_editor));
            if (!behavior.events.empty()) {
                auto events = array_value();
                for (const auto& event : behavior.events) {
                    events->array_values.push_back(string_value(event));
                }
                add_member(*obj, "events", std::move(events));
            }
            if (!behavior.config.empty()) {
                auto config = object_value();
                for (const auto& entry : behavior.config) {
                    switch (entry.kind) {
                    case SceneBehaviorConfigValueKind::Bool:
                        add_member(
                            *config,
                            entry.key,
                            bool_value(entry.bool_value));
                        break;
                    case SceneBehaviorConfigValueKind::Number:
                        add_member(
                            *config,
                            entry.key,
                            number_value(entry.number_value));
                        break;
                    case SceneBehaviorConfigValueKind::String:
                        add_member(
                            *config,
                            entry.key,
                            string_value(entry.string_value));
                        break;
                    }
                }
                add_member(*obj, "config", std::move(config));
            }
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
            if (!node.mesh_source && node.renderable_asset_node_id)
            {
                add_member(*obj, "renderable",
                    renderable_asset_value(*node.renderable_asset_node_id));
            }
            if (node.asset_reference) {
                add_member(*obj, "asset_reference",
                    asset_reference_value(*node.asset_reference));
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
            if (node.sky_visual) {
                add_member(*obj, "sky_visual",
                    sky_visual_value(*node.sky_visual));
            }
            if (node.sky_surface) {
                add_member(*obj, "sky_surface",
                    sky_surface_value(*node.sky_surface));
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
            if (node.scene_import_source) {
                add_member(*obj, "scene_import_source",
                    scene_import_source_value(*node.scene_import_source));
            }
            if (node.imported_node) {
                add_member(*obj, "imported_node",
                    imported_node_value(*node.imported_node));
            }
            if (node.mesh_source) {
                add_member(*obj, "mesh_source",
                    mesh_source_value(*node.mesh_source));
            }
            if (node.mesh_processing) {
                add_member(*obj, "mesh_processing",
                    mesh_processing_value(*node.mesh_processing));
            }
            if (node.mesh_derived_field_source) {
                add_member(*obj, "mesh_derived_field_source",
                    mesh_derived_field_source_value(
                        *node.mesh_derived_field_source));
            }
            if (node.mesh_sparse_operator_source) {
                add_member(*obj, "mesh_sparse_operator_source",
                    mesh_sparse_operator_source_value(
                        *node.mesh_sparse_operator_source));
            }
            if (node.mesh_sparse_apply_field) {
                add_member(*obj, "mesh_sparse_apply_field",
                    mesh_sparse_apply_field_value(
                        *node.mesh_sparse_apply_field));
            }
            if (node.mesh_sparse_diffusion_bands) {
                add_member(*obj, "mesh_sparse_diffusion_bands",
                    mesh_sparse_diffusion_bands_value(
                        *node.mesh_sparse_diffusion_bands));
            }
            if (node.mesh_level_mask_source) {
                add_member(*obj, "mesh_level_mask_source",
                    mesh_level_mask_source_value(
                        *node.mesh_level_mask_source));
            }
            if (node.mesh_wavelet_analysis) {
                add_member(*obj, "mesh_wavelet_analysis",
                    mesh_wavelet_analysis_value(
                        *node.mesh_wavelet_analysis));
            }
            if (node.mesh_compute_field) {
                add_member(*obj, "mesh_compute_field",
                    mesh_compute_field_value(*node.mesh_compute_field));
            }
            if (node.mesh_render_style) {
                add_member(*obj, "mesh_render_style",
                    mesh_render_style_value(*node.mesh_render_style));
            }
            if (node.mesh_mask_render_style) {
                add_member(*obj, "mesh_mask_render_style",
                    mesh_mask_render_style_value(
                        *node.mesh_mask_render_style));
            }
            if (node.mesh_region_set) {
                add_member(*obj, "mesh_region_set",
                    mesh_region_set_value(*node.mesh_region_set));
            }
            if (node.scalar_field_source) {
                add_member(*obj, "scalar_field_source",
                    scalar_field_source_value(*node.scalar_field_source));
            }
            if (node.vector_field_source) {
                add_member(*obj, "vector_field_source",
                    vector_field_source_value(*node.vector_field_source));
            }
            if (node.collision
                && !(node.collision->collision_asset == wz::asset::AssetKey{}))
            {
                add_member(*obj, "collision",
                    collision_value(*node.collision));
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
            if (node.event_trigger) {
                add_member(*obj, "event_trigger",
                    event_trigger_value(*node.event_trigger));
            }
            if (node.proximity) {
                add_member(*obj, "proximity",
                    proximity_value(*node.proximity));
            }
            if (node.motion) {
                add_member(*obj, "motion", motion_value(*node.motion));
            }
            if (node.behavior) {
                add_member(*obj, "behavior",
                    behavior_value(*node.behavior));
            }
            if (!node.behaviors.empty()) {
                auto behaviors = array_value();
                for (const auto& behavior : node.behaviors) {
                    behaviors->array_values.push_back(
                        behavior_value(behavior));
                }
                add_member(*obj, "behaviors", std::move(behaviors));
            }
            if (node.compute_kernel) {
                add_member(*obj, "compute_kernel",
                    compute_kernel_value(*node.compute_kernel));
            }
            if (node.render_shader) {
                add_member(*obj, "render_shader",
                    render_shader_value(*node.render_shader));
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
