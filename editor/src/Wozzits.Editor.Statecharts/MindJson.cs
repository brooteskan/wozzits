using System.Collections.Generic;
using System.Linq;
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
/// and bonds addressing qubits by INDEX, plus chi / memory / clock / commit, and an
/// optional agent layout (dispositions + one_hot). Emit resolves each qubit's model
/// Id -&gt; its positional index; Load regenerates positional qubit Ids (q0..qN-1),
/// since the IR carries no ids. <c>Emit(indented:false)</c> is the <c>mind_ir</c> string
/// embedded on a quantum_agent; <c>Emit(indented:true)</c> is the readable
/// <c>.mind.json</c> source.
///
/// AGENTS: the engine lays each agent's dispositions out as a CONTIGUOUS block of
/// qubit indices, so Emit orders qubits grouped by agent (see Mind.FlatOrder) and the
/// positional index of a qubit follows that grouping, not the raw Qubits list. A mind
/// whose every agent owns a single disposition and sets no exclusivity carries no
/// layout, and emits byte-identically to the pre-agent positional shape.
/// </summary>
public static class MindJson
{
    private static double? AsNum(JsonNode? n)
    {
        if (n is null) return null;
        try { return (double?)n; }
        catch { return null; }
    }

    private static int AsInt(JsonNode? n, int fallback) => (int)(AsNum(n) ?? fallback);

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

        // The agent layout can SUPPLY the qubit count (like mind_ir.cpp): read
        // `dispositions` first, then reconcile with an explicit `qubits`.
        List<int>? sizes = null;
        if (o["dispositions"] is JsonArray dispArr)
        {
            sizes = new List<int>();
            foreach (var d in dispArr)
            {
                int size = AsInt(d, 0);
                if (size < 1)
                    throw new MindFormatException("mind `dispositions` entries must be >= 1");
                sizes.Add(size);
            }
            if (sizes.Count == 0)
                throw new MindFormatException("mind `dispositions` must not be empty");
        }

        int qubits;
        if (sizes is not null)
        {
            int sum = sizes.Sum();
            if (o["qubits"] is { } qn && AsInt(qn, -1) != sum)
                throw new MindFormatException(
                    "mind `qubits` disagrees with the sum of `dispositions`");
            qubits = sum;
        }
        else
        {
            qubits = AsInt(o["qubits"], 0);
            if (qubits < 1)
                throw new MindFormatException("mind needs a positive `qubits` count");
        }

        var m = new Mind { Schema = schema, Name = (string?)o["name"] ?? "" };
        for (int i = 0; i < qubits; i++)
            m.Qubits.Add(new MindQubit { Id = $"q{i}" });

        // Materialize agents + the per-agent contiguous flat-index block. Absent a
        // layout, every qubit is its own single-disposition agent (the plain shape).
        var blocks = new List<(int Start, int Size)>();
        if (sizes is not null)
        {
            int start = 0;
            foreach (var size in sizes)
            {
                var agent = new MindAgent { Id = m.FreshAgentId() };
                m.Agents.Add(agent);
                for (int k = 0; k < size; k++)
                    m.Qubits[start + k].Agent = agent.Id;
                blocks.Add((start, size));
                start += size;
            }
        }
        else
        {
            for (int i = 0; i < qubits; i++)
            {
                var agent = new MindAgent { Id = m.FreshAgentId() };
                m.Agents.Add(agent);
                m.Qubits[i].Agent = agent.Id;
                blocks.Add((i, 1));
            }
        }

        // Exclusivity, aligned to the agents in `dispositions` order. Mirrors the
        // engine: one_hot needs a layout, and cannot have more entries than agents.
        if (o["one_hot"] is JsonArray oneHot)
        {
            if (sizes is null)
                throw new MindFormatException("mind `one_hot` needs a `dispositions` layout");
            if (oneHot.Count > m.Agents.Count)
                throw new MindFormatException("mind `one_hot` has more entries than agents");
            for (int i = 0; i < oneHot.Count; i++)
                m.Agents[i].OneHot = AsNum(oneHot[i]) ?? 0.0;
        }

        // Resolve a goal/bond endpoint: either the flat qubit `q`/`a`/`b`, or -- with
        // a layout -- the (agent, disposition) pair. Returns -1 out of range, mirroring
        // parse_mind's resolve so the two front ends accept exactly the same forms.
        int Resolve(JsonObject obj, string flatKey, string agentKey, string dispKey)
        {
            int agent = AsInt(obj[agentKey], -1);
            if (agent >= 0)
            {
                if (sizes is null || agent >= blocks.Count) return -1;
                int d = AsInt(obj[dispKey], 0);
                if (d < 0 || d >= blocks[agent].Size) return -1;
                return blocks[agent].Start + d;
            }
            int q = AsInt(obj[flatKey], -1);
            return (q < 0 || q >= qubits) ? -1 : q;
        }

        if (o["goals"] is JsonArray goals)
            foreach (var g in goals)
            {
                if (g is not JsonObject go) continue;
                int q = Resolve(go, "q", "agent", "disposition");
                if (q < 0)
                    throw new MindFormatException("mind goal names an out-of-range qubit");
                m.Qubits[q].Goal = AsNum(go["field"]) ?? 0.0;
            }

        if (o["bonds"] is JsonArray bonds)
            foreach (var b in bonds)
            {
                if (b is not JsonObject bo) continue;
                int a = Resolve(bo, "a", "a_agent", "a_disposition");
                int bb = Resolve(bo, "b", "b_agent", "b_disposition");
                if (a < 0 || bb < 0)
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

        m.Chi = Math.Max(0, AsInt(o["chi"], 0));
        m.Memory = Math.Max(0, AsInt(o["memory"], 0));

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
        // Make the agent partition total (orphan qubits get a singleton agent, empty
        // agents drop) so the flat order below covers every qubit exactly once.
        m.NormalizeAgents();

        var errors = Validate(m);
        if (errors.Count > 0)
            throw new MindFormatException(errors[0]);

        // The positional order the IR indices refer to: each agent's dispositions
        // contiguously, agents in first-appearance order. With every qubit its own
        // agent this is just the Qubits order, so the plain shape is unchanged.
        var flat = m.FlatOrder();
        var flatIndex = new Dictionary<string, int>();
        for (int i = 0; i < flat.Count; i++)
            flatIndex[flat[i].Id] = i;

        var root = new JsonObject
        {
            // What was READ, not a constant (D3-C24). Load refuses anything but
            // v0 today, so this is v0 in practice -- the point is that the value
            // now travels with the document instead of being asserted here.
            ["schema"] = m.Schema,
            ["qubits"] = m.Qubits.Count,
        };
        if (m.Name.Length > 0) root["name"] = m.Name;

        // Goals are sparse -- a zero bias is the same as none -- and addressed by the
        // FLAT (grouped) index.
        var goals = new JsonArray();
        for (int i = 0; i < flat.Count; i++)
            if (flat[i].Goal != 0.0)
                goals.Add(new JsonObject { ["q"] = i, ["field"] = flat[i].Goal });
        if (goals.Count > 0) root["goals"] = goals;

        // Bonds: model Ids -> flat positional indices.
        var bonds = new JsonArray();
        foreach (var b in m.Bonds)
            bonds.Add(new JsonObject
            {
                ["a"] = flatIndex[b.A],
                ["b"] = flatIndex[b.B],
                ["j"] = b.J,
            });
        if (bonds.Count > 0) root["bonds"] = bonds;

        // The agent layout -- only when it says something the plain shape cannot (an
        // agent with >1 disposition, or any exclusivity). Emitted in agent order so
        // dispositions/one_hot line up with the contiguous blocks the flat order laid.
        if (m.HasLayout)
        {
            var order = m.AgentOrder();
            var dispositions = new JsonArray();
            foreach (var a in order)
                dispositions.Add(m.MembersOf(a.Id).Count);
            root["dispositions"] = dispositions;

            // one_hot is sparse from the tail: emit up to the last exclusive agent
            // (the engine treats a short array as "the rest have none").
            int lastExclusive = -1;
            for (int i = 0; i < order.Count; i++)
                if (order[i].OneHot != 0.0) lastExclusive = i;
            if (lastExclusive >= 0)
            {
                var oneHot = new JsonArray();
                for (int i = 0; i <= lastExclusive; i++)
                    oneHot.Add(order[i].OneHot);
                root["one_hot"] = oneHot;
            }
        }

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
    /// broken IR. Backend&lt;-&gt;topology validity (a chi&gt;=2 TTN prefers a nearest-
    /// neighbour chain) and one-hot on a single-disposition agent are advisory and
    /// surfaced by the editor, not blocked here.</summary>
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
