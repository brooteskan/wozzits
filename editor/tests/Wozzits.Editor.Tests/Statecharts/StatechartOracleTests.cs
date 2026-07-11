using System.Text.Json.Nodes;
using Wozzits.Editor.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>
/// The E1 oracle: the C# authoring model + compiler must faithfully round-trip the
/// live corpus of statecharts the engine actually runs. Goldens are the 8 hand/gen
/// authored <c>*.sc.json</c> files, and the source of truth for "what the engine
/// consumes" is the minified <c>chart_ir</c> embedded in each scene behavior. These
/// pull from the sibling engine repo (both live under one parent, e.g. D:\code\wozzits);
/// set <c>WOZZITS_ENGINE_REPO</c> to override.
/// </summary>
public sealed class StatechartOracleTests
{
    private static string CorpusProjectDir() => CorpusLocator.ProjectDir();

    private static string StatechartsDir() => CorpusLocator.StatechartsDir();

    [Fact]
    public void AllGoldenCharts_Load_Validate_And_RoundTrip_Canonically()
    {
        var files = Directory.GetFiles(StatechartsDir(), "*.sc.json").OrderBy(f => f).ToArray();
        Assert.True(files.Length >= 8, $"expected >= 8 golden charts, found {files.Length}");

        var failures = new List<string>();
        foreach (var path in files)
        {
            var name = Path.GetFileName(path);
            var text = File.ReadAllText(path);
            try
            {
                var chart = StatechartJson.Load(text);

                var errors = StatechartJson.Validate(chart);
                if (errors.Count > 0)
                    failures.Add($"{name}: validation: {string.Join(" | ", errors)}");

                // (a) compiler output is canonically the authored chart
                var ir = StatechartJson.Emit(chart, indented: false);
                if (!CanonicalJson.Equal(text, ir, out var d1))
                    failures.Add($"{name}: emit != source: {d1}");

                // (b) compiling the compiled output is a fixpoint (load/emit are inverse)
                var again = StatechartJson.Emit(StatechartJson.Load(ir), indented: false);
                if (!CanonicalJson.Equal(ir, again, out var d2))
                    failures.Add($"{name}: not a round-trip fixpoint: {d2}");

                // (c) indented .sc.json form is the same chart as the minified chart_ir
                var pretty = StatechartJson.Emit(chart, indented: true);
                if (!CanonicalJson.Equal(ir, pretty, out var d3))
                    failures.Add($"{name}: pretty != minified: {d3}");
            }
            catch (Exception e)
            {
                failures.Add($"{name}: threw {e.GetType().Name}: {e.Message}");
            }
        }

        Assert.True(failures.Count == 0, "round-trip failures:\n  " + string.Join("\n  ", failures));
    }

    [Fact]
    public void SceneEmbeddedChartIr_Matches_CompilerOutput()
    {
        var scene = JsonNode.Parse(File.ReadAllText(Path.Combine(CorpusProjectDir(), "scene.json")))!.AsObject();
        var dir = StatechartsDir();

        var failures = new List<string>();
        int runners = 0;
        foreach (var node in scene["nodes"]!.AsArray())
        {
            if (node?["behaviors"] is not JsonArray behaviors) continue;
            foreach (var b in behaviors)
            {
                if (b?["module"]?.GetValue<string>() != "statechart_runner") continue;
                var cfg = b!["config"]!.AsObject();
                var chartName = cfg["chart"]!.GetValue<string>();
                var embed = cfg["chart_ir"]!.GetValue<string>();
                runners++;

                var goldenPath = Path.Combine(dir, chartName + ".sc.json");
                if (!File.Exists(goldenPath))
                {
                    failures.Add($"'{chartName}': no .sc.json source for the embedded chart_ir");
                    continue;
                }

                // my compiler, fed the .sc.json source, reproduces exactly what the engine runs
                var mine = StatechartJson.Emit(StatechartJson.Load(File.ReadAllText(goldenPath)), indented: false);
                if (!CanonicalJson.Equal(embed, mine, out var d1))
                    failures.Add($"'{chartName}': engine embed != compiler(source): {d1}");

                // and the embedded IR itself round-trips through the model
                var rt = StatechartJson.Emit(StatechartJson.Load(embed), indented: false);
                if (!CanonicalJson.Equal(embed, rt, out var d2))
                    failures.Add($"'{chartName}': embed is not a round-trip fixpoint: {d2}");
            }
        }

        Assert.True(runners >= 8, $"expected >= 8 statechart_runner embeds in scene.json, found {runners}");
        Assert.True(failures.Count == 0, "scene embed mismatches:\n  " + string.Join("\n  ", failures));
    }

    [Fact]
    public void TopoSort_Restores_A_Valid_Order_From_Shuffled_Pure()
    {
        var files = Directory.GetFiles(StatechartsDir(), "*.sc.json").OrderBy(f => f).ToArray();
        var failures = new List<string>();

        foreach (var path in files)
        {
            var name = Path.GetFileName(path);
            var chart = StatechartJson.Load(File.ReadAllText(path));

            // Corrupt the authored order; a correct compiler must restore a valid one.
            chart.Pure.Reverse();
            var ir = StatechartJson.Emit(chart, indented: false);
            var pure = JsonNode.Parse(ir)!["pure"]!.AsArray();

            var position = new Dictionary<string, int>();
            for (int i = 0; i < pure.Count; i++)
                position[pure[i]!["id"]!.GetValue<string>()] = i;

            for (int i = 0; i < pure.Count; i++)
                foreach (var dep in OpRefsOf(pure[i]!))
                    if (position[dep] >= i)
                        failures.Add($"{name}: op '{pure[i]!["id"]}' at {i} uses '{dep}' defined later at {position[dep]}");

            // Same op set, just reordered.
            var got = new HashSet<string>(position.Keys);
            var expected = StatechartJson.Load(File.ReadAllText(path)).Pure.Select(p => p.Id).ToHashSet();
            if (!got.SetEquals(expected))
                failures.Add($"{name}: op set changed after topo sort");
        }

        Assert.True(failures.Count == 0, "topo-sort failures:\n  " + string.Join("\n  ", failures));
    }

    private static IEnumerable<string> OpRefsOf(JsonNode op)
    {
        if (op["ins"] is JsonArray ins)
            foreach (var r in ins)
                if (r?["op"]?.GetValue<string>() is string s) yield return s;
        foreach (var key in new[] { "cond", "a", "b" })
            if (op[key]?["op"]?.GetValue<string>() is string s2) yield return s2;
    }
}
