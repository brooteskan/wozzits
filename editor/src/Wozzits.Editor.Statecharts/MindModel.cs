namespace Wozzits.Editor.Statecharts;

// The in-memory authoring model for a cognition MIND -- a quantum_agent's wave
// function, schema "wozzits.mind.ir.v0". A mind IS a graph: decision qubits are
// NODES (each with a longitudinal goal bias) and the pairwise couplings (bonds)
// between them are EDGES. Plus the coordination backend (chi), the anneal clock,
// the commit policy, and an optional learning-memory register.
//
// This mirrors the engine's cognition::AgentSpec (agent_cognition.h); MindJson
// compiles it to the JSON the engine's parse_mind reads (mind_ir.cpp) and loads it
// back. The IR is POSITIONAL -- qubits are a count, and goals/bonds address qubits
// by INDEX -- so a qubit's Id here is EDITOR-session identity only (stable selection
// + edit-safe bond references), NOT part of the on-disk form; MindJson resolves
// Id <-> index at the boundary. See MindJson for load/emit.

public static class MindSchema
{
    public const string V0 = "wozzits.mind.ir.v0";
}

/// <summary>How the mind's qubits are coordinated -- a classification of chi.</summary>
public enum MindBackend
{
    Exact,    // chi 0: genuine entanglement, small groups, ANY topology
    LoopyBp,  // chi 1: any topology incl. cycles, scales linearly, NO entanglement
    Ttn,      // chi >= 2: larger entangled groups; bonds MUST form a nearest-neighbour chain
}

/// <summary>One decision qubit (a node). <see cref="Goal"/> is a longitudinal bias:
/// &gt; 0 favors the |0&gt; outcome, &lt; 0 favors |1&gt;, 0 = undecided.</summary>
public sealed class MindQubit
{
    public string Id { get; set; } = "";   // editor-session identity (not in the IR)
    public double Goal { get; set; }
}

/// <summary>A pairwise coupling (an edge) between two distinct qubits, by
/// <see cref="MindQubit.Id"/>. <see cref="J"/> &gt; 0 is ferromagnetic (agree),
/// &lt; 0 anti (disagree).</summary>
public sealed class MindBond
{
    public string A { get; set; } = "";
    public string B { get; set; } = "";
    public double J { get; set; }
}

/// <summary>The exploration-&gt;commit anneal sweep + relaxation clock (CognitionClock).</summary>
public sealed class MindClock
{
    public double GammaStart { get; set; } = 2.0;
    public double GammaEnd { get; set; } = 0.0;
    public double AnnealSeconds { get; set; } = 4.0;
    public double RelaxRate { get; set; } = 1.0;
}

/// <summary>When a disposition collapses into a committed decision (CommitPolicy).</summary>
public sealed class MindCommit
{
    public double Confidence { get; set; } = 0.8;
    public double Decoherence { get; set; }
}

public sealed class Mind
{
    public string Schema { get; set; } = MindSchema.V0;
    public string Name { get; set; } = "";
    public List<MindQubit> Qubits { get; } = new();
    public List<MindBond> Bonds { get; } = new();
    public int Chi { get; set; }        // 0 exact / 1 loopy BP / >= 2 TTN bond dim
    public int Memory { get; set; }     // learning-register qubits (0 = none)
    public MindClock Clock { get; } = new();
    public MindCommit Commit { get; } = new();

    /// <summary>The backend family chi classifies into (for the inspector picker).</summary>
    public MindBackend Backend =>
        Chi <= 0 ? MindBackend.Exact : Chi == 1 ? MindBackend.LoopyBp : MindBackend.Ttn;

    public int IndexOfQubit(string id) => Qubits.FindIndex(q => q.Id == id);
}
