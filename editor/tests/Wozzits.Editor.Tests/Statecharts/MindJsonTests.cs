using System.Text.Json.Nodes;
using Wozzits.Editor.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>The editor's mind model must emit exactly the positional
/// "wozzits.mind.ir.v0" the engine's parse_mind (mind_ir.cpp) reads, and load it
/// back. These pin the model &lt;-&gt; IR round-trip and the schema shape.</summary>
public sealed class MindJsonTests
{
    // A 3-qubit mind: q0 biased to engage, q0<->q1 anti-coupled, q0<->q2 coupled.
    private static Mind SampleMind()
    {
        var m = new Mind { Name = "tank_mind", Chi = 1, Memory = 2 };
        m.Qubits.Add(new MindQubit { Id = "q0", Goal = 0.4 });
        m.Qubits.Add(new MindQubit { Id = "q1", Goal = 0.0 });
        m.Qubits.Add(new MindQubit { Id = "q2", Goal = -0.25 });
        m.Bonds.Add(new MindBond { A = "q0", B = "q1", J = -0.8 });
        m.Bonds.Add(new MindBond { A = "q0", B = "q2", J = 0.5 });
        m.Clock.GammaStart = 3.0;
        m.Clock.AnnealSeconds = 6.0;
        m.Commit.Confidence = 0.7;
        m.Commit.Decoherence = 0.2;
        return m;
    }

    [Fact]
    public void RoundTrips_Structure_Through_The_Ir()
    {
        var back = MindJson.Load(MindJson.Emit(SampleMind(), indented: false));

        Assert.Equal(3, back.Qubits.Count);
        Assert.Equal(0.4, back.Qubits[0].Goal);
        Assert.Equal(0.0, back.Qubits[1].Goal);
        Assert.Equal(-0.25, back.Qubits[2].Goal);

        Assert.Equal(2, back.Bonds.Count);
        // bonds resolve back to the same qubits positionally (q0<->q1, q0<->q2).
        Assert.Equal(back.Qubits[0].Id, back.Bonds[0].A);
        Assert.Equal(back.Qubits[1].Id, back.Bonds[0].B);
        Assert.Equal(-0.8, back.Bonds[0].J);
        Assert.Equal(back.Qubits[2].Id, back.Bonds[1].B);
        Assert.Equal(0.5, back.Bonds[1].J);

        Assert.Equal(1, back.Chi);
        Assert.Equal(MindBackend.LoopyBp, back.Backend);
        Assert.Equal(2, back.Memory);
        Assert.Equal(3.0, back.Clock.GammaStart);
        Assert.Equal(6.0, back.Clock.AnnealSeconds);
        Assert.Equal(0.7, back.Commit.Confidence);
        Assert.Equal(0.2, back.Commit.Decoherence);
    }

    [Fact]
    public void Emits_The_Engine_Schema_Shape()
    {
        var json = JsonNode.Parse(MindJson.Emit(SampleMind(), indented: false))!.AsObject();
        Assert.Equal("wozzits.mind.ir.v0", (string?)json["schema"]);
        Assert.Equal(3, (int?)json["qubits"]);

        // goals are sparse (q1's zero bias is omitted) and addressed by index.
        var goals = json["goals"]!.AsArray();
        Assert.Equal(2, goals.Count);
        Assert.Equal(0, (int?)goals[0]!["q"]);
        Assert.Equal(0.4, (double?)goals[0]!["field"]);
        Assert.Equal(2, (int?)goals[1]!["q"]);

        // bonds are addressed by index.
        var bonds = json["bonds"]!.AsArray();
        Assert.Equal(0, (int?)bonds[0]!["a"]);
        Assert.Equal(1, (int?)bonds[0]!["b"]);
        Assert.Equal(-0.8, (double?)bonds[0]!["j"]);

        Assert.Equal(1, (int?)json["chi"]);
        Assert.Equal(2, (int?)json["memory"]);
        Assert.NotNull(json["clock"]);
        Assert.Equal(0.7, (double?)json["commit"]!["confidence"]);
    }

    [Fact]
    public void Load_Rejects_Malformed_Minds()
    {
        Assert.Throws<MindFormatException>(() => MindJson.Load("""{"qubits":0}"""));
        Assert.Throws<MindFormatException>(() => MindJson.Load("""{}"""));
        Assert.Throws<MindFormatException>(() =>
            MindJson.Load("""{"qubits":2,"goals":[{"q":5,"field":1}]}"""));
        Assert.Throws<MindFormatException>(() =>
            MindJson.Load("""{"qubits":2,"bonds":[{"a":0,"b":9,"j":1}]}"""));
        Assert.Throws<MindFormatException>(() =>
            MindJson.Load("""{"qubits":2,"bonds":[{"a":1,"b":1,"j":1}]}"""));
        Assert.Throws<MindFormatException>(() => MindJson.Load("not json at all"));
    }

    [Fact]
    public void Validate_Flags_Structural_Problems()
    {
        Assert.Contains(MindJson.Validate(new Mind()), e => e.Contains("at least one qubit"));

        var m = SampleMind();
        m.Bonds.Add(new MindBond { A = "q0", B = "nope", J = 1 });
        Assert.Contains(MindJson.Validate(m), e => e.Contains("not in the mind"));
    }
}
