#pragma once

// engine/assets/scene/scene_asset_data.h

#include <asset/draft.h>
#include <asset/types.h>

#include <engine/assets/light/light.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_render_style/mesh_render_style.h>
#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator.h>
#include <engine/assets/vector_field/vector_field.h>

#include <scene/scene_ecs.h>
#include <scene/transform_node.h>
#include <scene/compile/compiled_scene.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

    // The frame's global fog, bound BY REFERENCE to an authored asset-graph
    // Atmosphere node (6c51cf5). Sits beside ambient_lighting/hdri_environment as
    // per-node environment state, but unlike them it is FRAME-GLOBAL: the medium
    // every program looks through, packed once per frame into the view-frequency
    // constants. It lives on a node only because that is where the scene language
    // puts authored state — the node's transform means nothing to it, and a second
    // node carrying one is an authoring error, not a blend.
    //
    // Identity follows SceneCollisionAsset: atmosphere_asset_node_id is the
    // persisted intent (a stable asset-graph anchor), atmosphere_asset is the
    // current resolved key, disposable and re-derived by
    // bridge_scene_atmosphere_keys on every (re)bind. See the
    // authored-vs-resolved identity rule.
    struct SceneAtmosphereAsset
    {
        wz::asset::AssetKey atmosphere_asset{};
        // 0 = unbound. Draft node ids are handed out from 1 (draft.h
        // next_node_id), so 0 is never a real anchor and needs no optional<>.
        wz::asset::AssetGraphDraftNodeId atmosphere_asset_node_id = 0;
        // Master switch for THIS binding, distinct from AtmosphereData::
        // fog_enabled (the asset's own switch). Disabled here means the frame
        // has no atmosphere at all, exactly as if the component were absent.
        bool enabled = true;
    };

    // The frame's global environment, bound BY REFERENCE to an authored
    // asset-graph FrameEnvironment node -- the single CONNECTED producer of the
    // frame's atmosphere / ambient / HDRI / directional light. Sits beside
    // atmosphere as per-node environment state and, like it, is FRAME-GLOBAL: a
    // second node carrying one is an authoring error, not a blend. This is the
    // successor to the standalone atmosphere component -- the renderer prefers a
    // frame environment and falls back to a standalone atmosphere for scenes
    // authored before it existed (back-compat).
    //
    // Identity follows SceneAtmosphereAsset: environment_asset_node_id is the
    // persisted intent (a stable asset-graph anchor); environment_asset is the
    // current resolved key, disposable and re-derived by
    // bridge_scene_environment_keys on every (re)bind.
    struct SceneEnvironmentAsset
    {
        wz::asset::AssetKey environment_asset{};
        // 0 = unbound. Draft node ids are handed out from 1, so 0 is never a real
        // anchor and needs no optional<>.
        wz::asset::AssetGraphDraftNodeId environment_asset_node_id = 0;
        // Master switch for THIS binding. Disabled means the frame has no
        // environment at all, exactly as if the component were absent.
        bool enabled = true;
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

    // Authoring recipe for a movement-constraint collision surface built
    // DIRECTLY from a scalar field heightfield + world mapping (no
    // TerrainAsset). When present and a scalar field is set, the scene
    // materialize pass resolves it to SceneCollisionAsset::collision_asset.
    struct SceneCollisionHeightFieldSource
    {
        wz::asset::AssetKey scalar_field_asset{};
        float origin[2]{ 0.0f, 0.0f };
        float size[2]{ 1.0f, 1.0f };
        float vertical_scale = 1.0f;
        float base_height = 0.0f;
        uint32_t projection_resolution_x = 0;
        uint32_t projection_resolution_y = 0;
    };

    struct SceneCollisionAsset
    {
        wz::asset::AssetKey collision_asset{};
        uint32_t layer_mask = 1;
        uint32_t collides_with_mask = 0xffffffffu;
        bool is_trigger = false;
        bool enabled = true;
        // When true, the runtime treats this collision as a terrain-style
        // constraint surface that Motion actors stick to.
        bool constrain_movement = false;
        // Collision binding by REFERENCE (issue #216/#217): the node points at an
        // authored asset-graph collision node (e.g. collision_from_height_field);
        // collision_asset is the current resolved key, re-bridged on every
        // (re)bind via bridge_scene_collision_keys, mirroring render_program_node_id.
        // The persisted intent is the node id; the key is disposable. Takes
        // precedence over the inline height_field_source materialize path.
        std::optional<wz::asset::AssetGraphDraftNodeId> collision_asset_node_id;
        std::optional<SceneCollisionHeightFieldSource> height_field_source;
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

    // Authored audio source: references an audio-renderable terminal (the audio
    // analog of renderable_asset) plus per-entity play policy. The node transform
    // supplies spatial data (pan/attenuation vs the listener) — see item 8.
    //
    // Reference identity mirrors the visual renderable's (node_id + key) pairing:
    // audio_renderable_node_id is the STABLE authored asset-graph anchor that
    // survives re-authoring (a renderable's content AssetKey changes when its
    // clip/gain/pitch change), while audio_renderable is the current resolved key
    // re-derived at bind time. Authored scenes should prefer the node id; the key
    // is the runtime-ready resolved form. (See the authored-vs-resolved identity
    // rule.) An empty audio_renderable key means "unresolved / node-id only".
    struct SceneAudioSourceAsset
    {
        std::optional<wz::asset::AssetGraphDraftNodeId> audio_renderable_node_id;
        wz::asset::AssetKey audio_renderable{};
        bool auto_play = true;  // start playing when the scene instantiates
        bool enabled = true;    // participate at all
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
        // Orientation footprint: ring radius for surface-normal averaging (used
        // to align the actor to the surface). It does NOT set height -- height
        // comes from the CENTER sample under the actor pivot. 0 = point support
        // (no ring). A ring sample can catch a nearby peak, so letting it drive
        // height would levitate an actor on convex terrain; height stays on the
        // ground under the center, the ring only tilts the orientation normal.
        float terrain_footprint_radius = 0.0f;
        bool terrain_align_to_surface = false;
        float terrain_alignment_strength = 1.0f;
        bool enabled = true;
    };

    // ─── Motion Filter component (per-DOF smoothing + clamp) ────────────────
    // A behavior (or a moving parent) drives a node's transform; the Motion
    // Filter smooths and/or clamps the RESULT per axis before it reaches the
    // node's subtree + camera. It lets an attached camera EASE into its target
    // pose (secondary motion, so the followed object doesn't look locked in
    // space), optionally hold a level horizon, stay above the terrain, and keep
    // rotation within limits -- each axis configured independently. Frames are
    // deliberately mixed to match intent: translation channels are WORLD axes
    // (vertical bob = world Y, terrain floor = world Y); rotation channels are
    // the node's LOCAL axes (roll = about forward, pitch = right, yaw = up), so
    // "damp the roll" is a single channel. Every field defaults to a no-op, so
    // the component present-but-untouched behaves as if absent. This is the
    // authored data (seam 1); the runtime filter pass + editor UI are later
    // seams.

    // One local-axis rotation channel (roll / pitch / yaw).
    struct SceneMotionFilterRotationAxis
    {
        // Critically-damped chase time toward the target, in seconds. 0 =
        // instant (the axis tracks its target rigidly, no damping).
        float smoothing_time = 0.0f;
        // Target the axis eases toward: false = FOLLOW the driven/attached
        // orientation; true = LEVEL (ease toward world-level / the horizon).
        bool level = false;
        // Optional hard clamp on the resulting axis angle (world-referenced
        // degrees, e.g. pitch in [-80, 80] so the camera never flips over).
        bool limit = false;
        float limit_min_degrees = 0.0f;
        float limit_max_degrees = 0.0f;
    };

    struct SceneMotionFilterAsset
    {
        // Per world-axis translation chase time, seconds; index 0/1/2 = X/Y/Z.
        // 0 = pass straight through (e.g. keep the vertical bob live on Y).
        float translation_smoothing[3]{ 0.0f, 0.0f, 0.0f };
        // Terrain floor on world Y: keep the node at or above the terrain height
        // beneath it plus this offset. A ONE-SIDED clamp (free to rise, blocked
        // from sinking) -- distinct from the Motion component's rigid terrain
        // stick.
        bool terrain_floor = false;
        float terrain_floor_offset = 0.0f;

        // Local-axis rotation channels.
        SceneMotionFilterRotationAxis roll;   // about local forward
        SceneMotionFilterRotationAxis pitch;  // about local right
        SceneMotionFilterRotationAxis yaw;    // about local up

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

    // How a node consumes a referenced Scene asset (issue #213):
    //   Instance — keep a live reference; the runtime grafts the sub-scene as
    //              children of the host at load (re-imports cleanly, small json).
    //   Flatten  — a one-time expansion of the sub-scene into real, editable
    //              host scene nodes (no live link kept).
    // Rides on the SceneFromGLB asset node's params (param "consume_mode"); the
    // host node only stores the reference (scene_source_node_id / scene_source).
    enum class SceneSourceConsumeMode : uint8_t
    {
        Instance = 0,
        Flatten,
    };

    // Per-component render-style override for a GLB scene-source descriptor
    // (issue #213). Keyed by GLB mesh index, mirroring
    // wz::engine::assets::SceneFromGLBStyleOverride (the create_scene_from_glb
    // input). Declared here — not reused from scene_asset_module.h — because
    // scene_asset_module.h includes this header, so the dependency may only run
    // one way; the materialize pass converts these into SceneFromGLBStyleOverride.
    struct SceneGLBSceneSourceStyleOverride
    {
        uint32_t mesh_index = 0;
        MeshRenderStyleData style{};
    };

    // GLB scene-source DESCRIPTOR (issue #213, the terrain_collision/mesh_source
    // model). A host node carries this authored descriptor; the runtime resolves
    // it AT scene materialization into a "Scene from GLB" asset and writes the
    // resolved Scene key into SceneNodeAsset::scene_source — then the existing
    // graft (instance) or flatten consumes it. Unlike scene_source_node_id (an
    // asset-graph node reference resolved via the draft bridge), this descriptor
    // is self-contained plain data: it survives rebinds (re-resolved every load /
    // rebind — same content => same key => cache hit) and persists as plain JSON,
    // independent of the asset graph. Precedence: if a node has glb_scene_source
    // it produces scene_source; else scene_source_node_id (if set) does. One
    // resolved key, one graft.
    struct SceneGLBSceneSource
    {
        // Resource-relative GLB file path (mirrors SceneMeshSourceAsset::path).
        std::string path;
        uint32_t scene_index = 0;

        // How the resolved Scene is consumed (instance graft vs persistent
        // flatten); mirrors SceneFromGLBCompileDesc::consume_mode.
        SceneSourceConsumeMode consume_mode = SceneSourceConsumeMode::Instance;

        // Per-component style mapping, mirroring SceneFromGLBDesc: a base style
        // applied to every imported mesh, plus sparse per-mesh-index overrides.
        // Unset base_style => the built-in default MeshRenderStyleData.
        std::optional<MeshRenderStyleData> base_style;
        std::vector<SceneGLBSceneSourceStyleOverride> style_overrides;
    };

    // Per-child authored-component override for a scene-source host (issue #213).
    // A host node's scene source expands into runtime-only grafted children that
    // save_scene excludes; without this, a component authored on a grafted child
    // (e.g. a render program) is lost on reload. The host carries a sticky list of
    // overrides keyed by the SOURCE node's stable id (child_id = the sub-scene id,
    // i.e. the grafted child's `<host>/<child_id>` suffix). On every (re)graft the
    // engine applies a matching override onto the expanded child AFTER expansion —
    // these are scene-graph authoring patches, NOT folded into the GLB Scene key
    // (unlike style_overrides). Overrides are applied opportunistically: an entry
    // whose child_id is absent after expansion (renamed/removed source node, or a
    // transient resolve failure) is RETAINED (logged), never auto-deleted, so a
    // later source fix re-attaches it. Only an explicit clear removes one.
    struct SceneSourceChildOverride
    {
        std::string child_id;
        std::optional<wz::asset::AssetGraphDraftNodeId> render_program_node_id;
    };

    // One authored semantic resource binding on a scene node (issue #229),
    // extending the #213 geometry/render_program ingredients cluster.
    // `semantic` names a row of the effective render program's authored
    // binding layout; asset_graph_node_id is the STABLE authored anchor
    // (survives re-authoring of the source) and `asset` the current resolved
    // key, cleared + re-bridged on every (re)bind exactly like
    // renderable_asset (the authored-vs-resolved identity rule). An empty
    // key means "unresolved / node-id only" — the synthesized custom
    // renderable's compile then names the unbound row.
    struct SceneRenderableSemanticBinding
    {
        std::string semantic;
        std::optional<wz::asset::AssetGraphDraftNodeId> asset_graph_node_id;
        wz::asset::AssetKey asset{};
    };

    // A per-instance value for one of the program layout's declared constant
    // tail fields (issue #229). Applied at PACK time over the synthesized
    // recipe's defaults — editing one never re-keys or recompiles anything
    // (the look analog of transform-on-node: instance data stays on the
    // node). Fields narrower than 4 dwords consume value[0..width).
    struct SceneRenderableConstantOverride
    {
        std::string name;
        float value[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // Issue #287: the authored answer to "who renders INTO this render-target
    // texture". #281 made the target authorable -- a kAssetTypeTexture resident
    // Sampled | RenderTarget that a material can bind -- but an authored target
    // with no app code behind it was never written to, so a surface sampled
    // whatever the acquire left. This component closes that loop: the node
    // carrying it (and, by default, its descendants) is rendered into the named
    // texture once per frame, before the main pass.
    //
    // The scene tree IS the selector -- no separate tagging language -- which is
    // exactly what the puppet showcase does by hand in app C++ today: gather
    // some nodes, render them into a texture, let a surface sample it.
    struct SceneRenderToTextureAsset
    {
        // The render-target texture drawn into. target_node_id is the authored
        // intent (an asset-graph node); target is the key resolved on every
        // (re)bind, mirroring renderable_asset. A target that does not resolve,
        // or is not resident as a render target, renders nothing and warns --
        // it never falls back to drawing somewhere else.
        std::optional<wz::asset::AssetGraphDraftNodeId> target_node_id;
        wz::asset::AssetKey target{};

        // Draw this node's descendants into the target too. Off = only the node
        // itself, for a subtree whose children are unrelated.
        bool include_descendants = true;

        // Also draw the same nodes in the main pass. Default OFF: the motivating
        // case is art that lives ON a surface (the puppet card), where drawing it
        // both to the texture and to the screen shows it twice. A mirror or a
        // security monitor -- whose source is world geometry you are already
        // drawing -- turns this on.
        bool also_draw_in_scene = false;

        bool enabled = true;
    };

    struct SceneNodeAsset
    {
        wz::scene::AuthoredEntityId id;
        std::optional<wz::scene::AuthoredEntityId> parent_id;
        std::string name;

        AuthoredTransform local{};

        bool visible = true;

        // Draw-order key. The renderer walks the flat node array in order (no
        // sort, no tree traversal), so draw order is purely array position. This
        // is an explicit, tree-position-independent layer key: the node list is
        // stable-sorted by render_order when it is (re)built, LOWER draws FIRST.
        // Ties keep authored array order, so default 0 everywhere renders exactly
        // as before. Editor exposes it as a coarse layer (Background/World/
        // Overlay); a future RHI layering pass can drive it with richer rules.
        int render_order = 0;

        // "Live?" axis (#252), orthogonal to `visible` ("drawn?"). Hierarchical:
        // a node is effectively active only if it AND every ancestor is active.
        // Gates behavior dispatch + the collision frame (a parked/pooled node
        // stops ticking + colliding), but NOT rendering. Default true = live.
        bool active = true;

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

        // Renderable binding by INGREDIENTS (issue #213): an alternative to the
        // pre-assembled renderable_asset above. The node carries a GEOMETRY
        // asset-graph node + a RENDER-PROGRAM asset-graph node, and the engine
        // assembles the RhiRenderableRecipe at load (geometry routed to the
        // recipe slot by its asset type). The render program is INHERITED down
        // the scene tree: a node with geometry but no program of its own draws
        // with its nearest ancestor's program; geometry is per-node. The *_asset
        // keys are this node's own resolved keys (re-bridged each bind, like
        // renderable_asset); the inherited program is resolved at assembly time,
        // never stored. Takes precedence over renderable_asset when geometry is
        // present.
        std::optional<wz::asset::AssetGraphDraftNodeId> geometry_asset_node_id;
        std::optional<wz::asset::AssetKey> geometry_asset;
        // Indexed GLB-part geometry (issue #213 increment 3, option b): when set,
        // geometry_asset_node_id references a "Scene from GLB" node and this names
        // the GLB part (glTF node id) to extract as this node's geometry. One shared
        // Scene-from-GLB node then feeds every part by name, instead of one
        // extractor graph node per part. The engine extracts the part mesh at
        // assembly time (create_mesh_from_glb_scene) and routes it like any Mesh.
        std::optional<std::string> geometry_glb_node_id;
        std::optional<wz::asset::AssetGraphDraftNodeId> render_program_node_id;
        std::optional<wz::asset::AssetKey> render_program_asset;

        // Custom-renderable ingredients (issue #229), completing the cluster
        // above: when either vector is non-empty (and geometry is a Mesh),
        // the engine synthesizes a CUSTOM (0x70A) renderable from geometry +
        // program + these bindings instead of the bare pull-mesh recipe.
        // Constant overrides never enter the synthesized asset — they ride
        // the node to the renderer and merge over the recipe's defaults per
        // frame.
        std::vector<SceneRenderableSemanticBinding> renderable_bindings;
        std::vector<SceneRenderableConstantOverride> renderable_constants;

        // Render-to-texture source (issue #287): this node's subtree fills the
        // named render-target texture each frame.
        std::optional<SceneRenderToTextureAsset> render_to_texture;

        // Preferred authored Scene-source component (issue #213): the host node
        // consumes the output of a "Scene from GLB" asset node as a sub-tree.
        // scene_source_node_id points at the authored asset-graph node (the
        // intent); scene_source is the current resolved Scene key (bridged on
        // every (re)bind, mirroring renderable_asset). The runtime grafts the
        // referenced scene's named nodes as children of this host (instance
        // mode); flatten expands them in place once at author time.
        std::optional<wz::asset::AssetGraphDraftNodeId> scene_source_node_id;
        std::optional<wz::asset::AssetKey> scene_source;

        // GLB scene-source descriptor (issue #213): the alternate, asset-graph-
        // independent route to scene_source. Resolved at scene materialization
        // (resolve_glb_scene_sources) into a create_scene_from_glb Scene asset,
        // whose key is written into scene_source above; the existing graft /
        // flatten then consumes it. Takes precedence over scene_source_node_id.
        std::optional<SceneGLBSceneSource> glb_scene_source;

        // Authored-component overrides for this host's grafted scene-source
        // children (issue #213). Keyed by the source node's stable id; applied to
        // the expanded child after every graft, persisted on the host, excluded
        // children carry their authored components through reload via these. See
        // SceneSourceChildOverride for the sticky/opportunistic apply policy.
        std::vector<SceneSourceChildOverride> scene_source_child_overrides;

        // Which glTF mesh this node uses, if any (issue #213). Set on the
        // "Scene from GLB" graph-compile path so the geometry an extractor reads
        // (SceneAssetData::glb_meshes) can be located by node id. This is purely
        // a lookup key into the scene's embedded per-mesh geometry; the graft /
        // flatten paths ignore it (renderables come through renderable_asset).
        std::optional<uint32_t> mesh_index;

        std::optional<SceneAssetReferenceAsset> asset_reference;
        std::optional<SceneCameraAsset> camera;
        std::optional<SceneDirectLightSourceAsset> direct_light_source;
        std::optional<SceneAmbientLightingAsset> ambient_lighting;
        std::optional<SceneHDRIEnvironmentAsset> hdri_environment;
        std::optional<SceneAtmosphereAsset> atmosphere;
        std::optional<SceneEnvironmentAsset> environment;
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
        std::optional<SceneAudioSourceAsset> audio_source;
        std::optional<SceneEventListenerAsset> event_listener;
        std::optional<SceneEventTriggerAsset> event_trigger;
        std::optional<SceneProximityAsset> proximity;
        std::optional<SceneMotionAsset> motion;
        std::optional<SceneMotionFilterAsset> motion_filter;
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

        // Per-mesh raw, OBJECT-SPACE geometry imported from a GLB, keyed by the
        // glTF mesh_index (issue #213). Populated only on the "Scene from GLB"
        // graph-compile path; it is the geometry an extractor ("Mesh from GLB
        // scene") reads to produce a standalone Mesh asset for a chosen node.
        // The mesh is verbatim from import_glb_meshes — NO node transform is
        // baked in: the GLB nodes are grafted into the runtime scene tree WITH
        // their transforms, so the scene node owns placement. The graft / flatten
        // paths ignore this map.
        std::unordered_map<uint32_t, MeshData> glb_meshes;

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

        // How a host node consuming this Scene asset expands it (issue #213).
        // Authored via the node's "consume_mode" param; default = Instance.
        // Note: the compiler itself is mode-agnostic (it always produces the
        // full Scene asset); this is carried so a consumer can read the authored
        // intent. The runtime/editor decides graft-as-children vs flatten.
        SceneSourceConsumeMode consume_mode = SceneSourceConsumeMode::Instance;
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

    // Renderable-binding-by-ingredients helpers (issue #213) — mirror the
    // renderable_asset_node helpers: geometry + render-program asset-graph node
    // refs, each with its resolved key re-bridged on (re)bind.
    inline void attach_geometry_asset_node(
        SceneNodeAsset& node,
        wz::asset::AssetGraphDraftNodeId node_id,
        wz::asset::AssetKey geometry_asset = {})
    {
        node.geometry_asset_node_id = node_id;
        node.geometry_asset = geometry_asset;
    }

    inline void detach_geometry_asset_node(SceneNodeAsset& node)
    {
        node.geometry_asset_node_id.reset();
        node.geometry_asset.reset();
        node.geometry_glb_node_id.reset();
    }

    // Indexed GLB-part geometry (issue #213 increment 3): point this node's
    // geometry at a part (glTF node id) of the Scene the asset-graph node
    // `scene_from_glb_node_id` produces. The engine extracts the part mesh at
    // assembly time; one Scene-from-GLB node feeds many parts by name.
    inline void attach_geometry_glb_part(
        SceneNodeAsset& node,
        wz::asset::AssetGraphDraftNodeId scene_from_glb_node_id,
        std::string glb_node_id,
        wz::asset::AssetKey geometry_asset = {})
    {
        node.geometry_asset_node_id = scene_from_glb_node_id;
        node.geometry_glb_node_id = std::move(glb_node_id);
        node.geometry_asset = geometry_asset;
    }

    inline void attach_render_program_node(
        SceneNodeAsset& node,
        wz::asset::AssetGraphDraftNodeId node_id,
        wz::asset::AssetKey render_program_asset = {})
    {
        node.render_program_node_id = node_id;
        node.render_program_asset = render_program_asset;
    }

    inline void detach_render_program_node(SceneNodeAsset& node)
    {
        node.render_program_node_id.reset();
        node.render_program_asset.reset();
    }

    // Author the collision binding by REFERENCE (issue #216/#217): point the
    // node's Collision component at an authored asset-graph collision node, and
    // set whether the resolved surface constrains Motion actors. Creates the
    // Collision component if absent. The resolved key (collision_asset) is left
    // for bridge_scene_collision_keys to fill on (re)bind. Mirrors
    // attach_render_program_node.
    inline void attach_collision_asset_node(
        SceneNodeAsset& node,
        wz::asset::AssetGraphDraftNodeId node_id,
        bool constrain_movement)
    {
        if (!node.collision) {
            node.collision.emplace();
        }
        node.collision->collision_asset_node_id = node_id;
        node.collision->constrain_movement = constrain_movement;
        node.collision->collision_asset = {};
    }

    inline void detach_collision_asset_node(SceneNodeAsset& node)
    {
        if (!node.collision) {
            return;
        }
        node.collision->collision_asset_node_id.reset();
        node.collision->collision_asset = {};
    }

    // Scene-source reference (issue #213) — mirrors the renderable helpers.
    inline void attach_scene_source_node(
        SceneNodeAsset& node,
        wz::asset::AssetGraphDraftNodeId node_id,
        wz::asset::AssetKey scene_source = {})
    {
        node.scene_source_node_id = node_id;
        node.scene_source = scene_source;
    }

    inline void detach_scene_source(SceneNodeAsset& node)
    {
        node.scene_source_node_id.reset();
        node.scene_source.reset();
        node.glb_scene_source.reset();
    }

    // GLB scene-source descriptor (issue #213): attach the authored descriptor;
    // the resolved scene_source key is filled in at materialization. Clears the
    // asset-graph-node route so the two scene-source routes never both drive one
    // node (descriptor takes precedence by design, but keep the node single-route).
    inline void attach_glb_scene_source(
        SceneNodeAsset& node,
        SceneGLBSceneSource source = {})
    {
        node.scene_source_node_id.reset();
        node.scene_source.reset();
        node.glb_scene_source = std::move(source);
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

    // Author the frame's fog by REFERENCE: point the node's Atmosphere component
    // at an authored asset-graph Atmosphere node. Creates the component if absent.
    // The resolved key (atmosphere_asset) is left for
    // bridge_scene_atmosphere_keys to fill on (re)bind. Mirrors
    // attach_collision_asset_node.
    inline void attach_atmosphere_asset_node(
        SceneNodeAsset& node,
        wz::asset::AssetGraphDraftNodeId node_id)
    {
        if (!node.atmosphere) {
            node.atmosphere.emplace();
        }
        node.atmosphere->atmosphere_asset_node_id = node_id;
        node.atmosphere->atmosphere_asset = {};
    }

    inline void detach_atmosphere_asset_node(SceneNodeAsset& node)
    {
        if (!node.atmosphere) {
            return;
        }
        node.atmosphere->atmosphere_asset_node_id = 0;
        node.atmosphere->atmosphere_asset = {};
    }

    // Author the frame's environment by REFERENCE: point the node's Environment
    // component at an authored asset-graph FrameEnvironment node. Creates the
    // component if absent. The resolved key is left for
    // bridge_scene_environment_keys to fill on (re)bind. Mirrors
    // attach_atmosphere_asset_node.
    inline void attach_environment_asset_node(
        SceneNodeAsset& node,
        wz::asset::AssetGraphDraftNodeId node_id)
    {
        if (!node.environment) {
            node.environment.emplace();
        }
        node.environment->environment_asset_node_id = node_id;
        node.environment->environment_asset = {};
    }

    inline void detach_environment_asset_node(SceneNodeAsset& node)
    {
        if (!node.environment) {
            return;
        }
        node.environment->environment_asset_node_id = 0;
        node.environment->environment_asset = {};
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

    inline bool has_authored_renderable_component(
        const SceneNodeAsset& node) noexcept
    {
        // Includes the by-ingredients binding (issue #213): geometry (+ inherited
        // program) assembles a Renderable at load, so an ingredient-only node has a
        // renderable component just like the legacy / asset-graph-node forms.
        return node.renderable.has_value()
            || node.renderable_asset_node_id.has_value()
            || node.renderable_asset.has_value()
            || node.geometry_asset_node_id.has_value()
            || node.geometry_asset.has_value();
    }

    inline bool has_authored_camera_component(
        const SceneNodeAsset& node) noexcept
    {
        return node.camera.has_value();
    }

    // Scene-source: grafts a sub-scene as this node's children (issue #213) --
    // any of the asset-graph node ref, the resolved key, or the inline GLB
    // descriptor.
    inline bool has_authored_scene_source_component(
        const SceneNodeAsset& node) noexcept
    {
        return node.scene_source_node_id.has_value()
            || node.scene_source.has_value()
            || node.glb_scene_source.has_value();
    }

    // ─── The authored-component binding table ────────────────────────────────
    // ONE list that authored_components_for_node(),
    // summarize_authored_scene_components(), has_runtime_relevant_components()
    // and the editor's add/remove verbs are all DERIVED from, so they cannot
    // disagree about which field carries which component.
    //
    // Before this table those were five hand-maintained parallel structures, and
    // four components shipped into the tree missing one or more of them --
    // SceneSource, MotionFilter (f4afe8c8), FrameEnvironment and RenderToTexture
    // (f810c5b6), each caught only by the scheduled audit in issue #80. The
    // recurring failure was never a hard question; it was five places to
    // remember. Now there is one.
    //
    // The three CoreNode kinds (Transform/Visibility/MotionType) are absent by
    // design: they are unconditional properties of every node, not something
    // detected on one. The static_assert below accounts for them.
    //
    // WHAT THIS DOES NOT DO: force a NEW SceneNodeAsset field to appear here.
    // C++20 cannot enumerate members, so completeness in that direction is
    // guarded by the field-count tripwire in
    // tests/asset_scene/scene_ecs_boundary_tests.cpp -- adding a field breaks
    // that static_assert, which points back at this table.
    struct AuthoredComponentBinding
    {
        wz::scene::SceneAuthoredComponentKind kind;

        // Does this node carry the component?
        bool (*present)(const SceneNodeAsset&);

        // Which SceneAuthoredComponentSummary counter it feeds.
        uint32_t wz::scene::SceneAuthoredComponentSummary::* counter;

        // How much this node adds to that counter. nullptr means the usual "1 if
        // present". Behavior overrides it because its counter counts behavior
        // INSTANCES (node.behaviors is a vector), not nodes carrying the kind.
        uint32_t (*count)(const SceneNodeAsset&) = nullptr;

        // Token for the editor's generic add/remove component verbs, or nullptr
        // for components the editor does not author that way. A token REQUIRES
        // add/remove below. Renderable deliberately has none: the legacy
        // embedded slot is compat-only and the preferred asset-graph binding is
        // authored by set_node_renderable_asset().
        const char* editor_token = nullptr;
        void (*add)(SceneNodeAsset&) = nullptr;
        void (*remove)(SceneNodeAsset&) = nullptr;
    };

    // Short aliases for the table below, which would be unreadable with the
    // fully qualified names repeated 48 times.
    using SceneComponentKind = wz::scene::SceneAuthoredComponentKind;
    using SceneComponentCounts = wz::scene::SceneAuthoredComponentSummary;

    inline constexpr AuthoredComponentBinding kAuthoredComponentBindings[] = {
        { SceneComponentKind::ParentLink,
          [](const SceneNodeAsset& n) { return n.parent_id.has_value(); },
          &SceneComponentCounts::parent_links },
        // Renderable includes the by-ingredients binding (geometry + inherited
        // program, issue #213): a node authored purely by ingredients assembles
        // a Renderable at load, so it reports Kind::Renderable like the legacy
        // and asset-graph-node forms.
        { SceneComponentKind::Renderable,
          &has_authored_renderable_component,
          &SceneComponentCounts::renderables },
        { SceneComponentKind::AssetReference,
          [](const SceneNodeAsset& n) { return n.asset_reference.has_value(); },
          &SceneComponentCounts::asset_references },
        { SceneComponentKind::SceneImportSource,
          [](const SceneNodeAsset& n) {
              return n.scene_import_source.has_value();
          },
          &SceneComponentCounts::scene_import_sources },
        { SceneComponentKind::SceneSource,
          &has_authored_scene_source_component,
          &SceneComponentCounts::scene_sources },
        { SceneComponentKind::Camera,
          &has_authored_camera_component,
          &SceneComponentCounts::cameras,
          nullptr,
          "camera",
          [](SceneNodeAsset& n) { n.camera = SceneCameraAsset{}; },
          [](SceneNodeAsset& n) { n.camera.reset(); } },
        { SceneComponentKind::Light,
          [](const SceneNodeAsset& n) {
              return n.direct_light_source.has_value();
          },
          &SceneComponentCounts::lights },
        { SceneComponentKind::AmbientLighting,
          [](const SceneNodeAsset& n) { return n.ambient_lighting.has_value(); },
          &SceneComponentCounts::ambient_lighting },
        { SceneComponentKind::HDRIEnvironment,
          [](const SceneNodeAsset& n) {
              return n.hdri_environment.has_value();
          },
          &SceneComponentCounts::hdri_environments },
        { SceneComponentKind::Atmosphere,
          [](const SceneNodeAsset& n) { return n.atmosphere.has_value(); },
          &SceneComponentCounts::atmospheres,
          nullptr,
          "atmosphere",
          [](SceneNodeAsset& n) { n.atmosphere = SceneAtmosphereAsset{}; },
          [](SceneNodeAsset& n) { n.atmosphere.reset(); } },
        // The frame environment is atmosphere's successor, not a variant of it:
        // a node may carry either, and each reports its own kind.
        { SceneComponentKind::FrameEnvironment,
          [](const SceneNodeAsset& n) { return n.environment.has_value(); },
          &SceneComponentCounts::frame_environments,
          nullptr,
          "environment",
          [](SceneNodeAsset& n) { n.environment = SceneEnvironmentAsset{}; },
          [](SceneNodeAsset& n) { n.environment.reset(); } },
        { SceneComponentKind::SkyVisual,
          [](const SceneNodeAsset& n) { return n.sky_visual.has_value(); },
          &SceneComponentCounts::sky_visuals },
        { SceneComponentKind::SkySurface,
          [](const SceneNodeAsset& n) { return n.sky_surface.has_value(); },
          &SceneComponentCounts::sky_surfaces },
        { SceneComponentKind::RenderToTexture,
          [](const SceneNodeAsset& n) {
              return n.render_to_texture.has_value();
          },
          &SceneComponentCounts::render_to_texture_sources,
          nullptr,
          "render_to_texture",
          [](SceneNodeAsset& n) {
              n.render_to_texture = SceneRenderToTextureAsset{};
          },
          [](SceneNodeAsset& n) { n.render_to_texture.reset(); } },
        { SceneComponentKind::InputReceiver,
          [](const SceneNodeAsset& n) { return n.input_receiver.has_value(); },
          &SceneComponentCounts::input_receivers },
        { SceneComponentKind::FlyingCameraController,
          [](const SceneNodeAsset& n) {
              return n.flying_camera_controller.has_value();
          },
          &SceneComponentCounts::flying_camera_controllers },
        { SceneComponentKind::ActorMovementController,
          [](const SceneNodeAsset& n) {
              return n.actor_movement_controller.has_value();
          },
          &SceneComponentCounts::actor_movement_controllers },
        { SceneComponentKind::GroundBoundary,
          [](const SceneNodeAsset& n) { return n.ground_boundary.has_value(); },
          &SceneComponentCounts::ground_boundaries },
        { SceneComponentKind::MeshSource,
          [](const SceneNodeAsset& n) { return n.mesh_source.has_value(); },
          &SceneComponentCounts::mesh_sources },
        { SceneComponentKind::MeshDerivedFieldSource,
          [](const SceneNodeAsset& n) {
              return n.mesh_derived_field_source.has_value();
          },
          &SceneComponentCounts::mesh_derived_field_sources },
        { SceneComponentKind::MeshSparseOperatorSource,
          [](const SceneNodeAsset& n) {
              return n.mesh_sparse_operator_source.has_value();
          },
          &SceneComponentCounts::mesh_sparse_operator_sources },
        { SceneComponentKind::MeshSparseApplyField,
          [](const SceneNodeAsset& n) {
              return n.mesh_sparse_apply_field.has_value();
          },
          &SceneComponentCounts::mesh_sparse_apply_fields },
        { SceneComponentKind::MeshSparseDiffusionBands,
          [](const SceneNodeAsset& n) {
              return n.mesh_sparse_diffusion_bands.has_value();
          },
          &SceneComponentCounts::mesh_sparse_diffusion_bands },
        { SceneComponentKind::MeshLevelMaskSource,
          [](const SceneNodeAsset& n) {
              return n.mesh_level_mask_source.has_value();
          },
          &SceneComponentCounts::mesh_level_mask_sources },
        { SceneComponentKind::MeshWaveletAnalysis,
          [](const SceneNodeAsset& n) {
              return n.mesh_wavelet_analysis.has_value();
          },
          &SceneComponentCounts::mesh_wavelet_analyses },
        { SceneComponentKind::MeshComputeField,
          [](const SceneNodeAsset& n) {
              return n.mesh_compute_field.has_value();
          },
          &SceneComponentCounts::mesh_compute_fields },
        { SceneComponentKind::MeshRenderStyle,
          [](const SceneNodeAsset& n) {
              return n.mesh_render_style.has_value();
          },
          &SceneComponentCounts::mesh_render_styles },
        { SceneComponentKind::MeshMaskRenderStyle,
          [](const SceneNodeAsset& n) {
              return n.mesh_mask_render_style.has_value();
          },
          &SceneComponentCounts::mesh_mask_render_styles },
        { SceneComponentKind::MeshRegionSet,
          [](const SceneNodeAsset& n) { return n.mesh_region_set.has_value(); },
          &SceneComponentCounts::mesh_region_sets },
        { SceneComponentKind::ScalarFieldSource,
          [](const SceneNodeAsset& n) {
              return n.scalar_field_source.has_value();
          },
          &SceneComponentCounts::scalar_field_sources },
        { SceneComponentKind::VectorFieldSource,
          [](const SceneNodeAsset& n) {
              return n.vector_field_source.has_value();
          },
          &SceneComponentCounts::vector_field_sources },
        { SceneComponentKind::Collision,
          [](const SceneNodeAsset& n) { return n.collision.has_value(); },
          &SceneComponentCounts::collisions,
          nullptr,
          "collision",
          [](SceneNodeAsset& n) { n.collision = SceneCollisionAsset{}; },
          [](SceneNodeAsset& n) { n.collision.reset(); } },
        { SceneComponentKind::Terrain,
          [](const SceneNodeAsset& n) { return n.terrain.has_value(); },
          &SceneComponentCounts::terrains },
        { SceneComponentKind::TerrainRenderStyle,
          [](const SceneNodeAsset& n) {
              return n.terrain_render_style.has_value();
          },
          &SceneComponentCounts::terrain_render_styles },
        { SceneComponentKind::TerrainMeshSource,
          [](const SceneNodeAsset& n) {
              return n.terrain_mesh_source.has_value();
          },
          &SceneComponentCounts::terrain_mesh_sources },
        { SceneComponentKind::TerrainHeightFieldSource,
          [](const SceneNodeAsset& n) {
              return n.terrain_height_field_source.has_value();
          },
          &SceneComponentCounts::terrain_height_field_sources },
        { SceneComponentKind::AudioListener,
          [](const SceneNodeAsset& n) { return n.audio_listener.has_value(); },
          &SceneComponentCounts::audio_listeners,
          nullptr,
          "audio_listener",
          [](SceneNodeAsset& n) {
              n.audio_listener = SceneAudioListenerAsset{};
          },
          [](SceneNodeAsset& n) { n.audio_listener.reset(); } },
        { SceneComponentKind::AudioSource,
          [](const SceneNodeAsset& n) { return n.audio_source.has_value(); },
          &SceneComponentCounts::audio_sources,
          nullptr,
          "audio_source",
          [](SceneNodeAsset& n) { n.audio_source = SceneAudioSourceAsset{}; },
          [](SceneNodeAsset& n) { n.audio_source.reset(); } },
        { SceneComponentKind::EventListener,
          [](const SceneNodeAsset& n) { return n.event_listener.has_value(); },
          &SceneComponentCounts::event_listeners },
        { SceneComponentKind::EventTrigger,
          [](const SceneNodeAsset& n) { return n.event_trigger.has_value(); },
          &SceneComponentCounts::event_triggers },
        { SceneComponentKind::Proximity,
          [](const SceneNodeAsset& n) { return n.proximity.has_value(); },
          &SceneComponentCounts::proximities,
          nullptr,
          "proximity",
          [](SceneNodeAsset& n) { n.proximity = SceneProximityAsset{}; },
          [](SceneNodeAsset& n) { n.proximity.reset(); } },
        { SceneComponentKind::Motion,
          [](const SceneNodeAsset& n) { return n.motion.has_value(); },
          &SceneComponentCounts::motions,
          nullptr,
          "motion",
          [](SceneNodeAsset& n) { n.motion = SceneMotionAsset{}; },
          [](SceneNodeAsset& n) { n.motion.reset(); } },
        { SceneComponentKind::MotionFilter,
          [](const SceneNodeAsset& n) { return n.motion_filter.has_value(); },
          &SceneComponentCounts::motion_filters,
          nullptr,
          "motion_filter",
          [](SceneNodeAsset& n) { n.motion_filter = SceneMotionFilterAsset{}; },
          [](SceneNodeAsset& n) { n.motion_filter.reset(); } },
        // One kind, two authored spellings: the single legacy slot and the
        // behaviors vector. The counter counts INSTANCES, hence the override.
        { SceneComponentKind::Behavior,
          [](const SceneNodeAsset& n) {
              return n.behavior.has_value() || !n.behaviors.empty();
          },
          &SceneComponentCounts::behaviors,
          [](const SceneNodeAsset& n) {
              return static_cast<uint32_t>(
                  (n.behavior ? 1u : 0u) + n.behaviors.size());
          } },
        { SceneComponentKind::ComputeKernel,
          [](const SceneNodeAsset& n) { return n.compute_kernel.has_value(); },
          &SceneComponentCounts::compute_kernels },
        { SceneComponentKind::RenderShader,
          [](const SceneNodeAsset& n) { return n.render_shader.has_value(); },
          &SceneComponentCounts::render_shaders },
        // The authored field is debug_visual; the boundary kind is the broader
        // AuxiliaryVisual.
        { SceneComponentKind::AuxiliaryVisual,
          [](const SceneNodeAsset& n) { return n.debug_visual.has_value(); },
          &SceneComponentCounts::auxiliary_visuals },
        { SceneComponentKind::EditorHandle,
          [](const SceneNodeAsset& n) { return n.editor_handle.has_value(); },
          &SceneComponentCounts::editor_handles },
    };

    // Every kind except the three unconditional CoreNode ones is bound exactly
    // once. Adding a SceneAuthoredComponentKind without a binding fails here, as
    // does binding one twice.
    consteval bool authored_component_bindings_cover_every_kind()
    {
        for (std::size_t raw = 0;
             raw < wz::scene::kSceneAuthoredComponentKindCount;
             ++raw) {
            const auto kind = static_cast<SceneComponentKind>(raw);
            if (kind == SceneComponentKind::Transform
                || kind == SceneComponentKind::Visibility
                || kind == SceneComponentKind::MotionType) {
                continue;
            }
            std::size_t bound = 0;
            for (const AuthoredComponentBinding& b : kAuthoredComponentBindings) {
                if (b.kind == kind) {
                    ++bound;
                }
            }
            if (bound != 1) {
                return false;
            }
        }
        return true;
    }

    // A table entry either has all three editor fields or none of them: a token
    // the verbs cannot act on, or verbs no token can reach, is a dead entry.
    consteval bool authored_component_editor_verbs_are_complete()
    {
        for (const AuthoredComponentBinding& b : kAuthoredComponentBindings) {
            const bool has_token = b.editor_token != nullptr;
            if (has_token != (b.add != nullptr)
                || has_token != (b.remove != nullptr)) {
                return false;
            }
        }
        return true;
    }

    static_assert(
        std::size(kAuthoredComponentBindings) + 3
            == wz::scene::kSceneAuthoredComponentKindCount,
        "Every SceneAuthoredComponentKind except the three unconditional "
        "CoreNode kinds needs exactly one entry in kAuthoredComponentBindings.");
    static_assert(authored_component_bindings_cover_every_kind());
    static_assert(authored_component_editor_verbs_are_complete());

    // Find the binding for an editor add/remove token, or nullptr.
    inline constexpr const AuthoredComponentBinding*
    find_authored_component_binding(std::string_view editor_token) noexcept
    {
        for (const AuthoredComponentBinding& b : kAuthoredComponentBindings) {
            if (b.editor_token != nullptr && editor_token == b.editor_token) {
                return &b;
            }
        }
        return nullptr;
    }

    inline std::vector<wz::scene::SceneAuthoredComponentKind>
    authored_components_for_node(const SceneNodeAsset& node)
    {
        using Kind = wz::scene::SceneAuthoredComponentKind;

        // The three CoreNode kinds are unconditional: every node has a
        // transform, a visibility and a motion type.
        std::vector<Kind> out{
            Kind::Transform,
            Kind::Visibility,
            Kind::MotionType,
        };

        for (const AuthoredComponentBinding& binding :
             kAuthoredComponentBindings) {
            if (binding.present(node)) {
                out.push_back(binding.kind);
            }
        }

        return out;
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

    // Coarse, pass-aligned draw layers -- the semantic buckets render_order is
    // meant to hold. Guardrail (issue: RHI layer compositing): keep authored draw
    // order a SMALL NAMED set ("which layer/pass this belongs to"), NOT a per-node
    // z-nudge -- so a future RHI compositor routes each layer to a pass with no
    // rework, and so the single-sorted-forward-list model this replaces does not
    // get re-entrenched. The editor authors these (a coarse dropdown maps its
    // labels to them); the engine only stable-sorts by the stored int. Named after
    // the passes we expect. Spaced by 100 so a later pass can interleave
    // sub-orders without renumbering; negative Sky keeps the distant background
    // behind default-0 world geometry.
    namespace render_layer
    {
        inline constexpr int Sky         = -200;  // sky dome, stars -- distant, drawn first
        inline constexpr int World       =    0;  // lit opaque geometry (the default)
        inline constexpr int Transparent =  100;  // blended surfaces, after opaque
        inline constexpr int Overlay     =  200;  // gizmos / UI, on top
    }

    // Bake draw order into the flat node list: STABLE-sort by render_order so
    // lower layers draw first, ties keeping their existing array order. Stable is
    // load-bearing — it preserves authored order within a layer, and makes the
    // sort a no-op for an all-default-0 scene (byte-identical render + save). The
    // renderer stays a plain linear walk; this is the single choke point that
    // "produces the flat array order", called whenever the list is (re)built
    // (load after materialize, and after each live render_order edit). Transforms
    // and visibility are resolved by parent-id lookup, not array position, so
    // reordering here never disturbs the transform hierarchy.
    inline void sort_scene_nodes_by_render_order(
        std::vector<SceneNodeAsset>& nodes)
    {
        std::stable_sort(
            nodes.begin(),
            nodes.end(),
            [](const SceneNodeAsset& a, const SceneNodeAsset& b) {
                return a.render_order < b.render_order;
            });
    }

    // Reorder `nodes` into a stable PRE-ORDER flatten of the parent_id tree: each
    // node immediately precedes its whole subtree, sibling order = the order the
    // siblings currently appear in the array (IMPLICIT sibling order), and root
    // order = the current array order of the roots. This is what makes the tree
    // structure itself define draw order — rearranging the tree (reparent, or
    // reordering siblings) moves a node's draw slot, with no per-node ordering
    // key. Robust: a dangling/empty/self parent id is treated as a root, and a
    // parent cycle can't strand a node (any node the walk doesn't reach is
    // appended in array order), so every node appears exactly once.
    inline void flatten_scene_nodes_preorder(std::vector<SceneNodeAsset>& nodes)
    {
        const std::size_t n = nodes.size();
        if (n < 2u) {
            return;
        }

        std::unordered_map<std::string, std::size_t> index_by_id;
        index_by_id.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            index_by_id.emplace(nodes[i].id, i);
        }

        // Children per parent index + the roots, both in array order (so sibling
        // and root order come straight from the current array).
        std::vector<std::vector<std::size_t>> children(n);
        std::vector<std::size_t> roots;
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t parent = n;
            if (nodes[i].parent_id && !nodes[i].parent_id->empty()) {
                const auto it = index_by_id.find(*nodes[i].parent_id);
                if (it != index_by_id.end() && it->second != i) {
                    parent = it->second;
                }
            }
            if (parent == n) {
                roots.push_back(i);
            }
            else {
                children[parent].push_back(i);
            }
        }

        // Iterative pre-order DFS (deep trees can't overflow the stack). Push in
        // reverse so siblings/roots pop in array order; emit a node before its
        // children.
        std::vector<std::size_t> order;
        order.reserve(n);
        std::vector<std::uint8_t> visited(n, 0u);
        std::vector<std::size_t> stack;
        stack.reserve(n);
        for (std::size_t r = roots.size(); r-- > 0;) {
            stack.push_back(roots[r]);
        }
        while (!stack.empty()) {
            const std::size_t cur = stack.back();
            stack.pop_back();
            if (visited[cur]) {
                continue;
            }
            visited[cur] = 1u;
            order.push_back(cur);
            const std::vector<std::size_t>& kids = children[cur];
            for (std::size_t k = kids.size(); k-- > 0;) {
                if (!visited[kids[k]]) {
                    stack.push_back(kids[k]);
                }
            }
        }
        // Any node unreached by the DFS (stranded in a parent cycle) is kept, in
        // array order, so the flatten never drops nodes.
        for (std::size_t i = 0; i < n; ++i) {
            if (!visited[i]) {
                order.push_back(i);
            }
        }

        std::vector<SceneNodeAsset> reordered;
        reordered.reserve(n);
        for (const std::size_t idx : order) {
            reordered.push_back(std::move(nodes[idx]));
        }
        nodes = std::move(reordered);
    }

    // Bake the final draw order into `nodes` — the single choke point that
    // "produces the flat array order" the renderer walks. Draw order defaults to
    // the tree's pre-order (flatten_scene_nodes_preorder), then a stable sort by
    // render_order lets the coarse render_layer key OVERRIDE it (ties keep the
    // pre-order). With render_order 0 everywhere, draw order IS the tree order.
    // Idempotent: an already-baked array is left unchanged. Called wherever the
    // node list is (re)built or structurally edited (load, add-child, reparent,
    // reorder).
    inline void bake_scene_node_draw_order(std::vector<SceneNodeAsset>& nodes)
    {
        flatten_scene_nodes_preorder(nodes);
        sort_scene_nodes_by_render_order(nodes);
    }

    // Move node `id` to just BEFORE `before_id` in the flat node list, changing
    // ONLY its array position — i.e. its draw order. Nesting and transforms are
    // resolved by parent_id, never array index (see reparent_scene_node, which
    // leaves the array slot untouched), so this reorders draw order without
    // disturbing the hierarchy. An empty `before_id` moves the node to the END
    // (drawn last). Returns true iff the array actually changed (false if `id`
    // is missing, `before_id` is a non-empty id that isn't present, before_id ==
    // id, or the node is already in that position).
    //
    // This is the in-memory apply behind the editor's scene-tree reorder. The
    // caller re-applies sort_scene_nodes_by_render_order afterward so the coarse
    // render_layer key still dominates: a reorder is WITHIN a layer (ties keep
    // the new array order under the stable sort); moving a node ACROSS layers is
    // a render_order edit, not a reorder, and the stable re-sort snaps a cross-
    // layer drag back into its own layer.
    inline bool reorder_scene_node(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& id,
        const wz::scene::AuthoredEntityId& before_id)
    {
        if (id.empty() || before_id == id) {
            return false;
        }
        SceneNodeAsset* from = find_scene_node(nodes, id);
        if (!from) {
            return false;
        }
        std::size_t dest = nodes.size();  // empty before_id => move to the end
        if (!before_id.empty()) {
            const SceneNodeAsset* before = find_scene_node(nodes, before_id);
            if (!before) {
                return false;
            }
            dest = static_cast<std::size_t>(before - nodes.data());
        }
        const std::size_t src = static_cast<std::size_t>(from - nodes.data());
        // Already immediately before `before_id` (or already last): no change.
        if (dest == src || dest == src + 1u) {
            return false;
        }
        SceneNodeAsset moved = std::move(nodes[src]);
        nodes.erase(nodes.begin() + static_cast<std::ptrdiff_t>(src));
        if (dest > src) {
            --dest;  // erase shifted everything after src left by one
        }
        nodes.insert(
            nodes.begin() + static_cast<std::ptrdiff_t>(dest),
            std::move(moved));
        return true;
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

    // Set a node's render_order (draw-order layer key) in a flat node list.
    // Returns true iff a node with that id exists AND the value changed (so the
    // caller can skip a re-bake on a no-op). The in-memory apply behind the
    // editor's render-layer dropdown; the caller re-bakes draw order afterward.
    inline bool set_scene_node_render_order(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& id,
        int render_order)
    {
        SceneNodeAsset* node = find_scene_node(nodes, id);
        if (!node || node->render_order == render_order) {
            return false;
        }
        node->render_order = render_order;
        return true;
    }

    // Reparent a node in a flat node list: set its parent to new_parent_id
    // (empty => detach to top level). Rejects a missing node, a missing/self new
    // parent, or a new parent that is a descendant of the node (cycle). Returns
    // false on rejection. The in-memory apply behind drag-to-reparent.
    inline bool reparent_scene_node(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& id,
        const wz::scene::AuthoredEntityId& new_parent_id)
    {
        SceneNodeAsset* node = find_scene_node(nodes, id);
        if (!node) {
            return false;
        }
        if (new_parent_id.empty()) {
            node->parent_id.reset();
            return true;
        }
        if (new_parent_id == id || !find_scene_node(nodes, new_parent_id)) {
            return false;
        }
        // Reject if new_parent is a descendant of the node (would form a cycle):
        // walk up new_parent's ancestry; if the node's id appears, reject.
        wz::scene::AuthoredEntityId cursor = new_parent_id;
        while (const SceneNodeAsset* ancestor = find_scene_node(nodes, cursor)) {
            if (!ancestor->parent_id) {
                break;
            }
            if (*ancestor->parent_id == id) {
                return false;
            }
            cursor = *ancestor->parent_id;
        }
        node->parent_id = new_parent_id;
        return true;
    }

    // Remove the node `id` and its entire subtree from a flat node list. Returns
    // the removed ids (empty if `id` was not present). The in-memory apply behind
    // delete; SceneAssetData-level reference cleanup (active camera, terrain
    // refs) is not done here.
    inline std::vector<wz::scene::AuthoredEntityId> remove_scene_node(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& id)
    {
        std::vector<wz::scene::AuthoredEntityId> removed;
        if (!find_scene_node(nodes, id)) {
            return removed;
        }

        const auto in_removed =
            [&removed](const wz::scene::AuthoredEntityId& candidate) {
                for (const auto& r : removed) {
                    if (r == candidate) {
                        return true;
                    }
                }
                return false;
            };

        removed.push_back(id);
        // Transitively collect descendants (whose parent is already removed).
        bool grew = true;
        while (grew) {
            grew = false;
            for (const SceneNodeAsset& node : nodes) {
                if (node.parent_id
                    && in_removed(*node.parent_id)
                    && !in_removed(node.id))
                {
                    removed.push_back(node.id);
                    grew = true;
                }
            }
        }

        std::vector<SceneNodeAsset> kept;
        kept.reserve(nodes.size());
        for (SceneNodeAsset& node : nodes) {
            if (!in_removed(node.id)) {
                kept.push_back(std::move(node));
            }
        }
        nodes = std::move(kept);
        return removed;
    }

    // ─── Behavior-binding authoring (in-memory apply behind the host ABI) ──
    // A node carries an optional singular `behavior` plus a plural `behaviors`
    // vector; each binding has a stable id. These helpers are the flat-node-list
    // apply behind the editor/host's live behavior authoring verbs (add/remove/
    // edit a binding). They mutate scene_nodes_ only; materializing the change
    // (rebuild_behavior_scene) and persistence are separate paths.

    // True if `id` is already used by any binding on this node (singular or
    // plural). Used to dedupe a freshly minted binding id.
    inline bool node_has_behavior_binding_id(
        const SceneNodeAsset& node,
        std::string_view id) noexcept
    {
        if (node.behavior && node.behavior->id == id) {
            return true;
        }
        for (const SceneBehaviorAsset& binding : node.behaviors) {
            if (binding.id == id) {
                return true;
            }
        }
        return false;
    }

    // Find a behavior binding on a node by id, across the singular `behavior`
    // and the plural `behaviors`. Null if no binding has that id.
    inline SceneBehaviorAsset* find_behavior_binding(
        SceneNodeAsset& node,
        std::string_view binding_id) noexcept
    {
        if (node.behavior && node.behavior->id == binding_id) {
            return &*node.behavior;
        }
        for (SceneBehaviorAsset& binding : node.behaviors) {
            if (binding.id == binding_id) {
                return &binding;
            }
        }
        return nullptr;
    }

    inline const SceneBehaviorAsset* find_behavior_binding(
        const SceneNodeAsset& node,
        std::string_view binding_id) noexcept
    {
        if (node.behavior && node.behavior->id == binding_id) {
            return &*node.behavior;
        }
        for (const SceneBehaviorAsset& binding : node.behaviors) {
            if (binding.id == binding_id) {
                return &binding;
            }
        }
        return nullptr;
    }

    // Mint a stable behavior-binding id for a node: an auto-slug from the node
    // id ("<node_id>.behavior.<n>"), with <n> the lowest counter that does not
    // collide with an existing binding id on the node. Mirrors mint_scene_node_id
    // (a deduped counter slug) but scoped to one node's bindings.
    inline std::string mint_behavior_binding_id(const SceneNodeAsset& node)
    {
        const std::string prefix = node.id + ".behavior.";
        for (uint64_t n = 1u;; ++n) {
            std::string candidate = prefix + std::to_string(n);
            if (!node_has_behavior_binding_id(node, candidate)) {
                return candidate;
            }
        }
    }

    struct SceneAddBehaviorResult
    {
        bool ok = false;
        std::string binding_id;
        std::string error;
    };

    // Append a behavior binding (the given module, a minted stable id, default
    // enabled, no events/config) to node `node_id` in a flat node list. The node
    // must exist. Returns {ok, binding_id, error}. The in-memory apply behind the
    // host's "add behavior" verb; materialization + persistence are separate.
    inline SceneAddBehaviorResult add_node_behavior(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string module)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return SceneAddBehaviorResult{
                .ok = false,
                .binding_id = {},
                .error = "node does not exist",
            };
        }

        SceneBehaviorAsset binding;
        binding.id = mint_behavior_binding_id(*node);
        binding.module = std::move(module);
        binding.enabled = true;
        const std::string minted = binding.id;
        node->behaviors.push_back(std::move(binding));
        return SceneAddBehaviorResult{
            .ok = true, .binding_id = minted, .error = {} };
    }

    // Remove the behavior binding `binding_id` from node `node_id` (matching the
    // singular `behavior` or an entry of `behaviors`). Returns false if no node
    // or binding matched.
    inline bool remove_node_behavior(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view binding_id)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        if (node->behavior && node->behavior->id == binding_id) {
            node->behavior.reset();
            return true;
        }
        const auto it = std::find_if(
            node->behaviors.begin(),
            node->behaviors.end(),
            [binding_id](const SceneBehaviorAsset& b) {
                return b.id == binding_id;
            });
        if (it == node->behaviors.end()) {
            return false;
        }
        node->behaviors.erase(it);
        return true;
    }

    // ─── Optional component add/remove (the editor's "add/remove component") ──
    // A small, closed set of optional node components the editor can add and
    // remove generically by a kind token: "camera", "renderable", "proximity",
    // "collision", "motion". Each maps to one std::optional<...> slot on
    // SceneNodeAsset. "add" default-constructs the slot (the struct's in-class
    // member initializers are the sensible defaults — same values the attach_*
    // helpers and the scene compiler use); "remove" resets it to nullopt. These
    // are the in-memory apply behind the host ABI's component verbs;
    // materialization/persistence are separate paths.
    //
    // These verbs deliberately do NOT cover the renderable component. The legacy
    // embedded SceneRenderableBinding slot (node.renderable) is the debug/compat
    // path and must not gain new authoring features. The PREFERRED asset-graph-
    // backed renderable (renderable_asset_node_id) is authored by the dedicated
    // set_node_renderable_asset() helper below, not these generic verbs.

    // All four verbs below are DERIVED from kAuthoredComponentBindings, so the
    // editor's token vocabulary cannot drift from the ECS boundary vocabulary
    // the way it used to: `environment` was an editor token with no
    // SceneAuthoredComponentKind, and `render_to_texture` was a Kind with no
    // token. A component is editor-authorable exactly when its table entry
    // carries an editor_token.

    // True if `kind` names one of the editor-managed optional components.
    inline bool is_optional_component_kind(std::string_view kind) noexcept
    {
        return find_authored_component_binding(kind) != nullptr;
    }

    // True if node `node_id` currently carries the optional component `kind`.
    // False if the node is missing or the kind is unknown. Lets a caller (e.g. a
    // test or the editor) observe component presence without parsing JSON.
    inline bool node_has_optional_component(
        const std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view kind) noexcept
    {
        const SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        const AuthoredComponentBinding* binding =
            find_authored_component_binding(kind);
        return binding != nullptr && binding->present(*node);
    }

    // Add the optional component `kind` to node `node_id`, default-constructing
    // its slot (sensible defaults from the struct's member initializers). Adding
    // a component the node already has overwrites it with a fresh default, which
    // is harmless and keeps the verb idempotent. Returns false if the node is
    // missing or `kind` is not an editor-managed kind (fail closed).
    inline bool add_node_optional_component(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view kind)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        const AuthoredComponentBinding* binding =
            find_authored_component_binding(kind);
        if (!binding) {
            return false;
        }
        binding->add(*node);
        return true;
    }

    // Remove the optional component `kind` from node `node_id` (reset its slot to
    // nullopt). Returns false if the node is missing or `kind` is not managed.
    // Resetting an absent component still returns true (the post-state is the
    // requested one -- the component is gone), keeping the verb idempotent.
    inline bool remove_node_optional_component(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view kind)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        const AuthoredComponentBinding* binding =
            find_authored_component_binding(kind);
        if (!binding) {
            return false;
        }
        binding->remove(*node);
        return true;
    }

    // Author the PREFERRED asset-graph-backed Renderable component on node
    // `node_id`: point renderable_asset_node_id at the authored asset-graph node
    // `asset_graph_node_id`, or clear it (reset to nullopt) when the id is 0.
    // Either way the resolved renderable_asset key is reset to nullopt so it
    // re-resolves from the (new or absent) node id. The legacy embedded
    // node.renderable slot is intentionally left untouched. Returns false if the
    // node is missing (fail closed); clearing an already-empty slot returns true
    // (the requested post-state is reached), keeping the verb idempotent.
    inline bool set_node_renderable_asset(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        if (asset_graph_node_id != 0) {
            node->renderable_asset_node_id = asset_graph_node_id;
        }
        else {
            node->renderable_asset_node_id.reset();
        }
        // Drop the cached resolved key so it re-resolves from the node id.
        node->renderable_asset.reset();
        return true;
    }

    // Author an AudioSource's renderable reference by STABLE asset-graph node id
    // (the editor picker target), mirroring set_node_renderable_asset. A non-zero
    // id points audio_renderable_node_id at the authored audio-renderable node and
    // creates the AudioSource component if absent (so picking a renderable is
    // enough to author the source); id 0 clears the reference but leaves the
    // component in place. Either way the resolved key is dropped so it re-resolves
    // from the node id at bind. Returns false only if the node is missing.
    inline bool set_node_audio_renderable(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        if (asset_graph_node_id != 0) {
            if (!node->audio_source) {
                node->audio_source = SceneAudioSourceAsset{};
            }
            node->audio_source->audio_renderable_node_id = asset_graph_node_id;
            node->audio_source->audio_renderable = {};
        }
        else if (node->audio_source) {
            node->audio_source->audio_renderable_node_id.reset();
            node->audio_source->audio_renderable = {};
        }
        return true;
    }

    // Set an AudioSource's per-entity play policy. No-op (false) if the node is
    // missing or has no AudioSource component (the editor adds it first).
    inline bool set_node_audio_source_play(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        bool auto_play,
        bool enabled)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node || !node->audio_source) {
            return false;
        }
        node->audio_source->auto_play = auto_play;
        node->audio_source->enabled = enabled;
        return true;
    }

    // Author the PREFERRED asset-graph-backed Scene-source component on node
    // `node_id` (issue #213): point scene_source_node_id at the authored
    // asset-graph node `asset_graph_node_id` (a "Scene from GLB" node), or clear
    // it (reset to nullopt) when the id is 0. Either way the resolved
    // scene_source key is reset to nullopt so it re-resolves from the (new or
    // absent) node id. Mirrors set_node_renderable_asset: returns false if the
    // node is missing (fail closed); clearing an already-empty slot returns true
    // (the requested post-state is reached), keeping the verb idempotent.
    inline bool set_node_scene_source(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        wz::asset::AssetGraphDraftNodeId asset_graph_node_id)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        if (asset_graph_node_id != 0) {
            node->scene_source_node_id = asset_graph_node_id;
        }
        else {
            node->scene_source_node_id.reset();
        }
        // Drop the cached resolved key so it re-resolves from the node id.
        node->scene_source.reset();
        return true;
    }

    // Set a behavior binding's enabled flag. False if no node/binding matched.
    inline bool set_node_behavior_enabled(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view binding_id,
        bool enabled)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        SceneBehaviorAsset* binding = find_behavior_binding(*node, binding_id);
        if (!binding) {
            return false;
        }
        binding->enabled = enabled;
        return true;
    }

    // Set a behavior binding's label + module. False if no node/binding matched.
    inline bool set_node_behavior_fields(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view binding_id,
        std::string label,
        std::string module)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        SceneBehaviorAsset* binding = find_behavior_binding(*node, binding_id);
        if (!binding) {
            return false;
        }
        binding->label = std::move(label);
        binding->module = std::move(module);
        return true;
    }

    // Replace a behavior binding's events list. False if no node/binding matched.
    inline bool set_node_behavior_events(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view binding_id,
        std::vector<std::string> events)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        SceneBehaviorAsset* binding = find_behavior_binding(*node, binding_id);
        if (!binding) {
            return false;
        }
        binding->events = std::move(events);
        return true;
    }

    // Set/replace one config entry (by key) on a behavior binding. False if no
    // node/binding matched. An existing entry with the same key is overwritten.
    inline bool set_node_behavior_config(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view binding_id,
        SceneBehaviorConfigValue value)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        SceneBehaviorAsset* binding = find_behavior_binding(*node, binding_id);
        if (!binding) {
            return false;
        }
        for (SceneBehaviorConfigValue& entry : binding->config) {
            if (entry.key == value.key) {
                entry = std::move(value);
                return true;
            }
        }
        binding->config.push_back(std::move(value));
        return true;
    }

    // Remove one config entry (by key) from a behavior binding. False if no
    // node/binding/key matched.
    inline bool clear_node_behavior_config(
        std::vector<SceneNodeAsset>& nodes,
        const wz::scene::AuthoredEntityId& node_id,
        std::string_view binding_id,
        std::string_view key)
    {
        SceneNodeAsset* node = find_scene_node(nodes, node_id);
        if (!node) {
            return false;
        }
        SceneBehaviorAsset* binding = find_behavior_binding(*node, binding_id);
        if (!binding) {
            return false;
        }
        const auto it = std::find_if(
            binding->config.begin(),
            binding->config.end(),
            [key](const SceneBehaviorConfigValue& c) { return c.key == key; });
        if (it == binding->config.end()) {
            return false;
        }
        binding->config.erase(it);
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

    // Derived from the binding table and the DOMAIN classification rather than a
    // hand-listed field set: a component is runtime-relevant because of how its
    // kind is classified, not because someone remembered to add it here. Adding
    // a RuntimeRelevant or Exportable kind to the table is enough.
    inline bool has_runtime_relevant_components(
        const SceneNodeAsset& node) noexcept
    {
        for (const AuthoredComponentBinding& binding :
             kAuthoredComponentBindings) {
            if (wz::scene::is_runtime_relevant_component(binding.kind)
                && binding.present(node)) {
                return true;
            }
        }
        return false;
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
        // The three CoreNode kinds hold for every node unconditionally, so they
        // are node counts rather than detections.
        out.nodes = static_cast<uint32_t>(scene.nodes.size());
        out.transforms = out.nodes;
        out.visibility = out.nodes;
        out.motion_types = out.nodes;
        // Scene-level lights are counted before the per-node pass adds each
        // node's own direct light source to the same counter.
        out.lights = static_cast<uint32_t>(scene.lights.size());

        for (const auto& node : scene.nodes) {
            for (const AuthoredComponentBinding& binding :
                 kAuthoredComponentBindings) {
                const uint32_t added = binding.count
                    ? binding.count(node)
                    : (binding.present(node) ? 1u : 0u);
                out.*(binding.counter) += added;
            }
        }

        return out;
    }

} // namespace wz::engine::assets
