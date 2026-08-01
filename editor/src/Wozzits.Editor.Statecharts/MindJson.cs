using System.Text.Json;
using System.Text.Json.Nodes;

namespace Wozzits.Editor.Statecharts;

/// <summary>Thrown when mind JSON is malformed or structurally invalid.</summary>
public sealed class MindFormatException : Exception
{
    public MindFormatException(string message) : base(message) { }
}

/// <summary>
/// Loads and compiles a cognition MIND between the POSITIONAL on-disk schema
/// ("wozzits.mind.ir.v0") and the in-memory authoring model. The on-disk form is the
/// SAME JSON the engine's parse_mind reads (mind_ir.cpp): a qubit COUNT, with goals
/// and bonds addressing qubits by INDEX, plus chi / memory / clock / commit. Emit
/// resolves each bond's qubit Ids -&gt; current indices; Load regenerates positional
/// qubit Ids (q0..qN-1), since the IR carries no ids. <c>Emit(indented:false)</c> is
/// the <c>mind_ir</c> string embedded on a quantum_agent; <c>Emit(indented:true)</c>
/// is the readable <c>.mind.json</c> source.
/// </summary>
public static class MindJson
{
    private static double? AsNum(JsonNode? n)
    {
        if (n is null) return null;
        try { return (double?)n; }
        catch { return null; }
    }

    // ======================================================================
    //  Load: .mind.json / mind_ir text -> authoring model
    // ======================================================================

    public static Mind Load(string json)
    {
        JsonNode? root;
        try { root = JsonNode.Parse(json); }
        catch (JsonException e) { throw new MindFormatException($"JSON parse failed: {e.Message}"); }
        if (root is not JsonObject o)
            throw new MindFormatException("mind root must be an object");

        // Read the schema, and ROUND-TRIP it. Load ignored this field entirely
        // while Emit hardcoded MindSchema.V0 (D3-C24), so the editor stamped a
        // conformance claim onto every mind it wrote without ever having checked
        // one -- and the engine's parse_mind does not look at the field at all, so
        // the editor is the only party that could. Writing a claim nobody verifies
        // is the worst of both.
        //
        // Same gate, and the same deliberate asymmetry, as StatechartJson.Load
        // (D2-H3): a MISSING schema is accepted as v0, so older files still open
        // and saving heals them, while a PRESENT but unrecognized one is refused
        // rather than loaded under the old meaning of its keys and then saved back
        // over the newer file.
        var schema = o["schema"] is { } schemaNode
            ? schemaNode.ToString()
            : MindSchema.V0;
        if (schema != MindSchema.V0)
            throw new MindFormatException(
                $"unrecognized mind schema '{schema}' -- this build reads "
                + $"'{MindSchema.V0}'. A mind from a newer editor is refused "
                + "rather than loaded under the old meaning of its keys (and then "
                + "saved back over the newer file).");

        int qubits = (int)(AsNum(o["qubits"]) ?? 0);
        if (qubits < 1)
            throw new MindFormatException("mind needs a positive `qubits` count");

        var m = new Mind { Schema = schema, Name = (string?)o["name"] ?? "" };
        for (int i = 0; i < qubits; i++)
            m.Qubits.Add(new MindQubit { Id = $"q{i}" });

        if (o["goals"] is JsonArray goals)
            foreach (var g in goals)
            {
                if (g is not JsonObject go) continue;
                int q = (int)(AsNum(go["q"]) ?? -1);
                if (q < 0 || q >= qubits)
                    throw new MindFormatException("mind goal names an out-of-range qubit");
                m.Qubits[q].Goal = AsNum(go["field"]) ?? 0.0;
            }

        if (o["bonds"] is JsonArray bonds)
            foreach (var b in bonds)
            {
                if (b is not JsonObject bo) continue;
                int a = (int)(AsNum(bo["a"]) ?? -1);
                int bb = (int)(AsNum(bo["b"]) ?? -1);
                if (a < 0 || bb < 0 || a >= qubits || bb >= qubits)
                    throw new MindFormatException("mind bond names an out-of-range qubit");
                if (a == bb)
                    throw new MindFormatException("mind bond couples a qubit to itself");
                m.Bonds.Add(new MindBond
                {
                    A = m.Qubits[a].Id,
                    B = m.Qubits[bb].Id,
                    J = AsNum(bo["j"]) ?? 0.0,
                });
            }

        m.Chi = Math.Max(0, (int)(AsNum(o["chi"]) ?? 0));
        m.Memory = Math.Max(0, (int)(AsNum(o["memory"]) ?? 0));

        if (o["clock"] is JsonObject ck)
        {
            m.Clock.GammaStart = AsNum(ck["gamma_start"]) ?? m.Clock.GammaStart;
            m.Clock.GammaEnd = AsNum(ck["gamma_end"]) ?? m.Clock.GammaEnd;
            m.Clock.AnnealSeconds = AsNum(ck["anneal_seconds"]) ?? m.Clock.AnnealSeconds;
            m.Clock.RelaxRate = AsNum(ck["relax_rate"]) ?? m.Clock.RelaxRate;
        }
        if (o["commit"] is JsonObject cm)
        {
            m.Commit.Confidence = AsNum(cm["confidence"]) ?? m.Commit.Confidence;
            m.Commit.Decoherence = AsNum(cm["decoherence"]) ?? m.Commit.Decoherence;
        }
        return m;
    }

    // ======================================================================
    //  Emit: authoring model -> .mind.json / mind_ir text
    // ======================================================================

    public static string Emit(Mind m, bool indented)
    {
        var errors = Validate(m);
        if (errors.Count > 0)
            throw new MindFormatException(errors[0]);

        var root = new JsonObject
        {
            // What was READ, not a constant (D3-C24). Load refuses anything but
            // v0 today, so this is v0 in practice -- the point is that the value
            // now travels with the document instead of being asserted here.
            ["schema"] = m.Schema,
            ["qubits"] = m.Qubits.Count,
        };
        if (m.Name.Length > 0) root["name"] = m.Name;

        // Goals are sparse -- a zero bias is the same as none.
        var goals = new JsonArray();
        for (int i = 0; i < m.Qubits.Count; i++)
            if (m.Qubits[i].Goal != 0.0)
                goals.Add(new JsonObject { ["q"] = i, ["field"] = m.Qubits[i].Goal });
        if (goals.Count > 0) root["goals"] = goals;

        // Bonds: model Ids -> positional indices.
        var bonds = new JsonArray();
        foreach (var b in m.Bonds)
            bonds.Add(new JsonObject
            {
                ["a"] = m.IndexOfQubit(b.A),
                ["b"] = m.IndexOfQubit(b.B),
                ["j"] = b.J,
            });
        if (bonds.Count > 0) root["bonds"] = bonds;

        root["chi"] = m.Chi;
        if (m.Memory > 0) root["memory"] = m.Memory;
        root["clock"] = new JsonObject
        {
            ["gamma_start"] = m.Clock.GammaStart,
            ["gamma_end"] = m.Clock.GammaEnd,
            ["anneal_seconds"] = m.Clock.AnnealSeconds,
            ["relax_rate"] = m.Clock.RelaxRate,
        };
        root["commit"] = new JsonObject
        {
            ["confidence"] = m.Commit.Confidence,
            ["decoherence"] = m.Commit.Decoherence,
        };
        return root.ToJsonString(new JsonSerializerOptions { WriteIndented = indented });
    }

    // ======================================================================
    //  Validate
    // ======================================================================

    /// <summary>Structural problems that would make the mind unloadable or emit a
    /// broken IR. Backend&lt;-&gt;topology validity (a chi&gt;=2 TTN needs a nearest-
    /// neighbour chain) is advisory and surfaced by the editor, not blocked here.</summary>
    public static IReadOnlyList<string> Validate(Mind m)
    {
        var errors = new List<string>();
        if (m.Qubits.Count < 1)
            errors.Add("a mind needs at least one qubit");
        var ids = new HashSet<string>();
        foreach (var q in m.Qubits)
            if (!ids.Add(q.Id))
                errors.Add($"duplicate qubit id '{q.Id}'");
        foreach (var b in m.Bonds)
        {
            if (m.IndexOfQubit(b.A) < 0 || m.IndexOfQubit(b.B) < 0)
                errors.Add("a bond references a qubit that is not in the mind");
            else if (b.A == b.B)
                errors.Add("a bond couples a qubit to itself");
        }
        return errors;
    }
}
