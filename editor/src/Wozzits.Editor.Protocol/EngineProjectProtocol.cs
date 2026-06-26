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

    public EngineSceneRenderableSource RenderableSource { get; init; } = new();

    public EngineSceneTransform? Transform { get; init; }

    public EngineSceneCamera? Camera { get; init; }

    public EngineSceneRenderable? Renderable { get; init; }

    public List<EngineSceneComponent> Components { get; init; } = [];

    public List<EngineSceneBehavior> Behaviors { get; init; } = [];

    public EngineSceneNodeSceneSource? SceneSource { get; init; }

    public List<EngineSceneNode> Children { get; init; } = [];
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

// Read-only summary of a node's GLB scene-source descriptor (issue #213),
// present only when the node carries a glb_scene_source block. The full
// MeshRenderStyleData is not surfaced: HasBaseStyle is a flag and
// StyleOverrideCount is the override count.
public sealed record EngineSceneNodeSceneSource
{
    public string Kind { get; init; } = string.Empty;

    public string Path { get; init; } = string.Empty;

    public string ConsumeMode { get; init; } = string.Empty;

    public uint SceneIndex { get; init; }

    public uint StyleOverrideCount { get; init; }

    public bool HasBaseStyle { get; init; }
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
