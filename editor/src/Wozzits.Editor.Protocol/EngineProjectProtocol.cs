namespace Wozzits.Editor.Protocol;

public static class EngineProjectStatus
{
    public const string Missing = "missing";
    public const string Invalid = "invalid";
    public const string Valid = "valid";
}

public sealed record EngineProjectManifest
{
    public string Root { get; init; } = string.Empty;

    public string ManifestPath { get; init; } = string.Empty;

    public string Schema { get; init; } = string.Empty;

    public uint FormatVersion { get; init; }

    public string Name { get; init; } = string.Empty;

    public bool RhiRenderPath { get; init; }

    public string ScenePath { get; init; } = string.Empty;

    public string AssetGraphPath { get; init; } = string.Empty;

    public string BehaviorProjectFolder { get; init; } = string.Empty;

    public string BehaviorModuleFolder { get; init; } = string.Empty;
}

public sealed record EngineProjectResponse
{
    public bool Ok { get; init; }

    public string Status { get; init; } = EngineProjectStatus.Invalid;

    public bool Created { get; init; }

    public string Error { get; init; } = string.Empty;

    public EngineProjectManifest Project { get; init; } = new();

    public bool IsValid => Ok && Status == EngineProjectStatus.Valid;

    public bool IsMissing => Status == EngineProjectStatus.Missing;
}
