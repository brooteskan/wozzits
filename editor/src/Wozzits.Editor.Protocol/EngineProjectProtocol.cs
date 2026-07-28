namespace Wozzits.Editor.Protocol;

public enum EngineProjectStatus : uint
{
    Missing = 0,
    Invalid = 1,
    Valid = 2,
}

public sealed record EngineProjectResponse
{
    public bool Ok { get; init; }

    public EngineProjectStatus Status { get; init; } = EngineProjectStatus.Invalid;

    public bool Created { get; init; }

    public string Error { get; init; } = string.Empty;

    public bool IsValid => Ok && Status == EngineProjectStatus.Valid;

    public bool IsMissing => Status == EngineProjectStatus.Missing;
}

public sealed record EngineMutationResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;
}

[Flags]
public enum EngineAssetGraphPortFlags : uint
{
    None = 0,
    Required = 1u << 0,
    Many = 1u << 1,
}

public enum EngineAssetGraphConnectionStatus : uint
{
    Compatible = 0,
    MissingNode = 1,
    MissingCompiler = 2,
    InvalidInputPort = 3,
    TypeMismatch = 4,
    SelfDependency = 5,
    Cycle = 6,
    DuplicateInputPort = 7,
}

public sealed record EngineAssetGraphConnectionCheckResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    public EngineAssetGraphConnectionCheck Check { get; init; } = new();
}

public sealed record EngineAssetGraphConnectionCheck
{
    public bool Compatible { get; init; }

    public EngineAssetGraphConnectionStatus Status { get; init; }

    public bool ReplacesExisting { get; init; }

    public ulong From { get; init; }

    public ulong To { get; init; }

    public uint ToInputPort { get; init; }

    public uint FromType { get; init; }

    public uint ToType { get; init; }

    public string Message { get; init; } = string.Empty;

    public string FromTypeName { get; init; } = string.Empty;

    public string ToTypeName { get; init; } = string.Empty;
}

public sealed record EngineSceneTransformEdit
{
    public string TranslationX { get; init; } = string.Empty;

    public string TranslationY { get; init; } = string.Empty;

    public string TranslationZ { get; init; } = string.Empty;

    public string RotationX { get; init; } = string.Empty;

    public string RotationY { get; init; } = string.Empty;

    public string RotationZ { get; init; } = string.Empty;

    public string ScaleX { get; init; } = string.Empty;

    public string ScaleY { get; init; } = string.Empty;

    public string ScaleZ { get; init; } = string.Empty;
}

public sealed record EngineSceneCameraEdit
{
    public string FieldOfViewY { get; init; } = string.Empty;

    public string NearPlane { get; init; } = string.Empty;

    public string FarPlane { get; init; } = string.Empty;

    public string Aspect { get; init; } = string.Empty;
}

public sealed record EngineAssetGraphSnapshotResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    public EngineAssetGraphSnapshot Snapshot { get; init; } = new();
}

public sealed record EngineAssetGraphSnapshot
{
    public string Schema { get; init; } = string.Empty;

    public double Zoom { get; init; } = 1.0;

    public List<EngineAssetGraphNode> Nodes { get; init; } = [];

    public List<EngineAssetGraphEdge> Edges { get; init; } = [];
}

public sealed record EngineAssetGraphNode
{
    public ulong Id { get; init; }

    public int Type { get; init; }

    public string TypeName { get; init; } = string.Empty;

    public string Schema { get; init; } = string.Empty;

    public string DisplayName { get; init; } = string.Empty;

    public string CompileStatus { get; init; } = string.Empty;

    public double X { get; init; }

    public double Y { get; init; }

    public List<EngineAssetGraphPort> InputPorts { get; init; } = [];

    public List<EngineAssetGraphPort> OutputPorts { get; init; } = [];

    public List<EngineAssetGraphDiagnostic> Diagnostics { get; init; } = [];

    public List<EngineAssetGraphParam> Params { get; init; } = [];
}

public sealed record EngineAddNodeResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    public ulong NodeId { get; init; }
}

public sealed record EngineAddSceneNodeResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    // The id the engine minted for the new node (a counter, as a string).
    public string NodeId { get; init; } = string.Empty;
}

public sealed record EngineCreateSceneletResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    // Resource-relative path of the scenelet the engine minted, in the form the
    // scenelet catalog reports and OpenScene accepts. The engine decides where
    // scenelets live and what a fresh one contains, so the editor neither builds
    // this path nor authors the document (issue #271).
    public string Path { get; init; } = string.Empty;
}

public sealed record EngineAssetCatalogResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    public List<EngineAssetCatalogEntry> Entries { get; init; } = [];
}

public sealed record EngineAssetCatalogEntry
{
    public int Type { get; init; }

    public string TypeName { get; init; } = string.Empty;

    public string Category { get; init; } = string.Empty;

    public List<EngineAssetCatalogSchema> Schemas { get; init; } = [];
}

public sealed record EngineAssetCatalogSchema
{
    public ulong Schema { get; init; }

    public string Label { get; init; } = string.Empty;
}

public sealed record EngineActuatorCatalogResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    public List<EngineActuator> Actuators { get; init; } = [];
}

public sealed record EngineActuator
{
    public string Name { get; init; } = string.Empty;

    public string Label { get; init; } = string.Empty;

    public List<EngineActuatorParam> Params { get; init; } = [];
}

public sealed record EngineActuatorParam
{
    public string Name { get; init; } = string.Empty;

    // WzActuatorParamKind: 0 = scalar (const|op), 1 = binding, 2 = agent.
    public int Kind { get; init; }

    public double DefaultValue { get; init; }
}

public sealed record EngineBehaviorModuleCatalogResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    public List<EngineBehaviorModule> Modules { get; init; } = [];
}

public sealed record EngineBehaviorModule
{
    public string Module { get; init; } = string.Empty;

    public List<EngineBehaviorModuleParam> Params { get; init; } = [];
}

public sealed record EngineBehaviorModuleParam
{
    // The config key the module reads (what the inspector writes back).
    public string Key { get; init; } = string.Empty;

    public string Label { get; init; } = string.Empty;

    // WzBehaviorParamType: 1 = float, 2 = bool, 3 = string.
    public int Type { get; init; }

    // FLOAT/BOOL authoring default (BOOL: 0 or 1).
    public double DefaultNumber { get; init; }

    public string DefaultString { get; init; } = string.Empty;
}

public sealed record EngineAssetGraphParam
{
    public string Name { get; init; } = string.Empty;

    // Declared widget type: "bool" | "int" | "float" | "float3" | "color" |
    // "string" | "filepath" | "enum".
    public string Kind { get; init; } = string.Empty;

    // Display text; for "enum", the selected option name.
    public string Value { get; init; } = string.Empty;

    // Enum choices ("enum" kind only).
    public List<string> Options { get; init; } = [];
}

public sealed record EngineAssetGraphPort
{
    public uint Index { get; init; }

    public uint Type { get; init; }

    public EngineAssetGraphPortFlags Flags { get; init; }

    public string Name { get; init; } = string.Empty;

    public string Label { get; init; } = string.Empty;

    public string TypeName { get; init; } = string.Empty;
}

public sealed record EngineAssetGraphDiagnostic
{
    public uint Severity { get; init; }

    public uint Code { get; init; }

    public string SeverityName { get; init; } = string.Empty;

    public string CodeName { get; init; } = string.Empty;

    public ulong Node { get; init; }

    public ulong Edge { get; init; }

    public uint InputPort { get; init; }

    public string Message { get; init; } = string.Empty;
}

public sealed record EngineAssetGraphEdge
{
    public ulong Id { get; init; }

    public ulong From { get; init; }

    public ulong To { get; init; }

    public uint ToInputPort { get; init; }
}

public sealed record EngineProjectSnapshotResponse
{
    public bool Ok { get; init; }

    public EngineProjectStatus Status { get; init; } = EngineProjectStatus.Invalid;

    public string Error { get; init; } = string.Empty;

    public string ProjectName { get; init; } = string.Empty;

    public EngineAssetGraphSnapshotResponse AssetGraph { get; init; } = new();

    public EngineSceneSnapshotResponse Scene { get; init; } = new();

    public bool IsValid => Ok && Status == EngineProjectStatus.Valid;

    public bool IsMissing => Status == EngineProjectStatus.Missing;
}

public sealed record EngineSceneSnapshotResponse
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    public EngineSceneSnapshot Snapshot { get; init; } = new();
}

public sealed record EngineSceneSnapshot
{
    public string Schema { get; init; } = string.Empty;

    public string Name { get; init; } = string.Empty;

    public List<EngineSceneNode> Roots { get; init; } = [];
}

public sealed record EngineSceneNode
{
    public string Id { get; init; } = string.Empty;

    public string DisplayName { get; init; } = string.Empty;

    public string? ParentId { get; init; }

    public string Kind { get; init; } = string.Empty;

    public bool? Visible { get; init; }

    // Draw-order layer key (render_layer): -200 Sky / 0 World / 100 Transparent /
    // 200 Overlay. Default 0. Surfaced so the inspector's layer dropdown shows the
    // node's current layer.
    public int RenderOrder { get; init; }

    public EngineSceneRenderableSource RenderableSource { get; init; } = new();

    public EngineSceneTransform? Transform { get; init; }

    public EngineSceneCamera? Camera { get; init; }

    public EngineSceneRenderable? Renderable { get; init; }

    public List<EngineSceneComponent> Components { get; init; } = [];

    public List<EngineSceneBehavior> Behaviors { get; init; } = [];

    public EngineSceneNodeSceneSource? SceneSource { get; init; }

    // Authored render-binding refs (issue #213), null when the node has none: the
    // "Subtree from asset" node ref and the render-binding geometry + render-program
    // asset-graph node ids. The inspector reveals + pre-selects these sections from
    // these, so live edits persist on reselect.
    public ulong? SceneSourceNodeId { get; init; }

    public ulong? GeometryNodeId { get; init; }

    public ulong? RenderProgramNodeId { get; init; }

    // Persisted Collision/Motion component field values (read-back gap fix), null
    // when the node has no such component. The inspector restores these fields on
    // select + after reload instead of resetting to defaults.
    public EngineSceneNodeCollision? Collision { get; init; }

    public EngineSceneNodeMotion? Motion { get; init; }

    // Motion Filter component field values (read-back), null when the node has no
    // Motion Filter. The inspector restores its per-DOF fields on select + reload.
    public EngineSceneNodeMotionFilter? MotionFilter { get; init; }

    // Persisted AudioSource component field values (read-back), null when the node
    // has no AudioSource. The inspector restores the renderable reference + play
    // policy on select + after reload.
    public EngineSceneNodeAudioSource? AudioSource { get; init; }

    // The node's Atmosphere component (which Atmosphere asset the frame's fog
    // reads + enabled), or null when the node has none. The inspector restores the
    // reference + enabled on select + after reload.
    public EngineSceneNodeAtmosphere? Atmosphere { get; init; }

    // The node's FrameEnvironment component (which FrameEnvironment asset the frame
    // reads + enabled), or null when the node has none. The inspector restores the
    // reference + enabled on select + after reload.
    public EngineSceneNodeEnvironment? Environment { get; init; }

    // Custom-renderable ingredients (issue #229/#230), empty when the node has
    // none: the authored semantic bindings + per-instance constant overrides.
    // The inspector pre-fills its binding rows + constant editors from these.
    public List<EngineSceneRenderableBinding> RenderableBindings { get; init; } = [];

    public List<EngineSceneRenderableConstant> RenderableConstants { get; init; } = [];

    public List<EngineSceneNode> Children { get; init; } = [];
}

// One authored semantic resource binding of a node's custom-renderable
// ingredients (issue #229/#230). AssetGraphNodeId is the authored source
// anchor (null = the row has a semantic but no wired source yet); the
// resolved key re-bridges on bind and is never surfaced.
public sealed record EngineSceneRenderableBinding
{
    public string Semantic { get; init; } = string.Empty;

    public ulong? AssetGraphNodeId { get; init; }
}

// One per-instance constant override of a node's custom-renderable ingredients
// (issue #229/#230): a value for one of the program layout's declared constant
// tail fields. A field narrower than 4 floats consumes a prefix of Value.
public sealed record EngineSceneRenderableConstant
{
    public string Name { get; init; } = string.Empty;

    public float[] Value { get; init; } = new float[4];
}

// Authored Collision-component field values surfaced read-back (read-back gap
// fix). CollisionAssetNodeId is the authored asset-graph collision node id (null
// when none).
public sealed record EngineSceneNodeCollision
{
    public ulong? CollisionAssetNodeId { get; init; }

    public bool ConstrainMovement { get; init; }
}

// Authored Motion-component field values surfaced read-back (read-back gap fix):
// the terrain-stick subset.
public sealed record EngineSceneNodeMotion
{
    public bool TerrainConstrained { get; init; }

    public float RideHeight { get; init; }

    public float FootprintRadius { get; init; }

    public bool AlignToSurface { get; init; }

    public float AlignmentStrength { get; init; }
}

// One local-axis rotation channel (roll/pitch/yaw) of a node's Motion Filter.
// SmoothingTime 0 = instant; Level eases the axis toward world-level instead of
// the target; Limit clamps the resulting angle to [LimitMinDegrees, LimitMax-
// Degrees].
public sealed record EngineSceneNodeMotionFilterRotationAxis
{
    public float SmoothingTime { get; init; }

    public bool Level { get; init; }

    public bool Limit { get; init; }

    public float LimitMinDegrees { get; init; }

    public float LimitMaxDegrees { get; init; }
}

// Motion Filter component field values surfaced read-back: per-DOF smoothing +
// clamp of a node's driven transform (secondary-motion camera damping).
// TranslationSmoothing is world X/Y/Z (0 = pass through); TerrainFloor clamps
// world Y to >= terrain + offset; roll/pitch/yaw are node-local channels.
public sealed record EngineSceneNodeMotionFilter
{
    public float[] TranslationSmoothing { get; init; } = new float[3];

    public bool TerrainFloor { get; init; }

    public float TerrainFloorOffset { get; init; }

    public EngineSceneNodeMotionFilterRotationAxis Roll { get; init; } = new();

    public EngineSceneNodeMotionFilterRotationAxis Pitch { get; init; } = new();

    public EngineSceneNodeMotionFilterRotationAxis Yaw { get; init; } = new();

    public bool Enabled { get; init; } = true;
}

// Authored AudioSource-component field values surfaced read-back.
// AudioRenderableNodeId is the authored audio-renderable asset-graph node id
// (null when none); AutoPlay/Enabled are the per-entity play policy.
public sealed record EngineSceneNodeAudioSource
{
    public ulong? AudioRenderableNodeId { get; init; }

    public bool AutoPlay { get; init; } = true;

    public bool Enabled { get; init; } = true;
}

// AtmosphereAssetNodeId is the authored Atmosphere asset-graph node id the frame's
// fog reads (null when none); Enabled is the master switch for this binding.
public sealed record EngineSceneNodeAtmosphere
{
    public ulong? AtmosphereAssetNodeId { get; init; }

    public bool Enabled { get; init; } = true;
}

// EnvironmentAssetNodeId is the authored FrameEnvironment asset-graph node id the
// frame's environment reads (null when none); Enabled is this binding's switch.
public sealed record EngineSceneNodeEnvironment
{
    public ulong? EnvironmentAssetNodeId { get; init; }

    public bool Enabled { get; init; } = true;
}

public sealed record EngineSceneTransform
{
    public List<double> Translation { get; init; } = [];

    public List<double> RotationQuaternion { get; init; } = [];

    public List<double> RotationEulerDegrees { get; init; } = [];

    public List<double> Scale { get; init; } = [];

    public EngineSceneTransformDisplay Display { get; init; } = new();
}

public sealed record EngineSceneTransformDisplay
{
    public string TranslationX { get; init; } = string.Empty;

    public string TranslationY { get; init; } = string.Empty;

    public string TranslationZ { get; init; } = string.Empty;

    public string RotationX { get; init; } = string.Empty;

    public string RotationY { get; init; } = string.Empty;

    public string RotationZ { get; init; } = string.Empty;

    public string ScaleX { get; init; } = string.Empty;

    public string ScaleY { get; init; } = string.Empty;

    public string ScaleZ { get; init; } = string.Empty;
}

public sealed record EngineSceneCamera
{
    public double? FieldOfViewY { get; init; }

    public double? NearPlane { get; init; }

    public double? FarPlane { get; init; }

    public double? Aspect { get; init; }

    // Canonical defaults for a freshly added Camera component. They mirror the
    // engine's SceneCameraAsset (and the runtime free-fly camera): ~60° vertical
    // FOV, 0.1 near, 1000 far, 16:9. Seeding these instead of leaving the fields
    // null keeps the inspector showing the engine's real defaults rather than
    // blanks that would otherwise apply back as a degenerate zero-FOV camera
    // (#220).
    public static EngineSceneCamera CreateEngineDefaults() => new()
    {
        FieldOfViewY = 1.0472,
        NearPlane = 0.1,
        FarPlane = 1000.0,
        Aspect = 16.0 / 9.0,
    };
}

public sealed record EngineSceneRenderable
{
    public ulong? AssetGraphNodeId { get; init; }
}

public sealed record EngineSceneRenderableSource
{
    public string Kind { get; init; } = string.Empty;

    public string DisplayName { get; init; } = string.Empty;
}

// The high-impact subset of a MeshRenderStyleData read back for a GLB component
// (issue #213 Phase 3b-2): surface + wireframe enabled flags and RGBA colors.
// Rgba is a 4-float array [r, g, b, a]. The editor pre-fills its style editor
// from this and re-authors the same subset via the style verb.
public sealed record EngineGlbStyle
{
    public bool SurfaceEnabled { get; init; }

    public float[] SurfaceRgba { get; init; } = [0f, 0f, 0f, 0f];

    public bool WireframeEnabled { get; init; }

    public float[] WireframeRgba { get; init; } = [0f, 0f, 0f, 0f];
}

// One per-mesh-index style override read back from a GLB scene-source
// descriptor (issue #213 Phase 3b-2). MeshIndex matches EngineGlbComponent.MeshIndex.
public sealed record EngineGlbStyleOverride
{
    public uint MeshIndex { get; init; }

    public EngineGlbStyle Style { get; init; } = new();
}

// Read-only summary of a node's GLB scene-source descriptor (issue #213),
// present only when the node carries a glb_scene_source block. Carries the
// summary fields plus the editor-authorable style subset (Phase 3b-2): the base
// style (when HasBaseStyle) and the per-mesh override table, so the editor can
// pre-fill its per-component style editor from the existing assignments.
public sealed record EngineSceneNodeSceneSource
{
    public string Kind { get; init; } = string.Empty;

    public string Path { get; init; } = string.Empty;

    public string ConsumeMode { get; init; } = string.Empty;

    public uint SceneIndex { get; init; }

    public uint StyleOverrideCount { get; init; }

    public bool HasBaseStyle { get; init; }

    public EngineGlbStyle BaseStyle { get; init; } = new();

    public List<EngineGlbStyleOverride> StyleOverrides { get; init; } = [];
}

// On-demand import of a GLB scene's component hierarchy (issue #213 Phase 3b-1):
// the flat node list backing a node's glb_scene_source descriptor, decoded from
// wz_import_glb_scene_hierarchy. Ok is false (with Error set) when the GLB could
// not be read/imported. The inspector links the tree via Component.ParentId.
public sealed record EngineGlbSceneHierarchy
{
    public bool Ok { get; init; }

    public string Error { get; init; } = string.Empty;

    public string SceneName { get; init; } = string.Empty;

    public uint SceneIndex { get; init; }

    public List<EngineGlbComponent> Components { get; init; } = [];
}

public sealed record EngineGlbComponent
{
    public string Id { get; init; } = string.Empty;

    public string Name { get; init; } = string.Empty;

    public string? ParentId { get; init; }

    public bool HasMesh { get; init; }

    public uint MeshIndex { get; init; }

    public uint NodeIndex { get; init; }
}

public sealed record EngineSceneComponent
{
    public string Kind { get; init; } = string.Empty;

    public string DisplayName { get; init; } = string.Empty;
}

public sealed record EngineSceneBehavior
{
    public string Id { get; init; } = string.Empty;

    public string Label { get; init; } = string.Empty;

    public string Module { get; init; } = string.Empty;

    public string Name { get; init; } = string.Empty;

    public bool Enabled { get; init; } = true;

    public List<string> Events { get; init; } = [];

    public List<EngineSceneBehaviorConfig> Config { get; init; } = [];
}

public sealed record EngineSceneBehaviorConfig
{
    public string Name { get; init; } = string.Empty;

    public string Kind { get; init; } = string.Empty;

    public string Value { get; init; } = string.Empty;
}
