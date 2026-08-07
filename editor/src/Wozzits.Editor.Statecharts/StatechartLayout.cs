using System.Text.Json;
using System.Text.Json.Nodes;

namespace Wozzits.Editor.Statecharts;

/// <summary>
/// Editor-owned layout for a chart: per-canvas zoom + hand-placed node positions. Persisted
/// in a <c>&lt;chart&gt;.sc.editor.json</c> sidecar next to the chart (the <c>.editor.</c>
/// convention, like the asset graph's sidecar) and applied OVER the auto-layout on open. The
/// engine never reads it.
/// </summary>
public sealed class StatechartLayout
{
    public readonly record struct Point(double X, double Y);

    public double ControlZoom { get; set; } = 1.0;

    public double DataflowZoom { get; set; } = 1.0;

    // When true the chart is protected: the editor never rewrites its .sc.json on save (only
    // this sidecar). Guards authored/reference charts against accidental edits.
    public bool ReadOnly { get; set; }

    public Dictionary<string, Point> StatePositions { get; } = new();

    public Dictionary<string, Point> NodePositions { get; } = new();

    public string ToJson()
    {
        var root = new JsonObject
        {
            ["readOnly"] = ReadOnly,
            ["control"] = new JsonObject { ["zoom"] = ControlZoom, ["positions"] = Positions(StatePositions) },
            ["dataflow"] = new JsonObject { ["zoom"] = DataflowZoom, ["positions"] = Positions(NodePositions) },
        };
        return root.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
    }

    public static StatechartLayout FromJson(string json)
    {
        var layout = new StatechartLayout();
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
        if (root["control"] is JsonObject control)
        {
            layout.ControlZoom = Number(control, "zoom", 1.0);
            ReadPositions(control["positions"] as JsonObject, layout.StatePositions);
        }
        if (root["dataflow"] is JsonObject dataflow)
        {
            layout.DataflowZoom = Number(dataflow, "zoom", 1.0);
            ReadPositions(dataflow["positions"] as JsonObject, layout.NodePositions);
        }
        return layout;
    }

    private static JsonObject Positions(Dictionary<string, Point> positions)
    {
        var o = new JsonObject();
        foreach (var (id, p) in positions)
        {
            o[id] = new JsonObject { ["x"] = p.X, ["y"] = p.Y };
        }
        return o;
    }

    private static void ReadPositions(JsonObject? obj, Dictionary<string, Point> into)
    {
        if (obj is null)
        {
            return;
        }

        foreach (var member in obj)
        {
            if (member.Value is JsonObject p)
            {
                into[member.Key] = new Point(Number(p, "x", 0), Number(p, "y", 0));
            }
        }
    }

    private static double Number(JsonObject o, string key, double fallback) =>
        o[key] is JsonValue v && v.TryGetValue<double>(out var x) ? x : fallback;
}
