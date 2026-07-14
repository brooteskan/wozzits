using System.Text.Json;
using System.Text.Json.Nodes;

namespace Wozzits.Editor.Statecharts;

/// <summary>
/// Editor-owned layout for a mind: the canvas zoom + hand-placed qubit positions.
/// Persisted in a <c>&lt;mind&gt;.mind.editor.json</c> sidecar next to the .mind.json (the
/// <c>.editor.</c> convention) and applied OVER the circular auto-layout on open. The
/// engine never reads it. Positions are keyed by qubit id (q0..qN-1), which the loader
/// regenerates deterministically, so they survive a reload as long as the count is stable.
/// </summary>
public sealed class MindLayout
{
    public readonly record struct Point(double X, double Y);

    public double Zoom { get; set; } = 1.0;

    public bool ReadOnly { get; set; }

    public Dictionary<string, Point> Positions { get; } = new();

    public string ToJson()
    {
        var positions = new JsonObject();
        foreach (var (id, p) in Positions)
        {
            positions[id] = new JsonObject { ["x"] = p.X, ["y"] = p.Y };
        }

        var root = new JsonObject
        {
            ["readOnly"] = ReadOnly,
            ["zoom"] = Zoom,
            ["positions"] = positions,
        };
        return root.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
    }

    public static MindLayout FromJson(string json)
    {
        var layout = new MindLayout();
        JsonNode? parsed;
        try
        {
            parsed = JsonNode.Parse(json);
        }
        catch (JsonException)
        {
            return layout;
        }

        if (parsed is not JsonObject root)
        {
            return layout;
        }

        layout.ReadOnly = root["readOnly"] is JsonValue rv && rv.TryGetValue<bool>(out var ro) && ro;
        layout.Zoom = Number(root, "zoom", 1.0);
        if (root["positions"] is JsonObject positions)
        {
            foreach (var member in positions)
            {
                if (member.Value is JsonObject p)
                {
                    layout.Positions[member.Key] = new Point(Number(p, "x", 0), Number(p, "y", 0));
                }
            }
        }

        return layout;
    }

    private static double Number(JsonObject o, string key, double fallback) =>
        o[key] is JsonValue v && v.TryGetValue<double>(out var x) ? x : fallback;
}
