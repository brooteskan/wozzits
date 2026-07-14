using System.Text.Json.Nodes;

namespace Wozzits.Editor.Statecharts;

// The in-memory AUTHORING model for a cognition-behavior statechart
// (schema "wozzits.statechart.ir.v0"). This mirrors the on-disk id-based JSON
// shape -- NOT the engine's index-based runtime structs. Ops reference their
// inputs by pure-op id, transitions target states by id, effects name agents
// and bindings by id/port. Stable ids (never indices) give the editor stable
// identity for selection/undo/rename and rename-safe cross-layer references;
// the engine's parse_chart resolves ids -> indices when it loads the compiled
// output. See StatechartJson for load/compile.

/// <summary>The schema id every chart carries at the top level.</summary>
public static class StatechartSchema
{
    public const string V0 = "wozzits.statechart.ir.v0";
}

public enum RefKind
{
    Const,
    Op,
}

/// <summary>
/// A value input: a literal constant, or the output of a pure op (by id).
/// Bools are carried as 0/1 in <see cref="Const"/>; <see cref="IsBool"/> records
/// that the literal was authored as a bool so it re-emits as true/false.
/// </summary>
public sealed class ValueRef
{
    public RefKind Kind { get; set; } = RefKind.Const;
    public double Const { get; set; }        // Kind == Const
    public bool IsBool { get; set; }         // emit as true/false rather than 0/1
    public string Op { get; set; } = "";     // Kind == Op -> a PureOp.Id

    public static ValueRef Number(double v) => new() { Kind = RefKind.Const, Const = v };
    public static ValueRef Bool(bool b) => new() { Kind = RefKind.Const, Const = b ? 1 : 0, IsBool = true };
    public static ValueRef FromOp(string id) => new() { Kind = RefKind.Op, Op = id };
}

/// <summary>A B1 entity port, resolved by node name at runtime. `self` is implicit.</summary>
public sealed class Binding
{
    public string Port { get; set; } = "";
    public string Find { get; set; } = "";
    public bool Subtree { get; set; } = true;   // scope: subtree (true) | global (false)
}

/// <summary>
/// Option A: an owned agent is a co-located quantum_agent (host = "self"); a ref
/// reads another entity's agent (host = a binding port). <see cref="Spec"/> is the
/// opaque quantum_agent config, preserved verbatim (the interpreter ignores it; the
/// scene co-locates a quantum_agent built from it, and the C++ emitter consumes it).
/// </summary>
public sealed class AgentDecl
{
    public string Id { get; set; } = "";
    public bool Owned { get; set; } = true;
    public string Host { get; set; } = "self";   // "self" or a Binding.Port
    // A REF (Owned == false) may NAME which quantum_agent on the host node it reads,
    // matched against the behavior's label. Empty = the first quantum_agent found
    // (back-compat). Makes a node hosting several agents unambiguous. Ignored for an
    // owned agent (the chart creates its own from Spec).
    public string AgentName { get; set; } = "";
    public JsonNode? Spec { get; set; }
}

public enum OpKind
{
    Marginal, Committed, Memory,               // agent reads
    Add, Sub, Mul, Min, Max, MulAdd, Clamp01,  // math
    Eq, Lt, Gt, And, Or, Not, Select,          // logic
    Proximity,                                 // sensors
}

/// <summary>A side-effect-free op. Fields used depend on <see cref="Op"/>.</summary>
public sealed class PureOp
{
    public string Id { get; set; } = "";
    public OpKind Op { get; set; }
    public string Agent { get; set; } = "";       // reads
    public int Slot { get; set; }                 // reads: marginal/committed slot, memory qubit
    public string Target { get; set; } = "";      // proximity: a Binding.Port
    public List<ValueRef> Ins { get; } = new();   // math/logic operands
    public ValueRef? Cond { get; set; }           // select
    public ValueRef? A { get; set; }              // select
    public ValueRef? B { get; set; }              // select

    public bool IsRead => Op is OpKind.Marginal or OpKind.Committed or OpKind.Memory;
}

public enum EffectKind
{
    SetGoal, SetDecoherence, Rearm, Reward,   // agent writes (owner-only, R2)
    MeasureAt,                                // non-commuting measurement (mutates; allowed on a ref)
    SetScale, SetVisible, PlaySound,          // actuators
    Call,                                     // a behavior-REGISTERED actuator, by name
}

/// <summary>The authoring kind of a <see cref="CallArg"/>, mirroring the actuator's
/// declared param: a Scalar param takes Const|Op; a Binding param takes Bind; an
/// Agent param takes Agent.</summary>
public enum CallArgKind
{
    Const, Op, Bind, Agent,
}

/// <summary>
/// One argument to a <see cref="EffectKind.Call"/> effect. Which field is live
/// depends on <see cref="Kind"/>: Const/Op are a scalar (literal or pure-op id),
/// Bind is a <see cref="Binding.Port"/>, Agent is an <see cref="AgentDecl.Id"/>.
/// </summary>
public sealed class CallArg
{
    public CallArgKind Kind { get; set; } = CallArgKind.Const;
    public double Const { get; set; }        // Kind == Const
    public bool IsBool { get; set; }         // emit const as true/false rather than 0/1
    public string Op { get; set; } = "";     // Kind == Op    -> a PureOp.Id
    public string Bind { get; set; } = "";   // Kind == Bind  -> a Binding.Port
    public string Agent { get; set; } = "";  // Kind == Agent -> an AgentDecl.Id

    public static CallArg Number(double v) => new() { Kind = CallArgKind.Const, Const = v };
    public static CallArg FromOp(string id) => new() { Kind = CallArgKind.Op, Op = id };
    public static CallArg ToBind(string port) => new() { Kind = CallArgKind.Bind, Bind = port };
    public static CallArg ToAgent(string id) => new() { Kind = CallArgKind.Agent, Agent = id };
}

/// <summary>A side effect. Fields used depend on <see cref="Kind"/>.</summary>
public sealed class Effect
{
    public EffectKind Kind { get; set; }
    public string Agent { get; set; } = "";       // agent-targeted
    public string TargetBind { get; set; } = "";  // actuator target -> a Binding.Port
    public int Slot { get; set; }                 // set_goal slot / reward qubit
    public ValueRef? Value { get; set; }          // set_goal/set_decoherence/set_scale/set_visible value; reward strength
    public bool Toward { get; set; }              // reward: bias toward |0>
    public string Fn { get; set; } = "";          // call: registered actuator name
    public List<CallArg> Args { get; } = new();   // call: resolved args, in order
}

public enum TriggerKind
{
    Commit, After, Guard, Event,
}

public sealed class Trigger
{
    public TriggerKind Kind { get; set; }
    public string Agent { get; set; } = "";      // commit
    public int Slot { get; set; }                // commit
    public int? Outcome { get; set; }            // commit: null = "any", else 0/1
    public double Seconds { get; set; }          // after
    public ValueRef? Cond { get; set; }          // guard
    public string EventName { get; set; } = "";  // event
}

public sealed class Transition
{
    public Trigger Trigger { get; set; } = new();
    public List<Effect> Actions { get; } = new();   // one-shot, in order
    public string Target { get; set; } = "";        // a State.Id
}

public sealed class State
{
    public string Id { get; set; } = "";
    public List<Effect> Do { get; } = new();        // continuous (channel-resolved)
    public List<Effect> Entry { get; } = new();     // one-shot
    public List<Effect> Exit { get; } = new();      // one-shot
    public List<Transition> Transitions { get; } = new();
}

public sealed class Region
{
    public string Id { get; set; } = "";
    public string Initial { get; set; } = "";     // a State.Id
    public List<string> States { get; } = new();   // State.Ids (orthogonal region)
}

public sealed class Chart
{
    public string Schema { get; set; } = StatechartSchema.V0;
    public string Name { get; set; } = "";
    public List<Binding> Bindings { get; } = new();
    public List<AgentDecl> Agents { get; } = new();
    public List<PureOp> Pure { get; } = new();
    public List<Region> Regions { get; } = new();
    public List<State> States { get; } = new();
}
