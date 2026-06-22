#pragma once

// engine/assets/scene/scene_asset_data.h

#include <asset/draft.h>
#include <asset/types.h>

#include <engine/assets/light/light.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_render_style/mesh_render_style.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <engine/assets/vector_field/vector_field.h>

#include <scene/scene_ecs.h>
#include <scene/transform_node.h>
#include <scene/compile/compiled_scene.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wz::engine::assets
{
    // SceneAssetData is authored scene source data: a small scene description
    // language that compiles into SceneInstance and then into scene-render
    // storage. AssetKey references remain resource-DAG references; entity and
    // component composition lives here in the scene model.
    // Legacy/debug compatibility path for embedded scene-render descriptors.
    // New authored scenes should prefer SceneNodeAsset::renderable_asset so
    // resource identity, dependencies, and compilation stay in the asset system.
    // Do not add new asset-definition features to this embedded binding.
    struct SceneRenderableBinding
    {
        wz::scene::SceneNodeClass node_class{};
        wz::scene::MeshHandle mesh{ wz::scene::INVALID_MESH };
        wz::scene::MaterialHandle material{ wz::scene::INVALID_MATERIAL };
        wz::scene::AABB local_bounds{};
        bool visible = true;
    };

    struct SceneLightAsset
    {
        wz::scene::AuthoredEntityId node_id;
        wz::scene::LightRecord light{};
    };

    struct SceneDirectLightSourceAsset
    {
        wz::asset::AssetKey light_asset{};
        DirectLightKind kind = DirectLightKind::Directional;
        float color[3]{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        float range = 10.0f;
        float inner_cone_radians = 0.4f;
        float outer_cone_radians = 0.8f;
    };

    struct SceneAmbientLightingAsset
    {
        wz::asset::AssetKey lighting_asset{};
        AmbientLightingMode mode = AmbientLightingMode::Constant;
        float color[3]{ 1.0f, 1.0f, 1.0f };
        float intensity = 0.2f;
        wz::asset::AssetKey intensity_field{};
        wz::asset::AssetKey color_field{};
        AmbientLightingDomainMapping domain_mapping =
            AmbientLightingDomainMapping::TerrainUV;
    };

    struct SceneHDRIEnvironmentAsset
    {
        wz::asset::AssetKey environment_asset{};
        std::string path;
        HDRIEnvironmentFormat format = HDRIEnvironmentFormat::Auto;
        float exposure = 1.0f;
        float rotation_x_radians = 0.0f;
        float rotation_y_radians = 0.0f;
        float rotation_z_radians = 0.0f;
        float lighting_intensity = 1.0f;
        float reflection_intensity = 1.0f;
        float background_intensity = 1.0f;
        uint32_t lighting_sample_resolution = 1024;
        float environment_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float environment_light_intensity = 0.0f;
        float dominant_light_direction[3]{ 0.0f, -1.0f, 0.0f };
        float dominant_light_color[3]{ 1.0f, 1.0f, 1.0f };
        float dominant_light_intensity = 0.0f;
        float dominant_light_confidence = 0.0f;
    };

    enum class SceneSkyVisualKind : uint8_t
    {
        None = 0,
        SolidColor,
        DirectionDebug,
        Gradient,
        EquirectangularTexture,
        ScalarField,
        VectorField,
    };

    enum class SceneSkyProjection : uint8_t
    {
        Sphere = 0,
    };

    struct SceneSkyVisualAsset
    {
        SceneSkyVisualKind kind = SceneSkyVisualKind::None;
        float solid_color[3]{ 0.0f, 0.0f, 0.0f };
        float gradient_top_color[3]{ 0.08f, 0.22f, 0.55f };
        float gradient_bottom_color[3]{ 0.62f, 0.78f, 0.95f };

        // Texture assets do not own lighting/radiance semantics here. They are
        // just visual content for the sky canvas; HDRIEnvironment remains the
        // lighting-oriented environment source.
        wz::asset::AssetKey texture_asset{};
        std::string texture_path;
        HDRIEnvironmentFormat texture_format = HDRIEnvironmentFormat::Auto;

        // Scalar fields are accepted as another drawable sky visual source so
        // debug/painted/procedural data can be projected onto the sky without
        // pretending it is a texture asset.
        wz::asset::AssetKey scalar_field_asset{};
        std::string scalar_field_node;

        // Vector fields are visual sky content here, not lighting policy.
        // Later they can feed richer sky effects while the environment system
        // continues to own radiance/lighting interpretation.
        wz::asset::AssetKey vector_field_asset{};
        std::string vector_field_node;

        float exposure = 1.0f;
        float rotation_x_radians = 0.0f;
        float rotation_y_radians = 0.0f;
        float rotation_z_radians = 0.0f;
    };

    struct SceneSkySurfaceAsset
    {
        std::string visual_node;
        SceneSkyProjection projection = SceneSkyProjection::Sphere;
        float radius = 1.0f;
        bool visible_to_camera = true;
    };

    struct SceneSkyDrawAsset
    {
        wz::scene::AuthoredEntityId surface_node;
        wz::scene::AuthoredEntityId visual_node;
        SceneSkyVisualKind visual_kind = SceneSkyVisualKind::None;
        SceneSkyProjection projection = SceneSkyProjection::Sphere;
        float radius = 1.0f;
        bool visible_to_camera = true;
        float solid_color[3]{ 0.0f, 0.0f, 0.0f };
        float gradient_top_color[3]{ 0.08f, 0.22f, 0.55f };
        float gradient_bottom_color[3]{ 0.62f, 0.78f, 0.95f };
        wz::asset::AssetKey texture_asset{};
        std::string texture_path;
        HDRIEnvironmentFormat texture_format = HDRIEnvironmentFormat::Auto;
        wz::asset::AssetKey scalar_field_asset{};
        wz::asset::AssetKey vector_field_asset{};
        float exposure = 0.0f;
        float rotation_x_radians = 0.0f;
        float rotation_y_radians = 0.0f;
        float rotation_z_radians = 0.0f;
    };

    struct SceneCameraAsset
    {
        float fov_y = 1.0472f;   // ~60 degrees
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
        float aspect = 16.0f / 9.0f;
    };

    struct SceneAssetReferenceAsset
    {
        // Direct assignment for the first authorable path. Empty means this
        // component resolves to null.
        wz::asset::AssetKey asset{};

        // Future stable authored asset identity. Preserved, but the editor can
        // already work immediately through the direct key above.
        std::string stable_asset_id;
        wz::asset::AssetType expected_type = wz::asset::AssetType::Unknown;
        std::string label;
    };

    struct AuthoredTransform
    {
        float translation[3]{ 0.f, 0.f, 0.f };
        float rotation_quat[4]{ 0.f, 0.f, 0.f, 1.f };
        float scale[3]{ 1.f, 1.f, 1.f };
    };

    // ─── Non-render component descriptors ─────────────────────────────────
    // Data-only: parsed from scene JSON, instantiated into SceneInstance
    // component tables.  No runtime behavior is implemented here.

    // ─── Debug/editor visual descriptors ─────────────────────────────────

    // Auxiliary visuals are exportable authored visual helpers. The legacy
    // JSON field and compatibility aliases still use "debug_visual" for now.
    enum class SceneAuxiliaryVisualKind : uint8_t
    {
        None = 0,
        Axes,
    };

    struct SceneAuxiliaryVisualAsset
    {
        SceneAuxiliaryVisualKind kind = SceneAuxiliaryVisualKind::None;
        float scale = 1.0f;
        bool visible = true;
    };

    using SceneDebugVisualKind = SceneAuxiliaryVisualKind;
    using SceneDebugVisualAsset = SceneAuxiliaryVisualAsset;

    enum class SceneEditorHandleKind : uint8_t
    {
        None = 0,
        Translate,
        Rotate,
        Scale,
        Transform,
    };

    struct SceneEditorHandleAsset
    {
        SceneEditorHandleKind kind = SceneEditorHandleKind::Transform;
        bool enabled = true;
        bool visible = true;
        float size = 1.0f;
    };

    // ─────────────────────────────────────────────────────────────────────

    struct SceneInputReceiverAsset
    {
        std::string input_map;   // asset URI, e.g. "asset://input_maps/fly"
        bool log_input = false;
    };

    struct SceneFlyingCameraControllerAsset
    {
        float move_speed       = 5.0f;
        float look_speed       = 0.0005f;
        float boost_multiplier = 3.0f;
        float roll_speed       = 1.5f;
    };

    enum class SceneActorMovementSpace : uint8_t
    {
        World = 0,
        Local,
    };

    struct SceneActorMovementControllerAsset
    {
        float move_speed = 5.0f;
        float boost_multiplier = 3.0f;
        SceneActorMovementSpace movement_space =
            SceneActorMovementSpace::World;
    };

    struct SceneGroundBoundaryAsset
    {
        float min[3]{ 0.0f, 0.0f, 0.0f };
        float max[3]{ 0.0f, 0.0f, 0.0f };
        bool constrain_vertical = true;
        bool enabled = true;
    };

    // Editor/import authoring drafts. These records may be stored in scene
    // source JSON so tools can rebuild asset-system nodes, but they are not
    // app/runtime components and do not instantiate into SceneInstance tables.
    // A runtime-ready scene must carry resolved asset references such as
    // renderable_asset and terrain.terrain_asset.
    enum class SceneMeshSourceKind : uint8_t
    {
        Placeholder = 0,
        GLB,
        ProceduralCube,
        ProceduralQuad,
        ProceduralTriangle,
    };

    struct SceneMeshSourceAsset
    {
        SceneMeshSourceKind kind = SceneMeshSourceKind::Placeholder;
        std::string path;
        uint32_t mesh_index = 0;
    };

    enum class SceneMeshProcessingOperation : uint8_t
    {
        Decimate = 0,
        MeshClusterHierarchyPreview,
        DebugTriangleStride,
    };

    struct SceneMeshProcessingAsset
    {
        bool enabled = false;
        SceneMeshProcessingOperation operation =
            SceneMeshProcessingOperation::Decimate;
        std::string region_set_ref;
        uint32_t target_vertex_count = 0;
        uint32_t target_triangle_count = 0;
        float target_ratio = 1.0f;
        bool preserve_boundary = true;
        float aspect_ratio = 0.0f;
        float edge_length = 0.0f;
        uint32_t max_valence = 0;
        float normal_deviation = 0.0f;
        float hausdorff_error = 0.0f;
        uint32_t preview_level_index = 0;
        wz::asset::AssetKey source_mesh_asset{};
        wz::asset::AssetKey processed_mesh_asset{};
        wz::asset::AssetKey hierarchy_asset{};
    };

    enum class SceneImportSourceKind : uint8_t
    {
        GLB = 0,
    };

    struct SceneImportSourceAsset
    {
        SceneImportSourceKind kind = SceneImportSourceKind::GLB;
        std::string path;
        std::string import_prefix;
        std::optional<uint32_t> scene_index;
    };

    struct SceneImportedNodeAsset
    {
        wz::scene::AuthoredEntityId anchor_node;
        std::string import_prefix;
        std::string source_node_id;
        bool missing_source = false;
    };

    struct SceneMeshRenderLayerAsset
    {
        bool enabled = false;
        float color[4]{ 0.0f, 1.0f, 0.15f, 1.0f };
        float emissive_strength = 1.0f;
    };

    struct SceneMeshRenderStyleAsset
    {
        wz::asset::AssetKey style_asset{};
        SceneMeshRenderLayerAsset wireframe{
            true,
            { 0.0f, 1.0f, 0.15f, 1.0f },
            1.0f,
        };
        SceneMeshRenderLayerAsset surface{
            false,
            { 0.25f, 0.9f, 0.35f, 1.0f },
            0.0f,
        };
        float alpha = 1.0f;
        bool depth_test = false;
        bool depth_write = false;
        bool double_sided = true;
        bool hidden_line_prepass = true;
        bool field_visualization_enabled = false;
        uint32_t field_visualization_channel_id =
            MeshWaveletChannelID::kDetailCost;
        float field_visualization_value_min = 0.0f;
        float field_visualization_value_max = 1.0f;
        float field_visualization_gamma = 1.0f;
        MeshFieldVisualizationPalette field_visualization_palette =
            MeshFieldVisualizationPalette::Heat;
        std::string field_visualization_field_ref;
        wz::asset::AssetKey field_visualization_asset{};
        MeshMaskRenderStyleData mask{};
        std::string mask_source_field_ref;
        wz::asset::AssetKey mask_source_field_asset{};
    };

    enum class SceneMeshMaskRenderMeshInput : uint8_t
    {
        Source = 0,
        Processed,
    };

    struct SceneMeshMaskRenderStyleAsset
    {
        bool enabled = true;
        SceneMeshMaskRenderMeshInput mesh_input =
            SceneMeshMaskRenderMeshInput::Source;
        std::string source_field_ref;
        SceneMeshRenderLayerAsset wireframe{
            true,
            { 1.0f, 1.0f, 1.0f, 0.5f },
            1.0f,
        };
        MeshMaskRenderStyleData mask{ true };
        wz::asset::AssetKey source_field_asset{};
    };

    enum class SceneMeshRegionSetIntent : uint8_t
    {
        MaterialMask = 0,
        RemeshSubset,
    };

    struct SceneMeshRegionSetAsset
    {
        bool enabled = true;
        std::string region_set_id = "regions";
        SceneMeshRegionSetIntent intent =
            SceneMeshRegionSetIntent::RemeshSubset;
        SceneMeshMaskRenderMeshInput mesh_input =
            SceneMeshMaskRenderMeshInput::Source;
        std::string source_field_ref;
        MeshMaskRenderStyleData mask{ true };
        wz::asset::AssetKey source_field_asset{};
    };

    enum class SceneMeshDerivedFieldSourceKind : uint8_t
    {
        Constant = 0,
        PositionGradient,
        VertexIndexGradient,
        TriangleCornerCount,
        VertexArea,
        TriangleArea,
        MeanEdgeLength,
        InverseAreaDensity,
        LogDensity,
    };

    enum class SceneMeshDerivedFieldComponent : uint8_t
    {
        X = 0,
        Y,
        Z,
    };

    struct SceneMeshDerivedFieldSourceAsset
    {
        bool enabled = true;
        std::string field_id = "field";
        MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Vertex;
        uint32_t channel_id = 0x2000u;
        MeshDerivedFieldValueType value_type =
            MeshDerivedFieldValueType::Float1;
        SceneMeshDerivedFieldSourceKind source_kind =
            SceneMeshDerivedFieldSourceKind::PositionGradient;
        SceneMeshDerivedFieldComponent component =
            SceneMeshDerivedFieldComponent::Y;
        bool normalize = true;
        float constant_value = 0.5f;

        // Materialization output / editor cache, not saved as authored intent.
        wz::asset::AssetKey resolved_field_asset{};
    };

    struct SceneMeshSparseOperatorSourceAsset
    {
        bool enabled = true;
        std::string operator_id = "uniform_laplacian";
        MeshSparseOperatorKind kind =
            MeshSparseOperatorKind::UniformVertexLaplacian;
        MeshOperatorDomain domain = MeshOperatorDomain::Vertex;
        MeshSparseOperatorValueConvention value_convention =
            MeshSparseOperatorValueConvention::NeighborWeights;

        // Materialization output / editor cache, not saved as authored intent.
        wz::asset::AssetKey resolved_operator_asset{};
    };

    enum class SceneMeshSparseApplyMode : uint8_t
    {
        Residual = 0,
    };

    struct SceneMeshSparseApplyFieldAsset
    {
        bool enabled = true;
        std::string operator_ref = "operator:uniform_laplacian";
        std::string input_field_ref = "field:height";
        uint32_t input_channel_id = 0x2000u;
        uint32_t output_channel_id = 0x2100u;
        SceneMeshSparseApplyMode apply_mode =
            SceneMeshSparseApplyMode::Residual;

        // Materialization output / editor cache, not saved as authored intent.
        wz::asset::AssetKey output_field_asset{};
    };

    enum class SceneMeshSparseDiffusionMode : uint8_t
    {
        Smooth = 0,
        DiffusionStep,
    };

    struct SceneMeshSparseDiffusionBandsAsset
    {
        bool enabled = true;
        std::string operator_ref = "operator:uniform_laplacian";
        std::string input_field_ref = "field:height";
        uint32_t input_channel_id = 0x2000u;
        uint32_t output_base_channel_id = 0x2200u;
        uint32_t band_count = 3;
        uint32_t iterations_per_band = 1;
        SceneMeshSparseDiffusionMode mode =
            SceneMeshSparseDiffusionMode::Smooth;
        float tau = 1.0f;

        // Materialization output / editor cache, not saved as authored intent.
        wz::asset::AssetKey output_field_asset{};
    };

    struct SceneMeshLevelMaskRegionAsset
    {
        uint32_t input_channel_id = 0x2200u;
        uint32_t output_channel_id = 0x3000u;
        float min_value = 0.0f;
        float max_value = 1.0f;
    };

    struct SceneMeshLevelMaskSourceAsset
    {
        bool enabled = true;
        std::string input_field_ref = "field:diffusion_bands";
        std::string output_field_id = "masks";
        MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Face;
        std::vector<SceneMeshLevelMaskRegionAsset> regions{
            SceneMeshLevelMaskRegionAsset{},
        };

        // Materialization output / editor cache, not saved as authored intent.
        wz::asset::AssetKey output_field_asset{};
    };

    enum class SceneMeshWaveletAnalysisFunction : uint8_t
    {
        BuiltinDetailHeatV0 = 0,
    };

    struct SceneMeshWaveletAnalysisAsset
    {
        bool enabled = true;
        SceneMeshWaveletAnalysisFunction function =
            SceneMeshWaveletAnalysisFunction::BuiltinDetailHeatV0;
        uint32_t scale_count = 3;
        float lambda_max_estimate = 2.0f;
        float gamma = 1.0f;
        wz::asset::AssetKey field_asset{};
    };

    struct SceneMeshComputeFieldChannelAsset
    {
        uint32_t channel_id = 0;
        MeshDerivedFieldValueType value_type =
            MeshDerivedFieldValueType::Float1;
    };

    // Editor/import recipe compiling a project-authored compute kernel into
    // a cached MeshDerivedField asset on the node's mesh. The kernel
    // reference reuses the compute_kernel authoring shape
    // (hlsl_path/entry/target/thread_group_size); declared inputs are
    // engine-extracted mesh data bound as SRVs in declared order, and params
    // are authored root-constant dwords appended after the engine-filled
    // vertex_count/index_count/triangle_count constants.
    struct SceneMeshComputeFieldAsset
    {
        bool enabled = true;
        std::string kernel_id;
        std::string hlsl_path;
        std::string entry = "main";
        std::string target = "cs_5_0";
        uint32_t thread_group_size_x = 1;
        uint32_t thread_group_size_y = 1;
        uint32_t thread_group_size_z = 1;
        std::vector<MeshComputeInput> inputs;
        std::vector<SceneMeshComputeFieldChannelAsset> channels;
        std::vector<uint32_t> params;

        // Materialized asset key derived from the authored recipe.
        // Not an authored JSON field.
        wz::asset::AssetKey field_asset{};
    };

    enum class SceneTerrainRenderPath : uint8_t
    {
        Auto = 0,
        Surface,
        DebugWireframe,
        None,
    };

    enum class SceneTerrainLightingSource : uint8_t
    {
        ExplicitNodes = 0,
        SceneDefault,
        EnvironmentNode,
        Hybrid,
    };

    struct SceneTerrainRenderStyleAsset
    {
        SceneTerrainRenderPath path = SceneTerrainRenderPath::Auto;
        bool depth_test = true;
        bool depth_write = true;

        // Terrain declares how it consumes resolved lighting; it should not
        // become the environment system. Materialization can already resolve
        // EnvironmentNode into terrain shader constants, and a future
        // SceneLightingContext can add shared irradiance/prefiltered-map GPU
        // resources without routing HDRI through scene light nodes.
        SceneTerrainLightingSource lighting_source =
            SceneTerrainLightingSource::ExplicitNodes;
        std::string directional_light_node;
        std::string ambient_light_node;
        std::string environment_node;

        float ambient_strength = 1.0f;
        float sky_visibility_strength = 1.0f;
        float normal_lighting_strength = 1.0f;
        float terrain_bounce_strength = 0.0f;
        float target_pixels_per_triangle = 2.0f;
        bool enable_surfel_lods = false;
        float surfel_target_coverage_px = 1.0f;
        float max_asset_triangle_density = 0.0f;
        float max_screen_triangle_density = 0.0f;
        uint32_t visual_chunk_count = 4096;
    };

    enum class SceneScalarFieldSourceKind : uint8_t
    {
        RawF32 = 0,
        ProceduralGradientX,
        ProceduralGradientY,
        ProceduralRadialGradient,
        ProceduralCheckerboard,
        ProceduralSineWaves,
    };

    // Editor/import recipe for building a scalar-field asset node.
    // The editor materialization pass should resolve this to scalar_field_asset
    // before other editor-authored asset recipes consume it.
    struct SceneScalarFieldSourceAsset
    {
        SceneScalarFieldSourceKind kind =
            SceneScalarFieldSourceKind::ProceduralGradientX;
        wz::asset::AssetKey scalar_field_asset{};
        std::string path;
        uint32_t width = 64;
        uint32_t height = 64;
        uint32_t depth = 1;
        float frequency = 1.0f;
        float amplitude = 1.0f;
    };

    enum class SceneVectorFieldSourceKind : uint8_t
    {
        RawF32 = 0,
    };

    // Editor/import recipe for building a vector-field asset node.
    // The editor materialization pass should resolve this to vector_field_asset
    // before other editor-authored asset recipes consume it.
    struct SceneVectorFieldSourceAsset
    {
        SceneVectorFieldSourceKind kind = SceneVectorFieldSourceKind::RawF32;
        wz::asset::AssetKey vector_field_asset{};
        std::string path;
        uint32_t width = 64;
        uint32_t height = 64;
        uint32_t depth = 1;
        uint32_t components_per_channel = 3;
        std::vector<VectorFieldChannelDesc> channels{
            VectorFieldChannelDesc{ .name = "normal" },
        };
    };

    struct SceneTerrainAsset
    {
        wz::asset::AssetKey terrain_asset{};
        wz::asset::AssetKey visual_proxy_asset{};
        wz::asset::AssetKey constraint_surface_asset{};
        bool calculate_constraint_surface = false;
        bool visible = true;
        bool queryable = true;
        bool constrain_movement = true;
    };

    struct SceneCollisionAsset
    {
        wz::asset::AssetKey collision_asset{};
        uint32_t layer_mask = 1;
        uint32_t collides_with_mask = 0xffffffffu;
        bool is_trigger = false;
        bool enabled = true;
    };

    enum class SceneTerrainMeshHeightPolicy : uint8_t
    {
        HighestAcceptedSurface = 0,
    };

    enum class SceneTerrainMeshSourceMode : uint8_t
    {
        MeshAsset = 0,
        SceneNode,
    };

    // Editor/import recipe for deriving a TerrainAsset from a mesh source.
    // The editor materialization pass should resolve this to terrain.terrain_asset
    // before preview/runtime instantiation.
    struct SceneTerrainMeshSourceAsset
    {
        SceneTerrainMeshSourceMode mode = SceneTerrainMeshSourceMode::MeshAsset;
        wz::scene::AuthoredEntityId source_node;
        wz::asset::AssetKey mesh_asset{};
        SceneTerrainMeshHeightPolicy height_policy =
            SceneTerrainMeshHeightPolicy::HighestAcceptedSurface;
        float min_surface_normal_y = 0.2f;
        bool include_backfaces = false;
    };

    enum class SceneTerrainHeightFieldSourceMode : uint8_t
    {
        ScalarFieldAsset = 0,
        SceneNode,
    };

    // Editor/import recipe for deriving a TerrainAsset from a scalar field.
    // The editor materialization pass should resolve this to terrain.terrain_asset
    // before preview/runtime instantiation.
    struct SceneTerrainHeightFieldSourceAsset
    {
        SceneTerrainHeightFieldSourceMode mode =
            SceneTerrainHeightFieldSourceMode::ScalarFieldAsset;
        wz::scene::AuthoredEntityId source_node;
        wz::asset::AssetKey scalar_field_asset{};
        float origin[2]{ 0.0f, 0.0f };
        float size[2]{ 1.0f, 1.0f };
        float vertical_scale = 1.0f;
        float base_height = 0.0f;
    };

    struct SceneAudioListenerAsset
    {
        bool active = true;
    };

    struct SceneEventListenerAsset
    {
        std::vector<std::string> channels;
    };

    struct SceneEventTriggerAsset
    {
        std::string event = "gpu.compute.request";
    };

    struct SceneProximityAsset
    {
        float radius = 1.0f;
        uint32_t layer_mask = 1;
        uint32_t detects_with_mask = 0xffffffffu;
        bool enabled = true;
    };

    enum class SceneMotionSpace : uint8_t
    {
        World = 0,
        Local,
    };

    struct SceneMotionAsset
    {
        float linear_velocity[3]{ 0.0f, 0.0f, 0.0f };
        float angular_velocity[3]{ 0.0f, 0.0f, 0.0f };
        SceneMotionSpace space = SceneMotionSpace::World;
        bool terrain_constrained = false;
        float terrain_ride_height = 0.0f;
        float terrain_footprint_radius = 0.0f;
        bool terrain_align_to_surface = false;
        float terrain_alignment_strength = 1.0f;
        bool enabled = true;
    };

    enum class SceneBehaviorConfigValueKind : uint8_t
    {
        Bool = 0,
        Number,
        String,
    };

    struct SceneBehaviorConfigValue
    {
        std::string key;
        SceneBehaviorConfigValueKind kind =
            SceneBehaviorConfigValueKind::String;
        bool bool_value = false;
        double number_value = 0.0;
        std::string string_value;
    };

    struct SceneBehaviorAsset
    {
        std::string id;
        std::string label;
        std::string module;
        std::string name;
        bool enabled = true;
        bool apply_in_editor = false;
        std::vector<std::string> events;
        std::vector<SceneBehaviorConfigValue> config;
    };

    enum class SceneComputeKernelPortKind : uint8_t
    {
        StructuredBuffer = 0,
        U32,
        F32,
    };

    enum class SceneComputeKernelPortDirection : uint8_t
    {
        Input = 0,
        Output,
    };

    enum class SceneComputeKernelBindingKind : uint8_t
    {
        SRV = 0,
        UAV,
    };

    struct SceneComputeKernelPortAsset
    {
        std::string name;
        SceneComputeKernelPortKind kind =
            SceneComputeKernelPortKind::StructuredBuffer;
        SceneComputeKernelPortDirection direction =
            SceneComputeKernelPortDirection::Input;

        // Buffer ports use binding_kind/register/stride fields. Root constant
        // ports use root_constant_offset/root_constant_dwords.
        SceneComputeKernelBindingKind binding_kind =
            SceneComputeKernelBindingKind::SRV;
        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        uint32_t stride_bytes = 0;
        uint32_t root_constant_offset = 0;
        uint32_t root_constant_dwords = 0;
    };

    struct SceneComputeKernelAsset
    {
        std::string kernel_id;
        std::string hlsl_path;
        std::string entry = "main";
        std::string target = "cs_5_0";
        uint32_t thread_group_size_x = 1;
        uint32_t thread_group_size_y = 1;
        uint32_t thread_group_size_z = 1;
        std::vector<SceneComputeKernelPortAsset> ports;

        // Materialized asset keys derived from the authored kernel contract.
        // These are not authored JSON fields.
        wz::asset::AssetKey compute_shader_asset{};
        wz::asset::AssetKey compute_pipeline_asset{};
    };

    struct SceneDescriptorBindingAsset
    {
        std::string kind;
        std::string visibility;
        std::string semantic;
        uint32_t shader_register = 0;
        uint32_t register_space = 0;
        uint32_t descriptor_count = 1;
    };

    struct SceneRenderShaderAsset
    {
        std::string program_id;
        std::string vertex_hlsl_path;
        std::string pixel_hlsl_path;
        std::string vertex_entry = "main";
        std::string pixel_entry = "main";
        std::string vertex_target = "vs_5_0";
        std::string pixel_target = "ps_5_0";
        std::string binding_model = "mesh_ia";
        std::string input_layout = "mesh_position_normal_uv";
        std::string blend = "opaque";
        std::string depth = "test_write";
        std::string raster = "solid_cull_none";
        std::vector<SceneDescriptorBindingAsset> descriptor_bindings;
        wz::asset::AssetKey render_program_asset{};
    };

    // ─────────────────────────────────────────────────────────────────────

    struct SceneNodeAsset
    {
        wz::scene::AuthoredEntityId id;
        std::optional<wz::scene::AuthoredEntityId> parent_id;
        std::string name;

        AuthoredTransform local{};

        bool visible = true;

        wz::scene::TransformNode::MotionType motion_type =
            wz::scene::TransformNode::MotionType::Static;

        // Legacy embedded renderable authoring data, exported as
        // debug_renderable. Compatibility/debug path only; prefer
        // renderable_asset for new authored scenes.
        std::optional<SceneRenderableBinding> renderable;

        // Preferred authored Renderable component. The node id points at the
        // authored asset graph; renderable_asset is the current resolved key.
        std::optional<wz::asset::AssetGraphDraftNodeId>
            renderable_asset_node_id;
        std::optional<wz::asset::AssetKey> renderable_asset;
        std::optional<SceneAssetReferenceAsset> asset_reference;
        std::optional<SceneCameraAsset> camera;
        std::optional<SceneDirectLightSourceAsset> direct_light_source;
        std::optional<SceneAmbientLightingAsset> ambient_lighting;
        std::optional<SceneHDRIEnvironmentAsset> hdri_environment;
        std::optional<SceneSkyVisualAsset> sky_visual;
        std::optional<SceneSkySurfaceAsset> sky_surface;

        std::optional<SceneInputReceiverAsset> input_receiver;
        std::optional<SceneFlyingCameraControllerAsset> flying_camera_controller;
        std::optional<SceneActorMovementControllerAsset> actor_movement_controller;
        std::optional<SceneGroundBoundaryAsset> ground_boundary;
        std::optional<SceneImportSourceAsset> scene_import_source;
        std::optional<SceneImportedNodeAsset> imported_node;
        std::optional<SceneMeshSourceAsset> mesh_source;
        std::optional<SceneMeshProcessingAsset> mesh_processing;
        std::optional<SceneMeshDerivedFieldSourceAsset>
            mesh_derived_field_source;
        std::optional<SceneMeshSparseOperatorSourceAsset>
            mesh_sparse_operator_source;
        std::optional<SceneMeshSparseApplyFieldAsset>
            mesh_sparse_apply_field;
        std::optional<SceneMeshSparseDiffusionBandsAsset>
            mesh_sparse_diffusion_bands;
        std::optional<SceneMeshLevelMaskSourceAsset>
            mesh_level_mask_source;
        std::optional<SceneMeshWaveletAnalysisAsset> mesh_wavelet_analysis;
        std::optional<SceneMeshComputeFieldAsset> mesh_compute_field;
        std::optional<SceneMeshRenderStyleAsset> mesh_render_style;
        std::optional<SceneMeshMaskRenderStyleAsset>
            mesh_mask_render_style;
        std::optional<SceneMeshRegionSetAsset> mesh_region_set;
        std::optional<SceneScalarFieldSourceAsset> scalar_field_source;
        std::optional<SceneVectorFieldSourceAsset> vector_field_source;
        std::optional<SceneCollisionAsset> collision;
        std::optional<SceneTerrainAsset> terrain;
        std::optional<SceneTerrainRenderStyleAsset> terrain_render_style;
        std::optional<SceneTerrainMeshSourceAsset> terrain_mesh_source;
        std::optional<SceneTerrainHeightFieldSourceAsset>
            terrain_height_field_source;
        std::optional<SceneAudioListenerAsset> audio_listener;
        std::optional<SceneEventListenerAsset> event_listener;
        std::optional<SceneEventTriggerAsset> event_trigger;
        std::optional<SceneProximityAsset> proximity;
        std::optional<SceneMotionAsset> motion;
        std::optional<SceneBehaviorAsset> behavior;
        std::vector<SceneBehaviorAsset> behaviors;
        std::optional<SceneComputeKernelAsset> compute_kernel;
        std::optional<SceneRenderShaderAsset> render_shader;

        std::optional<SceneAuxiliaryVisualAsset> debug_visual;
        std::optional<SceneEditorHandleAsset> editor_handle;
    };

    struct SceneDefaults
    {
        std::optional<wz::scene::AuthoredEntityId> active_camera_node;
    };

    struct SceneAssetData
    {
        std::string name;
        std::vector<SceneNodeAsset> nodes;
        std::vector<SceneLightAsset> lights;
        std::vector<SceneSkyDrawAsset> sky_draws;
        SceneDefaults defaults{};

        bool valid() const noexcept { return !nodes.empty(); }
    };

    struct SceneAssetReferenceBinding
    {
        std::string uri;
        wz::asset::AssetKey key{};
    };

    struct SceneFromJSONCompileDesc
    {
        std::vector<SceneAssetReferenceBinding> renderable_asset_references;
        std::vector<SceneAssetReferenceBinding> collision_asset_references;
        std::vector<SceneAssetReferenceBinding> terrain_asset_references;
        std::vector<SceneAssetReferenceBinding> mesh_asset_references;
        std::vector<SceneAssetReferenceBinding> scalar_field_asset_references;
        std::vector<SceneAssetReferenceBinding> vector_field_asset_references;
    };

    struct SceneGLBMeshRenderableBinding
    {
        uint32_t mesh_index = 0;
        wz::asset::AssetKey mesh_asset{};
        wz::asset::AssetKey renderable_asset{};
    };

    struct SceneFromGLBCompileDesc
    {
        uint32_t scene_index = 0;
        std::vector<SceneGLBMeshRenderableBinding> mesh_renderables;
    };

    struct SceneAssetAuthoringRecipeSummary
    {
        uint32_t nodes_with_recipes = 0;
        uint32_t total_recipes = 0;
        uint32_t scene_import_sources = 0;
        uint32_t mesh_sources = 0;
        uint32_t mesh_processing = 0;
        uint32_t mesh_derived_field_sources = 0;
        uint32_t mesh_sparse_operator_sources = 0;
        uint32_t mesh_sparse_apply_fields = 0;
        uint32_t mesh_sparse_diffusion_bands = 0;
        uint32_t mesh_level_mask_sources = 0;
        uint32_t mesh_wavelet_analyses = 0;
        uint32_t mesh_compute_fields = 0;
        uint32_t mesh_render_styles = 0;
        uint32_t mesh_mask_render_styles = 0;
        uint32_t mesh_region_sets = 0;
        uint32_t scalar_field_sources = 0;
        uint32_t vector_field_sources = 0;
        uint32_t direct_light_sources = 0;
        uint32_t ambient_lighting = 0;
        uint32_t hdri_environments = 0;
        uint32_t sky_visuals = 0;
        uint32_t sky_surfaces = 0;
        uint32_t terrain_render_styles = 0;
        uint32_t terrain_mesh_sources = 0;
        uint32_t terrain_height_field_sources = 0;
        uint32_t event_triggers = 0;
        uint32_t compute_kernels = 0;
        uint32_t render_shaders = 0;
    };

    inline SceneNodeAsset make_scene_node(
        wz::scene::AuthoredEntityId id,
        std::string name = {})
    {
        SceneNodeAsset node{};
        node.id = std::move(id);
        node.name = name.empty() ? node.id : std::move(name);
        return node;
    }

    inline SceneNodeAsset& add_scene_node(
        SceneAssetData& scene,
        SceneNodeAsset node)
    {
        scene.nodes.push_back(std::move(node));
        return scene.nodes.back();
    }

    inline void set_parent(
        SceneNodeAsset& node,
        wz::scene::AuthoredEntityId parent_id)
    {
        node.parent_id = std::move(parent_id);
    }

    inline void set_transform(
        SceneNodeAsset& node,
        const AuthoredTransform& transform)
    {
        node.local = transform;
    }

    inline void normalize_scene_direction(float direction[3]) noexcept
    {
        const float len =
            std::sqrt(
                direction[0] * direction[0]
                + direction[1] * direction[1]
                + direction[2] * direction[2]);
        if (len > 1e-6f) {
            direction[0] /= len;
            direction[1] /= len;
            direction[2] /= len;
        }
    }

    inline void authored_light_direction_from_transform(
        const AuthoredTransform& transform,
        float out[3]) noexcept
    {
        const float qx = transform.rotation_quat[0];
        const float qy = transform.rotation_quat[1];
        const float qz = transform.rotation_quat[2];
        const float qw = transform.rotation_quat[3];

        out[0] = 2.0f * (qx * qy + qw * qz);
        out[1] = -1.0f + 2.0f * (qx * qx + qz * qz);
        out[2] = 2.0f * (qy * qz - qw * qx);
        normalize_scene_direction(out);

        if (out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f) {
            out[1] = -1.0f;
        }
    }

    inline void authored_light_direction_from_node(
        const SceneNodeAsset& node,
        float out[3]) noexcept
    {
        authored_light_direction_from_transform(node.local, out);
    }

    inline void set_node_rotation_from_authored_light_direction(
        SceneNodeAsset& node,
        const float direction[3]) noexcept
    {
        constexpr float from[3]{ 0.0f, -1.0f, 0.0f };
        float to[3]{ direction[0], direction[1], direction[2] };
        normalize_scene_direction(to);

        const float dot =
            std::clamp(
                from[0] * to[0] + from[1] * to[1] + from[2] * to[2],
                -1.0f,
                1.0f);

        if (dot > 0.9999f) {
            node.local.rotation_quat[0] = 0.0f;
            node.local.rotation_quat[1] = 0.0f;
            node.local.rotation_quat[2] = 0.0f;
            node.local.rotation_quat[3] = 1.0f;
            return;
        }

        if (dot < -0.9999f) {
            node.local.rotation_quat[0] = 1.0f;
            node.local.rotation_quat[1] = 0.0f;
            node.local.rotation_quat[2] = 0.0f;
            node.local.rotation_quat[3] = 0.0f;
            return;
        }

        float axis[3]{
            from[1] * to[2] - from[2] * to[1],
            from[2] * to[0] - from[0] * to[2],
            from[0] * to[1] - from[1] * to[0],
        };
        normalize_scene_direction(axis);

        const float angle = std::acos(dot);
        const float s = std::sin(angle * 0.5f);
        node.local.rotation_quat[0] = axis[0] * s;
        node.local.rotation_quat[1] = axis[1] * s;
        node.local.rotation_quat[2] = axis[2] * s;
        node.local.rotation_quat[3] = std::cos(angle * 0.5f);
    }

    inline void attach_renderable_asset(
        SceneNodeAsset& node,
        wz::asset::AssetKey renderable_asset)
    {
        node.renderable_asset = renderable_asset;
    }

    inline void attach_renderable_asset_node(
        SceneNodeAsset& node,
        wz::asset::AssetGraphDraftNodeId node_id,
        wz::asset::AssetKey renderable_asset = {})
    {
        node.renderable_asset_node_id = node_id;
        node.renderable_asset = renderable_asset;
    }

    inline void detach_renderable_asset_node(SceneNodeAsset& node)
    {
        node.renderable_asset_node_id.reset();
        node.renderable_asset.reset();
    }

    inline void attach_asset_reference(
        SceneNodeAsset& node,
        SceneAssetReferenceAsset reference = {})
    {
        node.asset_reference = std::move(reference);
    }

    inline void attach_camera(
        SceneNodeAsset& node,
        SceneCameraAsset camera = {})
    {
        node.camera = camera;
    }

    inline void attach_direct_light_source(
        SceneNodeAsset& node,
        SceneDirectLightSourceAsset source = {})
    {
        node.direct_light_source = source;
    }

    inline void attach_ambient_lighting(
        SceneNodeAsset& node,
        SceneAmbientLightingAsset lighting = {})
    {
        node.ambient_lighting = lighting;
    }

    inline void attach_hdri_environment(
        SceneNodeAsset& node,
        SceneHDRIEnvironmentAsset environment = {})
    {
        node.hdri_environment = std::move(environment);
    }

    inline void attach_sky_visual(
        SceneNodeAsset& node,
        SceneSkyVisualAsset visual = {})
    {
        node.sky_visual = std::move(visual);
    }

    inline void attach_sky_surface(
        SceneNodeAsset& node,
        SceneSkySurfaceAsset surface = {})
    {
        node.sky_surface = std::move(surface);
    }

    inline void attach_auxiliary_visual(
        SceneNodeAsset& node,
        SceneAuxiliaryVisualAsset visual)
    {
        node.debug_visual = visual;
    }

    inline void attach_editor_handle(
        SceneNodeAsset& node,
        SceneEditorHandleAsset handle = {})
    {
        node.editor_handle = handle;
    }

    inline void attach_actor_movement_controller(
        SceneNodeAsset& node,
        SceneActorMovementControllerAsset controller = {})
    {
        node.actor_movement_controller = controller;
    }

    inline void attach_ground_boundary(
        SceneNodeAsset& node,
        SceneGroundBoundaryAsset boundary)
    {
        node.ground_boundary = boundary;
    }

    inline void attach_scene_import_source(
        SceneNodeAsset& node,
        SceneImportSourceAsset source = {})
    {
        node.scene_import_source = std::move(source);
    }

    inline void attach_mesh_source(
        SceneNodeAsset& node,
        SceneMeshSourceAsset source = {})
    {
        node.mesh_source = std::move(source);
    }

    inline void attach_mesh_render_style(
        SceneNodeAsset& node,
        SceneMeshRenderStyleAsset style = {})
    {
        node.mesh_render_style = style;
    }

    inline void attach_mesh_mask_render_style(
        SceneNodeAsset& node,
        SceneMeshMaskRenderStyleAsset style = {})
    {
        node.mesh_mask_render_style = std::move(style);
    }

    inline void attach_mesh_region_set(
        SceneNodeAsset& node,
        SceneMeshRegionSetAsset region_set = {})
    {
        node.mesh_region_set = std::move(region_set);
    }

    inline void attach_mesh_derived_field_source(
        SceneNodeAsset& node,
        SceneMeshDerivedFieldSourceAsset source = {})
    {
        node.mesh_derived_field_source = std::move(source);
    }

    inline void attach_mesh_sparse_operator_source(
        SceneNodeAsset& node,
        SceneMeshSparseOperatorSourceAsset source = {})
    {
        node.mesh_sparse_operator_source = std::move(source);
    }

    inline void attach_mesh_sparse_apply_field(
        SceneNodeAsset& node,
        SceneMeshSparseApplyFieldAsset field = {})
    {
        node.mesh_sparse_apply_field = std::move(field);
    }

    inline void attach_mesh_sparse_diffusion_bands(
        SceneNodeAsset& node,
        SceneMeshSparseDiffusionBandsAsset bands = {})
    {
        node.mesh_sparse_diffusion_bands = std::move(bands);
    }

    inline void attach_mesh_level_mask_source(
        SceneNodeAsset& node,
        SceneMeshLevelMaskSourceAsset source = {})
    {
        node.mesh_level_mask_source = std::move(source);
    }

    inline void attach_mesh_wavelet_analysis(
        SceneNodeAsset& node,
        SceneMeshWaveletAnalysisAsset analysis = {})
    {
        node.mesh_wavelet_analysis = analysis;
    }

    inline void attach_mesh_compute_field(
        SceneNodeAsset& node,
        SceneMeshComputeFieldAsset field = {})
    {
        node.mesh_compute_field = std::move(field);
    }

    inline void attach_scalar_field_source(
        SceneNodeAsset& node,
        SceneScalarFieldSourceAsset source = {})
    {
        node.scalar_field_source = std::move(source);
    }

    inline void attach_vector_field_source(
        SceneNodeAsset& node,
        SceneVectorFieldSourceAsset source = {})
    {
        node.vector_field_source = std::move(source);
    }

    inline void attach_terrain(
        SceneNodeAsset& node,
        SceneTerrainAsset terrain = {})
    {
        node.terrain = terrain;
    }

    inline void attach_collision(
        SceneNodeAsset& node,
        SceneCollisionAsset collision = {})
    {
        node.collision = collision;
    }

    inline void attach_terrain_mesh_source(
        SceneNodeAsset& node,
        SceneTerrainMeshSourceAsset source = {})
    {
        node.terrain_mesh_source = source;
    }

    inline void attach_exclusive_terrain_mesh_source(
        SceneNodeAsset& node,
        SceneTerrainMeshSourceAsset source = {})
    {
        node.terrain_height_field_source.reset();
        attach_terrain_mesh_source(node, std::move(source));
    }

    inline void attach_terrain_height_field_source(
        SceneNodeAsset& node,
        SceneTerrainHeightFieldSourceAsset source = {})
    {
        node.terrain_height_field_source = source;
    }

    inline void attach_exclusive_terrain_height_field_source(
        SceneNodeAsset& node,
        SceneTerrainHeightFieldSourceAsset source = {})
    {
        node.terrain_mesh_source.reset();
        attach_terrain_height_field_source(node, std::move(source));
    }

    inline void attach_terrain_render_style(
        SceneNodeAsset& node,
        SceneTerrainRenderStyleAsset style = {})
    {
        node.terrain_render_style = style;
    }

    inline std::vector<wz::scene::SceneAuthoredComponentKind>
    authored_components_for_node(const SceneNodeAsset& node)
    {
        using Kind = wz::scene::SceneAuthoredComponentKind;

        std::vector<Kind> out{
            Kind::Transform,
            Kind::Visibility,
            Kind::MotionType,
        };

        if (node.parent_id) {
            out.push_back(Kind::ParentLink);
        }
        if (node.renderable || node.renderable_asset_node_id) {
            out.push_back(Kind::Renderable);
        }
        if (node.asset_reference) {
            out.push_back(Kind::AssetReference);
        }
        if (node.scene_import_source) {
            out.push_back(Kind::SceneImportSource);
        }
        if (node.camera) {
            out.push_back(Kind::Camera);
        }
        if (node.direct_light_source) {
            out.push_back(Kind::Light);
        }
        if (node.ambient_lighting) {
            out.push_back(Kind::AmbientLighting);
        }
        if (node.hdri_environment) {
            out.push_back(Kind::HDRIEnvironment);
        }
        if (node.sky_visual) {
            out.push_back(Kind::SkyVisual);
        }
        if (node.sky_surface) {
            out.push_back(Kind::SkySurface);
        }
        if (node.input_receiver) {
            out.push_back(Kind::InputReceiver);
        }
        if (node.flying_camera_controller) {
            out.push_back(Kind::FlyingCameraController);
        }
        if (node.actor_movement_controller) {
            out.push_back(Kind::ActorMovementController);
        }
        if (node.ground_boundary) {
            out.push_back(Kind::GroundBoundary);
        }
        if (node.mesh_source) {
            out.push_back(Kind::MeshSource);
        }
        if (node.mesh_derived_field_source) {
            out.push_back(Kind::MeshDerivedFieldSource);
        }
        if (node.mesh_sparse_operator_source) {
            out.push_back(Kind::MeshSparseOperatorSource);
        }
        if (node.mesh_sparse_apply_field) {
            out.push_back(Kind::MeshSparseApplyField);
        }
        if (node.mesh_sparse_diffusion_bands) {
            out.push_back(Kind::MeshSparseDiffusionBands);
        }
        if (node.mesh_level_mask_source) {
            out.push_back(Kind::MeshLevelMaskSource);
        }
        if (node.mesh_wavelet_analysis) {
            out.push_back(Kind::MeshWaveletAnalysis);
        }
        if (node.mesh_compute_field) {
            out.push_back(Kind::MeshComputeField);
        }
        if (node.mesh_render_style) {
            out.push_back(Kind::MeshRenderStyle);
        }
        if (node.mesh_mask_render_style) {
            out.push_back(Kind::MeshMaskRenderStyle);
        }
        if (node.mesh_region_set) {
            out.push_back(Kind::MeshRegionSet);
        }
        if (node.scalar_field_source) {
            out.push_back(Kind::ScalarFieldSource);
        }
        if (node.vector_field_source) {
            out.push_back(Kind::VectorFieldSource);
        }
        if (node.collision) {
            out.push_back(Kind::Collision);
        }
        if (node.terrain) {
            out.push_back(Kind::Terrain);
        }
        if (node.terrain_render_style) {
            out.push_back(Kind::TerrainRenderStyle);
        }
        if (node.terrain_mesh_source) {
            out.push_back(Kind::TerrainMeshSource);
        }
        if (node.terrain_height_field_source) {
            out.push_back(Kind::TerrainHeightFieldSource);
        }
        if (node.audio_listener) {
            out.push_back(Kind::AudioListener);
        }
        if (node.event_listener) {
            out.push_back(Kind::EventListener);
        }
        if (node.event_trigger) {
            out.push_back(Kind::EventTrigger);
        }
        if (node.proximity) {
            out.push_back(Kind::Proximity);
        }
        if (node.motion) {
            out.push_back(Kind::Motion);
        }
        if (node.behavior || !node.behaviors.empty()) {
            out.push_back(Kind::Behavior);
        }
        if (node.compute_kernel) {
            out.push_back(Kind::ComputeKernel);
        }
        if (node.render_shader) {
            out.push_back(Kind::RenderShader);
        }
        if (node.debug_visual) {
            out.push_back(Kind::AuxiliaryVisual);
        }
        if (node.editor_handle) {
            out.push_back(Kind::EditorHandle);
        }

        return out;
    }

    inline bool has_authored_renderable_component(
        const SceneNodeAsset& node) noexcept
    {
        return node.renderable.has_value()
            || node.renderable_asset_node_id.has_value();
    }

    inline bool has_authored_camera_component(
        const SceneNodeAsset& node) noexcept
    {
        return node.camera.has_value();
    }

    inline bool has_authored_editor_only_components(
        const SceneNodeAsset& node) noexcept
    {
        return node.editor_handle.has_value();
    }

    inline bool has_authored_auxiliary_visual_component(
        const SceneNodeAsset& node) noexcept
    {
        return node.debug_visual.has_value();
    }

    inline const SceneNodeAsset* find_scene_node(
        const SceneAssetData& scene,
        const wz::scene::AuthoredEntityId& id) noexcept
    {
        for (const auto& node : scene.nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    }

    inline SceneNodeAsset* find_scene_node(
        SceneAssetData& scene,
        const wz::scene::AuthoredEntityId& id) noexcept
    {
        for (auto& node : scene.nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    }

    // Same lookup over a bare node list — the form WozzitsApp_v1 keeps its live
    // scene in (scene_nodes_), where there is no enclosing SceneAssetData.
    inline const SceneNodeAsset* find_scene_node(
        const std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& id) noexcept
    {
        for (const auto& node : nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    }

    inline SceneNodeAsset* find_scene_node(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& id) noexcept
    {
        for (auto& node : nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    }

    // Mint the next free node id for a flat node list. New nodes use a plain
    // integer counter (as a string); existing non-numeric ids (root, mesh, ...)
    // are skipped, so the counter only fills numeric slots and never collides.
    inline wz::scene::AuthoredEntityId mint_scene_node_id(
        const std::vector<SceneNodeAsset>& nodes)
    {
        uint64_t next = 1u;
        for (const SceneNodeAsset& node : nodes) {
            if (node.id.empty()) {
                continue;
            }
            uint64_t value = 0u;
            bool numeric = true;
            for (const char c : node.id) {
                if (c < '0' || c > '9') {
                    numeric = false;
                    break;
                }
                value = (value * 10u) + static_cast<uint64_t>(c - '0');
            }
            if (numeric && value >= next) {
                next = value + 1u;
            }
        }
        return std::to_string(next);
    }

    struct SceneAddChildResult
    {
        bool ok = false;
        wz::scene::AuthoredEntityId new_id;
        std::string error;
    };

    // Append a new child node (no components, no label yet) under parent_id in a
    // flat node list. An empty parent_id adds at the top level; otherwise the
    // parent must exist. The id is minted via mint_scene_node_id. This is the
    // in-memory apply behind the editor's "add child"; persistence is separate.
    inline SceneAddChildResult add_child_scene_node(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& parent_id)
    {
        if (!parent_id.empty() && !find_scene_node(nodes, parent_id)) {
            return SceneAddChildResult{
                .ok = false,
                .new_id = {},
                .error = "parent node does not exist",
            };
        }

        SceneNodeAsset node;
        node.id = mint_scene_node_id(nodes);
        const wz::scene::AuthoredEntityId new_id = node.id;
        if (!parent_id.empty()) {
            node.parent_id = parent_id;
        }
        nodes.push_back(std::move(node));
        return SceneAddChildResult{ .ok = true, .new_id = new_id, .error = {} };
    }

    // Set a node's editable label (name) and visibility in a flat node list.
    // Returns false if no node has that id. The in-memory apply behind the
    // editor's live name/visibility edits; persistence is a separate path.
    inline bool set_scene_node_properties(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& id,
        std::string name,
        bool visible)
    {
        SceneNodeAsset* node = find_scene_node(nodes, id);
        if (!node) {
            return false;
        }
        node->name = std::move(name);
        node->visible = visible;
        return true;
    }

    inline bool is_direct_child_scene_node(
        const SceneNodeAsset& parent,
        const SceneNodeAsset& child) noexcept
    {
        return child.parent_id.has_value() && *child.parent_id == parent.id;
    }

    inline bool is_terrain_mesh_source_node_candidate(
        const SceneNodeAsset& terrain_node,
        const SceneNodeAsset& candidate) noexcept
    {
        return candidate.id != terrain_node.id
            && candidate.mesh_source.has_value()
            && is_direct_child_scene_node(terrain_node, candidate);
    }

    inline bool is_terrain_height_field_source_node_candidate(
        const SceneNodeAsset& terrain_node,
        const SceneNodeAsset& candidate) noexcept
    {
        return candidate.id != terrain_node.id
            && candidate.scalar_field_source.has_value()
            && is_direct_child_scene_node(terrain_node, candidate);
    }

    inline std::vector<wz::scene::AuthoredEntityId>
    terrain_mesh_source_candidate_nodes(
        const SceneAssetData& scene,
        const SceneNodeAsset& terrain_node)
    {
        std::vector<wz::scene::AuthoredEntityId> out;
        for (const auto& candidate : scene.nodes) {
            if (is_terrain_mesh_source_node_candidate(
                    terrain_node,
                    candidate))
            {
                out.push_back(candidate.id);
            }
        }
        return out;
    }

    inline std::vector<wz::scene::AuthoredEntityId>
    terrain_height_field_source_candidate_nodes(
        const SceneAssetData& scene,
        const SceneNodeAsset& terrain_node)
    {
        std::vector<wz::scene::AuthoredEntityId> out;
        for (const auto& candidate : scene.nodes) {
            if (is_terrain_height_field_source_node_candidate(
                    terrain_node,
                    candidate))
            {
                out.push_back(candidate.id);
            }
        }
        return out;
    }

    inline bool is_referenced_terrain_source_node(
        const SceneAssetData& scene,
        const SceneNodeAsset& node) noexcept
    {
        for (const auto& owner : scene.nodes) {
            if (owner.terrain_mesh_source
                && owner.terrain_mesh_source->source_node == node.id)
            {
                return true;
            }
            if (owner.terrain_height_field_source
                && owner.terrain_height_field_source->source_node == node.id)
            {
                return true;
            }
        }
        return false;
    }

    inline bool can_own_materialized_renderable_asset(
        const SceneAssetData& scene,
        const SceneNodeAsset& node) noexcept
    {
        if (is_referenced_terrain_source_node(scene, node)) {
            return false;
        }

        return node.renderable.has_value()
            || node.renderable_asset_node_id.has_value()
            || node.mesh_source.has_value()
            || node.imported_node.has_value()
            || node.scalar_field_source.has_value()
            || node.vector_field_source.has_value()
            || node.terrain.has_value()
            || node.terrain_mesh_source.has_value()
            || node.terrain_height_field_source.has_value();
    }

    inline uint32_t detach_stale_materialized_renderable_assets(
        SceneAssetData& scene)
    {
        uint32_t detached = 0;
        for (auto& node : scene.nodes) {
            if (node.renderable_asset
                && !can_own_materialized_renderable_asset(scene, node))
            {
                detach_renderable_asset_node(node);
                ++detached;
            }
        }
        return detached;
    }

    inline bool has_authored_debug_visual_component(
        const SceneNodeAsset& node) noexcept
    {
        return has_authored_auxiliary_visual_component(node);
    }

    inline bool has_asset_authoring_recipes(
        const SceneNodeAsset& node) noexcept
    {
        return node.scene_import_source.has_value()
            || node.mesh_source.has_value()
            || node.mesh_processing.has_value()
            || node.mesh_derived_field_source.has_value()
            || node.mesh_sparse_operator_source.has_value()
            || node.mesh_sparse_apply_field.has_value()
            || node.mesh_sparse_diffusion_bands.has_value()
            || node.mesh_level_mask_source.has_value()
            || node.mesh_wavelet_analysis.has_value()
            || node.mesh_compute_field.has_value()
            || node.mesh_render_style.has_value()
            || node.mesh_mask_render_style.has_value()
            || node.mesh_region_set.has_value()
            || node.scalar_field_source.has_value()
            || node.vector_field_source.has_value()
            || node.direct_light_source.has_value()
            || node.ambient_lighting.has_value()
            || node.hdri_environment.has_value()
            || node.sky_visual.has_value()
            || node.sky_surface.has_value()
            || node.terrain_render_style.has_value()
            || node.terrain_mesh_source.has_value()
            || node.terrain_height_field_source.has_value()
            || node.event_trigger.has_value()
            || node.compute_kernel.has_value()
            || node.render_shader.has_value();
    }

    inline bool has_runtime_relevant_components(
        const SceneNodeAsset& node) noexcept
    {
        return has_authored_renderable_component(node)
            || has_authored_camera_component(node)
            || node.direct_light_source.has_value()
            || node.ambient_lighting.has_value()
            || node.hdri_environment.has_value()
            || node.sky_visual.has_value()
            || node.sky_surface.has_value()
            || node.input_receiver.has_value()
            || node.flying_camera_controller.has_value()
            || node.actor_movement_controller.has_value()
            || node.ground_boundary.has_value()
            || node.collision.has_value()
            || node.terrain.has_value()
            || node.audio_listener.has_value()
            || node.event_listener.has_value()
            || node.proximity.has_value()
            || node.motion.has_value()
            || node.behavior.has_value()
            || !node.behaviors.empty()
            || node.compute_kernel.has_value()
            || node.render_shader.has_value()
            || node.debug_visual.has_value();
    }

    inline SceneAssetAuthoringRecipeSummary
    summarize_scene_asset_authoring_recipes(const SceneAssetData& scene)
    {
        SceneAssetAuthoringRecipeSummary out{};

        for (const auto& node : scene.nodes) {
            if (!has_asset_authoring_recipes(node)) {
                continue;
            }

            ++out.nodes_with_recipes;

            if (node.scene_import_source) {
                ++out.scene_import_sources;
                ++out.total_recipes;
            }
            if (node.mesh_source) {
                ++out.mesh_sources;
                ++out.total_recipes;
            }
            if (node.mesh_processing) {
                ++out.mesh_processing;
                ++out.total_recipes;
            }
            if (node.mesh_derived_field_source) {
                ++out.mesh_derived_field_sources;
                ++out.total_recipes;
            }
            if (node.mesh_sparse_operator_source) {
                ++out.mesh_sparse_operator_sources;
                ++out.total_recipes;
            }
            if (node.mesh_sparse_apply_field) {
                ++out.mesh_sparse_apply_fields;
                ++out.total_recipes;
            }
            if (node.mesh_sparse_diffusion_bands) {
                ++out.mesh_sparse_diffusion_bands;
                ++out.total_recipes;
            }
            if (node.mesh_level_mask_source) {
                ++out.mesh_level_mask_sources;
                ++out.total_recipes;
            }
            if (node.mesh_wavelet_analysis) {
                ++out.mesh_wavelet_analyses;
                ++out.total_recipes;
            }
            if (node.mesh_compute_field) {
                ++out.mesh_compute_fields;
                ++out.total_recipes;
            }
            if (node.mesh_render_style) {
                ++out.mesh_render_styles;
                ++out.total_recipes;
            }
            if (node.mesh_mask_render_style) {
                ++out.mesh_mask_render_styles;
                ++out.total_recipes;
            }
            if (node.mesh_region_set) {
                ++out.mesh_region_sets;
                ++out.total_recipes;
            }
            if (node.scalar_field_source) {
                ++out.scalar_field_sources;
                ++out.total_recipes;
            }
            if (node.vector_field_source) {
                ++out.vector_field_sources;
                ++out.total_recipes;
            }
            if (node.direct_light_source) {
                ++out.direct_light_sources;
                ++out.total_recipes;
            }
            if (node.ambient_lighting) {
                ++out.ambient_lighting;
                ++out.total_recipes;
            }
            if (node.hdri_environment) {
                ++out.hdri_environments;
                ++out.total_recipes;
            }
            if (node.sky_visual) {
                ++out.sky_visuals;
                ++out.total_recipes;
            }
            if (node.sky_surface) {
                ++out.sky_surfaces;
                ++out.total_recipes;
            }
            if (node.terrain_render_style) {
                ++out.terrain_render_styles;
                ++out.total_recipes;
            }
            if (node.terrain_mesh_source) {
                ++out.terrain_mesh_sources;
                ++out.total_recipes;
            }
            if (node.terrain_height_field_source) {
                ++out.terrain_height_field_sources;
                ++out.total_recipes;
            }
            if (node.event_trigger) {
                ++out.event_triggers;
                ++out.total_recipes;
            }
            if (node.compute_kernel) {
                ++out.compute_kernels;
                ++out.total_recipes;
            }
            if (node.render_shader) {
                ++out.render_shaders;
                ++out.total_recipes;
            }
        }

        return out;
    }

    inline wz::scene::SceneAuthoredComponentSummary summarize_authored_scene_components(
        const SceneAssetData& scene)
    {
        wz::scene::SceneAuthoredComponentSummary out{};
        out.nodes = static_cast<uint32_t>(scene.nodes.size());
        out.transforms = out.nodes;
        out.visibility = out.nodes;
        out.motion_types = out.nodes;
        out.lights = static_cast<uint32_t>(scene.lights.size());

        for (const auto& node : scene.nodes) {
            if (node.parent_id) {
                ++out.parent_links;
            }
            if (has_authored_renderable_component(node)) {
                ++out.renderables;
            }
            if (node.asset_reference) {
                ++out.asset_references;
            }
            if (node.scene_import_source) {
                ++out.scene_import_sources;
            }
            if (has_authored_camera_component(node)) {
                ++out.cameras;
            }
            if (node.direct_light_source) {
                ++out.lights;
            }
            if (node.ambient_lighting) {
                ++out.ambient_lighting;
            }
            if (node.hdri_environment) {
                ++out.hdri_environments;
            }
            if (node.sky_visual) {
                ++out.sky_visuals;
            }
            if (node.sky_surface) {
                ++out.sky_surfaces;
            }
            if (node.input_receiver) {
                ++out.input_receivers;
            }
            if (node.flying_camera_controller) {
                ++out.flying_camera_controllers;
            }
            if (node.actor_movement_controller) {
                ++out.actor_movement_controllers;
            }
            if (node.ground_boundary) {
                ++out.ground_boundaries;
            }
            if (node.mesh_source) {
                ++out.mesh_sources;
            }
            if (node.mesh_derived_field_source) {
                ++out.mesh_derived_field_sources;
            }
            if (node.mesh_sparse_operator_source) {
                ++out.mesh_sparse_operator_sources;
            }
            if (node.mesh_sparse_apply_field) {
                ++out.mesh_sparse_apply_fields;
            }
            if (node.mesh_sparse_diffusion_bands) {
                ++out.mesh_sparse_diffusion_bands;
            }
            if (node.mesh_level_mask_source) {
                ++out.mesh_level_mask_sources;
            }
            if (node.mesh_wavelet_analysis) {
                ++out.mesh_wavelet_analyses;
            }
            if (node.mesh_compute_field) {
                ++out.mesh_compute_fields;
            }
            if (node.mesh_render_style) {
                ++out.mesh_render_styles;
            }
            if (node.mesh_mask_render_style) {
                ++out.mesh_mask_render_styles;
            }
            if (node.mesh_region_set) {
                ++out.mesh_region_sets;
            }
            if (node.scalar_field_source) {
                ++out.scalar_field_sources;
            }
            if (node.vector_field_source) {
                ++out.vector_field_sources;
            }
            if (node.collision) {
                ++out.collisions;
            }
            if (node.terrain) {
                ++out.terrains;
            }
            if (node.terrain_render_style) {
                ++out.terrain_render_styles;
            }
            if (node.terrain_mesh_source) {
                ++out.terrain_mesh_sources;
            }
            if (node.terrain_height_field_source) {
                ++out.terrain_height_field_sources;
            }
            if (node.audio_listener) {
                ++out.audio_listeners;
            }
            if (node.event_listener) {
                ++out.event_listeners;
            }
            if (node.event_trigger) {
                ++out.event_triggers;
            }
            if (node.proximity) {
                ++out.proximities;
            }
            if (node.motion) {
                ++out.motions;
            }
            if (node.behavior) {
                ++out.behaviors;
            }
            out.behaviors += static_cast<uint32_t>(node.behaviors.size());
            if (node.compute_kernel) {
                ++out.compute_kernels;
            }
            if (node.render_shader) {
                ++out.render_shaders;
            }
            if (node.debug_visual) {
                ++out.auxiliary_visuals;
            }
            if (node.editor_handle) {
                ++out.editor_handles;
            }
        }

        return out;
    }

} // namespace wz::engine::assets
