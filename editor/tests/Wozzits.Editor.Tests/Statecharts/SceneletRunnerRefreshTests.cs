using System.Collections.Generic;
using System.Text.Json.Nodes;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

// A runner/quantum_agent that lives in a scenelet (not the open scene) kept a stale
// chart_ir/mind_ir when its source was edited -- so spawned tanks silently ran an old chart.
public sealed class SceneletRunnerRefreshTests
{
    private const string Scenelet = """
    {
      "schema": "wozzits.scene.v0",
      "nodes": [
        {
          "id": "enemy_tank_root",
          "name": "enemy_tank_v1",
          "behaviors": [
            { "id": "b.tank", "module": "enemy_tank_v1", "config": {} },
            { "id": "b.runner", "module": "statechart_runner",
              "config": { "chart": "hunt_or_refuel", "chart_ir": "OLD_IR" } },
            { "id": "b.agent", "module": "quantum_agent",
              "config": { "mind": "first_mind", "mind_ir": "OLD_MIND" } }
          ]
        }
      ]
    }
    """;

    [Fact]
    public void Refreshes_A_Runner_Chart_Ir_In_A_Scenelet_File()
    {
        var saved = new Dictionary<string, string> { ["hunt_or_refuel"] = "NEW_IR" };

        Assert.True(SceneletRunnerRefresh.TryRefresh(
            Scenelet, "statechart_runner", "chart", "chart_ir", saved,
            out var updated, out var count));

        Assert.Equal(1, count);
        Assert.Equal("NEW_IR", RunnerConfig(updated, "b.runner", "chart_ir"));
        Assert.Equal("OLD_MIND", RunnerConfig(updated, "b.agent", "mind_ir"));   // untouched
    }

    [Fact]
    public void Refreshes_A_Quantum_Agent_Mind_Ir_Too()
    {
        var saved = new Dictionary<string, string> { ["first_mind"] = "NEW_MIND" };

        Assert.True(SceneletRunnerRefresh.TryRefresh(
            Scenelet, "quantum_agent", "mind", "mind_ir", saved,
            out var updated, out var count));

        Assert.Equal(1, count);
        Assert.Equal("NEW_MIND", RunnerConfig(updated, "b.agent", "mind_ir"));
    }

    [Fact]
    public void No_Change_When_The_Ir_Is_Already_Current()
    {
        // The in-scene refresh already set chart_ir to the fresh value; the disk scan must
        // then be a no-op so it does not needlessly rewrite (and reformat) the open scenelet.
        var saved = new Dictionary<string, string> { ["hunt_or_refuel"] = "OLD_IR" };

        Assert.False(SceneletRunnerRefresh.TryRefresh(
            Scenelet, "statechart_runner", "chart", "chart_ir", saved, out _, out var count));
        Assert.Equal(0, count);
    }

    [Fact]
    public void No_Change_When_No_Runner_Names_A_Saved_Chart()
    {
        var saved = new Dictionary<string, string> { ["some_other_chart"] = "NEW_IR" };

        Assert.False(SceneletRunnerRefresh.TryRefresh(
            Scenelet, "statechart_runner", "chart", "chart_ir", saved, out _, out _));
    }

    private static string? RunnerConfig(string json, string behaviorId, string key)
    {
        var nodes = (JsonNode.Parse(json) as JsonObject)?["nodes"] as JsonArray;
        foreach (var n in nodes!)
        {
            if ((n as JsonObject)?["behaviors"] is JsonArray behaviors)
            {
                foreach (var b in behaviors)
                {
                    if ((string?)b?["id"] == behaviorId)
                    {
                        return (string?)b?["config"]?[key];
                    }
                }
            }
        }
        return null;
    }
}
