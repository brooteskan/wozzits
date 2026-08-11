using System.Text.Json.Nodes;
using Wozzits.Editor.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>
/// The editor MATERIALIZES a complete clock/commit block into every mind it writes
/// (MindJson.Emit), so a fresh mind's defaults ARE what lands on disk -- the engine's
/// absent-field fill never runs for an editor mind. Those defaults must therefore
/// equal the engine's AUTHORING defaults (kQuantumAgentDefaultGammaEnd in
/// quantum_agent_behaviors.h, and the absent-field fills in mind_ir.cpp), or an
/// editor mind silently anneals/commits differently than an identically-authored
/// hand-written or scalar one.
///
/// That is exactly #298: gamma_end drifted to 0.0 here while the engine authored
/// 0.5, and there was no editor control to even see it. These pin the editor's copy
/// so it cannot drift again. They can only catch drift on the EDITOR side -- a C#
/// test cannot read the C++ constant -- so the engine authoring defaults remain the
/// reference: if one changes there, update the matching expected value here in the
/// same change. (The library deliberately keeps its own, different struct defaults;
/// these are the AUTHORING copy, not that one.)
/// </summary>
public sealed class MindDefaultsTests
{
    [Fact]
    public void FreshMind_Defaults_Match_The_Engine_Authoring_Defaults()
    {
        var clock = new MindClock();
        Assert.Equal(2.0, clock.GammaStart);    // mind_ir.cpp spec.clock.gamma_start
        Assert.Equal(0.5, clock.GammaEnd);      // kQuantumAgentDefaultGammaEnd (#298)
        Assert.Equal(4.0, clock.AnnealSeconds); // mind_ir.cpp spec.clock.anneal_seconds
        Assert.Equal(1.0, clock.RelaxRate);     // mind_ir.cpp spec.clock.relax_rate

        var commit = new MindCommit();
        Assert.Equal(0.8, commit.Confidence);   // mind_ir.cpp spec.commit.confidence
        Assert.Equal(0.0, commit.Decoherence);  // mind_ir.cpp spec.commit.decoherence_rate
    }

    [Fact]
    public void Emit_Writes_Every_Clock_And_Commit_Field()
    {
        // A materialized file must carry the FULL clock/commit block. If a future
        // knob is added to the model but not emitted, the engine's absent-field
        // default governs it instead of the editor's -- reopening the drift for
        // that field. This fails loudly the moment emit stops covering one.
        var m = new Mind();
        m.Qubits.Add(new MindQubit { Id = "q0" });
        var json = JsonNode.Parse(MindJson.Emit(m, indented: false))!.AsObject();

        var clock = json["clock"]!.AsObject();
        Assert.Equal(2.0, (double?)clock["gamma_start"]);
        Assert.Equal(0.5, (double?)clock["gamma_end"]);
        Assert.Equal(4.0, (double?)clock["anneal_seconds"]);
        Assert.Equal(1.0, (double?)clock["relax_rate"]);

        var commit = json["commit"]!.AsObject();
        Assert.Equal(0.8, (double?)commit["confidence"]);
        Assert.Equal(0.0, (double?)commit["decoherence"]);
    }
}
