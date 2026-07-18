namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Nodes;

// Re-embed a just-saved chart/mind's compiled IR into behaviours that live in SCENELET files
// on disk. A behaviour in a scenelet is not part of the currently-open scene tree, so the
// in-scene refresh (MainWindowViewModel.RefreshAttached*) never touches it and its embedded IR
// (chart_ir / mind_ir) goes stale -- silently freezing it at whatever it last compiled, no
// matter how much the source .sc.json / .mind.json is edited afterward. Spawned actors read
// scenelets, so this is the copy gameplay actually runs.
public static class SceneletRunnerRefresh
{
    // Rewrite every behaviour of `module` in `sceneletJson` whose config `nameKey` names one of
    // `savedByName`, replacing its config `irKey` with the fresh IR. Returns true (with the
    // rewritten json + count changed) iff anything actually changed -- a behaviour already
    // carrying the current IR is left alone, so a scenelet that happens to be the open scene
    // (already refreshed in place) is a no-op here rather than a redundant reformat.
    public static bool TryRefresh(
        string sceneletJson,
        string module,
        string nameKey,
        string irKey,
        IReadOnlyDictionary<string, string> savedByName,
        out string updatedJson,
        out int updatedCount)
    {
        updatedJson = sceneletJson;
        updatedCount = 0;

        if (savedByName.Count == 0
            || JsonNode.Parse(sceneletJson) is not JsonObject root
            || root["nodes"] is not JsonArray nodes)
        {
            return false;
        }

        var changed = 0;
        foreach (var node in nodes)
        {
            if (node is not JsonObject nodeObj)
            {
                continue;
            }

            foreach (var behavior in Behaviours(nodeObj))
            {
                if (behavior is not JsonObject b
                    || (string?)b["module"] != module
                    || b["config"] is not JsonObject config)
                {
                    continue;
                }

                var name = (string?)config[nameKey];
                if (name is null || !savedByName.TryGetValue(name, out var ir))
                {
                    continue;
                }
                if ((string?)config[irKey] == ir)
                {
                    continue;   // already current -- leave the file untouched
                }

                config[irKey] = ir;
                changed++;
            }
        }

        if (changed == 0)
        {
            return false;
        }

        updatedJson = root.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
        updatedCount = changed;
        return true;
    }

    // A scene node carries its behaviours as a "behaviors" array (and, on older files, a
    // single "behavior" object). Yield both shapes.
    private static IEnumerable<JsonNode?> Behaviours(JsonObject node)
    {
        if (node["behaviors"] is JsonArray array)
        {
            foreach (var b in array)
            {
                yield return b;
            }
        }
        if (node["behavior"] is JsonObject single)
        {
            yield return single;
        }
    }
}
