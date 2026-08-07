using System.Text.Json.Nodes;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>
/// Structural JSON equality for the statechart oracle. The golden charts exist in
/// two formatting styles (hand-authored compact vs generated indent=2), so a
/// byte-diff is meaningless; what matters is that the same chart comes out. Object
/// members are compared order-INsensitively, arrays order-SENSITIVELY (pure[]/do[]
/// order is load-bearing), and numbers by value (so 3 == 3.0).
/// </summary>
internal static class CanonicalJson
{
    public static bool Equal(string a, string b, out string diff)
    {
        JsonNode? na = JsonNode.Parse(a);
        JsonNode? nb = JsonNode.Parse(b);
        diff = "";
        return DeepEqual(na, nb, "$", ref diff);
    }

    private static bool DeepEqual(JsonNode? a, JsonNode? b, string path, ref string diff)
    {
        if (a is null && b is null) return true;
        if (a is null || b is null)
        {
            diff = $"{path}: present on {(a is null ? "B" : "A")} only";
            return false;
        }

        switch (a, b)
        {
            case (JsonObject oa, JsonObject ob):
            {
                var keys = new HashSet<string>();
                foreach (var kv in oa) keys.Add(kv.Key);
                foreach (var kv in ob) keys.Add(kv.Key);
                foreach (var k in keys)
                {
                    bool ha = oa.TryGetPropertyValue(k, out var va);
                    bool hb = ob.TryGetPropertyValue(k, out var vb);
                    if (!ha || !hb)
                    {
                        diff = $"{path}.{k}: present on {(ha ? "A" : "B")} only";
                        return false;
                    }
                    if (!DeepEqual(va, vb, $"{path}.{k}", ref diff)) return false;
                }
                return true;
            }
            case (JsonArray aa, JsonArray ab):
            {
                if (aa.Count != ab.Count)
                {
                    diff = $"{path}: array length {aa.Count} vs {ab.Count}";
                    return false;
                }
                for (int i = 0; i < aa.Count; i++)
                    if (!DeepEqual(aa[i], ab[i], $"{path}[{i}]", ref diff)) return false;
                return true;
            }
            case (JsonValue va, JsonValue vb):
                return ValueEqual(va, vb, path, ref diff);
            default:
                diff = $"{path}: kind mismatch ({a.GetType().Name} vs {b.GetType().Name})";
                return false;
        }
    }

    private static bool ValueEqual(JsonValue a, JsonValue b, string path, ref string diff)
    {
        if (a.TryGetValue<bool>(out var ba) && b.TryGetValue<bool>(out var bb))
            return ba == bb || Mismatch(a, b, path, ref diff);
        if (a.TryGetValue<double>(out var da) && b.TryGetValue<double>(out var db))
            return da == db || Mismatch(a, b, path, ref diff);
        if (a.TryGetValue<string>(out var sa) && b.TryGetValue<string>(out var sb))
            return sa == sb || Mismatch(a, b, path, ref diff);
        return Mismatch(a, b, path, ref diff);
    }

    private static bool Mismatch(JsonValue a, JsonValue b, string path, ref string diff)
    {
        diff = $"{path}: {a.ToJsonString()} != {b.ToJsonString()}";
        return false;
    }
}
