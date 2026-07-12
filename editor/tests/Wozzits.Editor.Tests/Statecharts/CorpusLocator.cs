namespace Wozzits.Editor.Tests.Statecharts;

// Locates the live statechart corpus in the sibling engine repo (both live under one
// parent, e.g. D:\code\wozzits). Set WOZZITS_ENGINE_REPO to override.
internal static class CorpusLocator
{
    public static string ProjectDir()
    {
        var roots = new List<string>();
        var env = Environment.GetEnvironmentVariable("WOZZITS_ENGINE_REPO");
        if (!string.IsNullOrEmpty(env))
        {
            roots.Add(env);
        }
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
        {
            roots.Add(Path.Combine(d.FullName, "wozzits-window-engine"));
        }

        foreach (var r in roots)
        {
            var p = Path.Combine(r, "window_engine", "resources", "projects", "test_mesh_001");
            if (Directory.Exists(Path.Combine(p, "behavior", "statecharts")))
            {
                return p;
            }
        }

        throw new DirectoryNotFoundException(
            "Could not locate the engine corpus (wozzits-window-engine/.../test_mesh_001/behavior/statecharts). " +
            "Place the engine repo beside the editor repo, or set WOZZITS_ENGINE_REPO. Searched: " +
            string.Join("; ", roots));
    }

    public static string StatechartsDir() => Path.Combine(ProjectDir(), "behavior", "statecharts");

    // The known demo charts (the editor's oracle corpus). Iterating tests use this rather than
    // "every *.sc.json in the folder" so working charts a user creates there (New Chart writes
    // into the same folder) don't break the corpus tests.
    private static readonly string[] GoldenNames =
    {
        "traffic_light", "skinner", "twins", "village4", "village5", "caravan", "squad", "zeno",
    };

    public static IReadOnlyList<string> GoldenChartFiles()
    {
        var dir = StatechartsDir();
        return GoldenNames
            .Select(n => Path.Combine(dir, n + ".sc.json"))
            .Where(File.Exists)
            .OrderBy(p => p)
            .ToArray();
    }
}
