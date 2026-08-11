namespace Wozzits.Editor.Statecharts;

// The in-memory authoring model for a cognition MIND -- a quantum_agent's wave
// function, schema "wozzits.mind.ir.v0". A mind IS a graph: decision qubits are
// NODES (each with a longitudinal goal bias) and the pairwise couplings (bonds)
// between them are EDGES. Plus the coordination backend (chi), the anneal clock,
// the commit policy, and an optional learning-memory register.
//
// AGENTS group the qubits. Each qubit is one DISPOSITION owned by exactly one agent;
// an agent owning several dispositions is a decision-maker holding a >2-way choice
// (flee | fight | hide), and an agent can be made mutually EXCLUSIVE (one-hot: pick
// exactly one). By default every qubit is its own single-disposition agent, which is
// the model every earlier mind used and emits the plain positional shape below. This
// mirrors the engine's AgentSpec.dispositions_per_agent + one_hot_strength.
//
// This mirrors the engine's cognition::AgentSpec (agent_cognition.h); MindJson
// compiles it to the JSON the engine's parse_mind reads (mind_ir.cpp) and loads it
// back. The IR is POSITIONAL -- qubits are a count, and goals/bonds address qubits
// by INDEX -- so a qubit's Id here is EDITOR-session identity only (stable selection
// + edit-safe bond references), NOT part of the on-disk form; MindJson resolves
// Id <-> index at the boundary. The engine also requires each agent's dispositions to
// occupy a CONTIGUOUS block of indices, so MindJson emits qubits grouped by agent.
// See MindJson for load/emit.

using System.Collections.Generic;
using System.Linq;

public static class MindSchema
{
    public const string V0 = "wozzits.mind.ir.v0";
}

/// <summary>How the mind's qubits are coordinated -- a classification of chi.</summary>
public enum MindBackend
{
    Exact,    // chi 0: genuine entanglement, small groups, ANY topology
    LoopyBp,  // chi 1: any topology incl. cycles, scales linearly, NO entanglement
    // chi >= 2: larger entangled groups, ANY topology. A nearest-neighbour chain routes to
    // the cheap chain specialization (engine TtnChain); every other shape routes to the
    // general graph tensor network (engine GraphTn, ~8x the chain's cost) -- so the name is
    // the chi TIER, not a chain requirement. See build_coordination (agent_cognition.cpp).
    Ttn,
}

/// <summary>A decision-maker that owns one or more dispositions (qubits). By default
/// each qubit is its own agent. <see cref="OneHot"/> is the mutual-exclusion penalty
/// weight: 0 leaves the agent's dispositions independent yes/no bits; &gt; 0 makes them
/// a one-hot choice (exactly one active), and is a strength like any bond -- ~2x the
/// largest goal you expect is a reasonable start. Exclusivity only means anything for
/// an agent owning at least two dispositions (engine exclusivity.h / add_one_hot).</summary>
public sealed class MindAgent
{
    public string Id { get; set; } = "";   // editor-session identity (not in the IR)
    public double OneHot { get; set; }
}

/// <summary>One decision qubit -- a disposition (a node) owned by an agent.
/// <see cref="Goal"/> is a longitudinal bias: &gt; 0 favors the |0&gt; outcome,
/// &lt; 0 favors |1&gt;, 0 = undecided.</summary>
public sealed class MindQubit
{
    public string Id { get; set; } = "";     // editor-session identity (not in the IR)
    public string Agent { get; set; } = "";   // owning MindAgent.Id (normalized to a real one)
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

    /// <summary>The RESIDUAL transverse field the sweep lands on (how much doubt
    /// survives commit). 0.5, NOT the C++ CognitionClock struct's 0.0: these are the
    /// AUTHORING defaults a mind created in the editor starts from and materializes
    /// into its file, so they must match the engine's authoring default
    /// (kQuantumAgentDefaultGammaEnd, quantum_agent_behaviors.h) -- the library
    /// deliberately keeps its own, different struct default (0.0, an undriven test
    /// baseline). This drifted to 0.0 once (#298): with no editor control for it,
    /// every editor mind then annealed fully classical and nobody could see why.
    /// MindDefaultsTests pins each value here so it cannot silently drift again.</summary>
    public double GammaEnd { get; set; } = 0.5;

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
    public List<MindAgent> Agents { get; } = new();
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

    public MindQubit? Qubit(string id) => Qubits.FirstOrDefault(q => q.Id == id);

    public MindAgent? AgentOf(string qubitId)
    {
        var q = Qubit(qubitId);
        return q is null ? null : Agents.FirstOrDefault(a => a.Id == q.Agent);
    }

    /// <summary>The qubits owned by <paramref name="agentId"/>, in <see cref="Qubits"/>
    /// order (which is their disposition order within the agent).</summary>
    public List<MindQubit> MembersOf(string agentId) =>
        Qubits.Where(q => q.Agent == agentId).ToList();

    // ----- the partition, as the engine sees it -------------------------------
    //
    // The engine lays agents out as CONTIGUOUS blocks of qubit indices, so both the
    // emit order and the flat index of a qubit are derived from the agent grouping,
    // not from the raw Qubits list order. Agents appear in the order their FIRST
    // member appears in Qubits, and with every qubit its own agent (the default)
    // that reduces to the plain Qubits order -- so a mind with no real grouping
    // emits byte-identically to the pre-agent shape.

    /// <summary>Agents in emit order: the order each agent's first member appears in
    /// <see cref="Qubits"/>. Assumes a normalized model (see <see cref="NormalizeAgents"/>).</summary>
    public List<MindAgent> AgentOrder()
    {
        var order = new List<MindAgent>();
        var seen = new HashSet<string>();
        foreach (var q in Qubits)
        {
            if (!seen.Add(q.Agent))
            {
                continue;
            }

            var a = Agents.FirstOrDefault(x => x.Id == q.Agent);
            if (a is not null)
            {
                order.Add(a);
            }
        }

        return order;
    }

    /// <summary>Qubits in emit order: each agent's dispositions contiguously, agents in
    /// <see cref="AgentOrder"/>. This is the positional order the IR indices refer to.</summary>
    public List<MindQubit> FlatOrder()
    {
        var order = new List<MindQubit>();
        foreach (var a in AgentOrder())
        {
            order.AddRange(MembersOf(a.Id));
        }

        return order;
    }

    /// <summary>The positional (IR) index of a qubit, or -1 if unknown. This is the
    /// slot an actuator reads -- grouping qubits into agents CHANGES it.</summary>
    public int FlatIndexOf(string qubitId) => FlatOrder().FindIndex(q => q.Id == qubitId);

    /// <summary>Does the mind carry a non-trivial layout (any agent with more than one
    /// disposition, or any exclusivity set)? When false the IR omits dispositions/one_hot
    /// entirely and is the plain positional shape.</summary>
    public bool HasLayout =>
        AgentOrder().Any(a => MembersOf(a.Id).Count > 1 || a.OneHot != 0.0);

    // ----- agent lifecycle -----------------------------------------------------

    /// <summary>Ensure every qubit belongs to a real agent (orphans get a fresh
    /// single-disposition agent) and drop agents that no longer own any qubit. Idempotent;
    /// call after any structural edit and before emit so the partition is always total.</summary>
    public void NormalizeAgents()
    {
        foreach (var q in Qubits)
        {
            if (q.Agent.Length == 0 || Agents.All(a => a.Id != q.Agent))
            {
                var agent = new MindAgent { Id = FreshAgentId() };
                Agents.Add(agent);
                q.Agent = agent.Id;
            }
        }

        Agents.RemoveAll(a => Qubits.All(q => q.Agent != a.Id));

        // Exclusivity means nothing on an agent that no longer owns two dispositions;
        // clear it so a shrunk agent does not emit a degenerate one_hot the author can
        // no longer see.
        foreach (var a in Agents)
        {
            if (a.OneHot != 0.0 && Qubits.Count(q => q.Agent == a.Id) <= 1)
            {
                a.OneHot = 0.0;
            }
        }
    }

    /// <summary>The lowest unused "aN" agent id (distinct from qubit "qN" ids).</summary>
    public string FreshAgentId()
    {
        var used = Agents.Select(a => a.Id).ToHashSet();
        for (int i = 0; ; i++)
        {
            var id = "a" + i;
            if (used.Add(id))
            {
                return id;
            }
        }
    }
}
