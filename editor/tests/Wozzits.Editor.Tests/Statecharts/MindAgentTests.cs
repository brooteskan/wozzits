using System.Text.Json.Nodes;
using Wozzits.Editor.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>The agent layer -- multi-disposition agents (`dispositions`) and per-agent
/// exclusivity (`one_hot`). The editor must emit exactly the positional layout the
/// engine's parse_mind reads: each agent's dispositions in a CONTIGUOUS block of qubit
/// indices, dispositions/one_hot aligned to that block order. And a mind with no real
/// grouping must still emit the plain (layout-free) shape.</summary>
public sealed class MindAgentTests
{
    // Agent a0 owns q0/q1/q2 as a one-hot choice (strength 2.0); agent a1 owns q3.
    private static Mind GroupedMind()
    {
        var m = new Mind { Name = "squad", Chi = 0 };
        m.Agents.Add(new MindAgent { Id = "a0", OneHot = 2.0 });
        m.Agents.Add(new MindAgent { Id = "a1" });
        m.Qubits.Add(new MindQubit { Id = "q0", Agent = "a0", Goal = 0.4 });
        m.Qubits.Add(new MindQubit { Id = "q1", Agent = "a0" });
        m.Qubits.Add(new MindQubit { Id = "q2", Agent = "a0" });
        m.Qubits.Add(new MindQubit { Id = "q3", Agent = "a1", Goal = -0.2 });
        m.Bonds.Add(new MindBond { A = "q0", B = "q3", J = 0.5 });
        return m;
    }

    [Fact]
    public void Emits_Dispositions_And_One_Hot_In_Agent_Order()
    {
        var json = JsonNode.Parse(MindJson.Emit(GroupedMind(), indented: false))!.AsObject();

        Assert.Equal(4, (int?)json["qubits"]);

        var disp = json["dispositions"]!.AsArray();
        Assert.Equal(new[] { 3, 1 }, disp.Select(n => (int)n!).ToArray());

        // one_hot aligns to agents; a1 has none, so the array stops after a0.
        var oneHot = json["one_hot"]!.AsArray();
        Assert.Equal(new[] { 2.0 }, oneHot.Select(n => (double)n!).ToArray());

        // The bond addresses the flat indices of q0 (0) and q3 (3).
        var bond = json["bonds"]!.AsArray()[0]!.AsObject();
        Assert.Equal(0, (int?)bond["a"]);
        Assert.Equal(3, (int?)bond["b"]);
    }

    [Fact]
    public void RoundTrips_The_Partition()
    {
        var back = MindJson.Load(MindJson.Emit(GroupedMind(), indented: false));

        Assert.Equal(4, back.Qubits.Count);
        Assert.Equal(2, back.Agents.Count);

        // The 3-disposition exclusive agent, and the lone one.
        var big = back.AgentOf(back.Qubits[0].Id)!;
        Assert.Equal(3, back.MembersOf(big.Id).Count);
        Assert.Equal(2.0, big.OneHot);

        var lone = back.AgentOf(back.Qubits[3].Id)!;
        Assert.Single(back.MembersOf(lone.Id));
        Assert.Equal(0.0, lone.OneHot);

        Assert.Equal(0.4, back.Qubits[0].Goal);
        Assert.Equal(-0.2, back.Qubits[3].Goal);
        Assert.Single(back.Bonds);

        // Emit is a fixed point after a load -- no structural drift on the cycle.
        Assert.Equal(
            MindJson.Emit(GroupedMind(), indented: false),
            MindJson.Emit(back, indented: false));
    }

    // The IR requires each agent's dispositions to be CONTIGUOUS. When the model's
    // qubit list interleaves two agents, Emit must regroup them (and remap indices).
    [Fact]
    public void Groups_Interleaved_Members_Contiguously()
    {
        var m = new Mind();
        m.Agents.Add(new MindAgent { Id = "a0" });
        m.Agents.Add(new MindAgent { Id = "a1" });
        m.Qubits.Add(new MindQubit { Id = "q0", Agent = "a0" });
        m.Qubits.Add(new MindQubit { Id = "q1", Agent = "a1", Goal = -0.3 });
        m.Qubits.Add(new MindQubit { Id = "q2", Agent = "a0", Goal = 0.7 });

        var json = JsonNode.Parse(MindJson.Emit(m, indented: false))!.AsObject();

        // a0 = {q0, q2} first (its first member q0 leads), then a1 = {q1}.
        Assert.Equal(new[] { 2, 1 }, json["dispositions"]!.AsArray().Select(n => (int)n!).ToArray());

        // q2's goal lands at flat index 1 (a0's second disposition), q1's at index 2.
        var goals = json["goals"]!.AsArray();
        Assert.Equal(1, (int?)goals[0]!["q"]);
        Assert.Equal(0.7, (double?)goals[0]!["field"]);
        Assert.Equal(2, (int?)goals[1]!["q"]);
        Assert.Equal(-0.3, (double?)goals[1]!["field"]);
    }

    // A mind where every qubit is its own single-disposition agent carries no layout,
    // and must emit the plain positional shape -- no dispositions/one_hot keys at all.
    [Fact]
    public void Singletons_Emit_No_Layout()
    {
        var m = new Mind();
        m.Qubits.Add(new MindQubit { Id = "q0", Goal = 0.4 });
        m.Qubits.Add(new MindQubit { Id = "q1" });

        var json = JsonNode.Parse(MindJson.Emit(m, indented: false))!.AsObject();

        Assert.Null(json["dispositions"]);
        Assert.Null(json["one_hot"]);
        Assert.False(m.HasLayout);
    }

    // A multi-disposition agent with NO exclusivity still needs a `dispositions` block
    // (so the read surface addresses it), but no `one_hot`.
    [Fact]
    public void Grouping_Without_Exclusivity_Emits_Dispositions_But_No_One_Hot()
    {
        var m = new Mind();
        m.Agents.Add(new MindAgent { Id = "a0" });
        m.Qubits.Add(new MindQubit { Id = "q0", Agent = "a0" });
        m.Qubits.Add(new MindQubit { Id = "q1", Agent = "a0" });

        var json = JsonNode.Parse(MindJson.Emit(m, indented: false))!.AsObject();

        Assert.Equal(new[] { 2 }, json["dispositions"]!.AsArray().Select(n => (int)n!).ToArray());
        Assert.Null(json["one_hot"]);
    }

    [Fact]
    public void Loads_Agent_Disposition_Addressing()
    {
        // A hand-authored mind that addresses goals/bonds by (agent, disposition)
        // rather than a flat qubit -- both forms must resolve identically.
        const string json = """
            {"schema":"wozzits.mind.ir.v0","qubits":3,"dispositions":[2,1],
             "goals":[{"agent":0,"disposition":1,"field":0.9}],
             "bonds":[{"a_agent":0,"a_disposition":0,"b_agent":1,"b_disposition":0,"j":-0.5}]}
            """;

        var m = MindJson.Load(json);

        // agent 0 = flat {0,1}, so (agent 0, disposition 1) is flat qubit 1.
        Assert.Equal(0.9, m.Qubits[1].Goal);
        Assert.Single(m.Bonds);
        Assert.Equal(m.Qubits[0].Id, m.Bonds[0].A);   // (0,0) -> flat 0
        Assert.Equal(m.Qubits[2].Id, m.Bonds[0].B);   // (1,0) -> flat 2
    }

    // Exclusivity on an agent that has shrunk to one disposition is meaningless and
    // invisible; normalization clears it rather than emit a degenerate one_hot.
    [Fact]
    public void Normalize_Clears_One_Hot_On_A_Shrunk_Agent()
    {
        var m = new Mind();
        m.Agents.Add(new MindAgent { Id = "a0", OneHot = 2.0 });
        m.Qubits.Add(new MindQubit { Id = "q0", Agent = "a0" });   // only one member

        m.NormalizeAgents();

        Assert.Equal(0.0, m.Agents.Single(a => a.Id == "a0").OneHot);
        Assert.False(m.HasLayout);
    }

    [Fact]
    public void Load_Rejects_Bad_Layouts()
    {
        // one_hot without a dispositions layout.
        Assert.Throws<MindFormatException>(() =>
            MindJson.Load("""{"qubits":2,"one_hot":[1.0]}"""));
        // dispositions that do not sum to qubits.
        Assert.Throws<MindFormatException>(() =>
            MindJson.Load("""{"qubits":4,"dispositions":[2,1]}"""));
        // a zero-width disposition block.
        Assert.Throws<MindFormatException>(() =>
            MindJson.Load("""{"qubits":2,"dispositions":[2,0]}"""));
        // more one_hot entries than agents.
        Assert.Throws<MindFormatException>(() =>
            MindJson.Load("""{"qubits":2,"dispositions":[2],"one_hot":[1.0,2.0]}"""));
    }
}
