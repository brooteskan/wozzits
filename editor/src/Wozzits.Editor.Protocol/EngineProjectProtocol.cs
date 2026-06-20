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
}

public sealed record EngineAssetGraphPort
{
    public uint Index { get; init; }

    public string Label { get; init; } = string.Empty;
}

public sealed record EngineAssetGraphEdge
{
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

public sealed record EngineSceneComponent
{
    public string Kind { get; init; } = string.Empty;

    public string DisplayName { get; init; } = string.Empty;
}
