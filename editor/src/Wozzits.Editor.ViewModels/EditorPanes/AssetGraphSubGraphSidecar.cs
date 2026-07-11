namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.Text.Json;
using System.Text.Json.Serialization;

// Editor-only persistence of node sub-graphs to a sidecar JSON file, kept entirely
// separate from the engine-authored asset graph (issue woguls/wozzits-editor#1): the
// asset compiler never reads it, and it references nodes only by their stable engine
// node id. Serialization is pure (string in / string out) so it is unit-testable; the
// caller owns the file I/O.
public sealed record PersistedSubGraph
{
    public string Id { get; init; } = string.Empty;
    public string Name { get; init; } = string.Empty;
    public string? ParentId { get; init; }
    public double ProxyX { get; init; }
    public double ProxyY { get; init; }
    public List<ulong> MemberNodeIds { get; init; } = [];
}

public sealed record SubGraphSidecarDocument
{
    public int Version { get; init; } = 1;
    public List<PersistedSubGraph> SubGraphs { get; init; } = [];
}

public static class AssetGraphSubGraphSidecar
{
    // Sits next to the engine's assets.graph.json in the project directory. The
    // ".editor." infix mirrors the scene_editor_metadata convention: editor-only, ignored
    // by the asset compiler.
    public const string FileName = "assets.graph.editor.json";

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    public static string Serialize(IEnumerable<AssetGraphSubGraph> subGraphs)
    {
        var document = new SubGraphSidecarDocument
        {
            SubGraphs = subGraphs
                .Select(subGraph => new PersistedSubGraph
                {
                    Id = subGraph.Id,
                    Name = subGraph.Name,
                    ParentId = subGraph.ParentId,
                    ProxyX = subGraph.ProxyX,
                    ProxyY = subGraph.ProxyY,
                    MemberNodeIds = [.. subGraph.MemberNodeIds],
                })
                .ToList(),
        };

        return JsonSerializer.Serialize(document, Options);
    }

    // Never throws: a missing/empty/corrupt sidecar yields no groups rather than blocking
    // the project from opening.
    public static IReadOnlyList<PersistedSubGraph> Deserialize(string json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return [];
        }

        try
        {
            var document = JsonSerializer.Deserialize<SubGraphSidecarDocument>(json, Options);
            return document?.SubGraphs ?? [];
        }
        catch (JsonException)
        {
            return [];
        }
    }
}
