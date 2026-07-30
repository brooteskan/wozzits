#pragma once

// engine/behavior/statechart_ir.h
//
// The compiled in-memory form of a cognition-behavior statechart
// (schema "wozzits.statechart.ir.v0"). ONE parser, TWO consumers: the
// statechart_runner interpreter (engine builtin) and the C++ emitter. Keeping
// the parse + structs here (not in the runner) means the emitter reads the
// identical Chart the interpreter runs -- the interpreter stays the oracle.
//
// Two layers (design S3): a PERSISTENT pure-dataflow layer (`pure`, evaluated
// every frame, no side effects) and a CONTROL layer (`regions`/`states`) where
// states GATE effects. See statechart_ir_schema.md for the authored JSON shape.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wz::engine::behavior::statechart
{
    // The one schema id this build reads. parse_chart REQUIRES it: the format is
    // versioned precisely so a later revision can change what a key means, and a
    // parser that ignores the version reads a future chart as if it were this one
    // -- silently and wrongly, which is the failure the `reward` toward->branch
    // rename already had to be defended against by hand. The editor's writer
    // stamps this same string (Wozzits.Editor.Statecharts.StatechartSchema.V0);
    // the pair must be changed together, in both repos, when the format moves.
    inline constexpr std::string_view kChartSchema = "wozzits.statechart.ir.v0";

    // A value input: a literal, or the output of a pure op (by index). Bools are
    // carried as 0/1 in `constant`; truthiness is (value != 0).
    struct Ref
    {
        enum class Kind : uint8_t { Const, Op };
        Kind kind = Kind::Const;
        double constant = 0.0;   // Kind::Const
        uint16_t op = 0;         // Kind::Op -> index into Chart::pure
    };

    // Side-effect-free ops, evaluated in topological order into a scratch buffer.
    enum class OpKind : uint8_t
    {
        Marginal, Committed, Memory,   // agent reads (use `agent`,`slot`)
        Add, Sub, Mul, Min, Max,       // in0,in1
        MulAdd,                        // in0*in1 + in2
        Clamp01,                       // in0
        Eq, Lt, Gt,                    // in0,in1 -> bool
        And, Or, Not,                  // in0(,in1) -> bool
        Select,                        // in0 ? in1 : in2
        // Sensors. Proximity: horizontal distance from self to the target binding
        // (slot holds the target binding index). More (Gaze/Timer/TerrainSlope) TBD.
        Proximity,
        // ReadState: the live value of a behavior-published named scalar on self
        // (`name`), read through facts->get_entity_scalar. The pull twin of an `event`
        // trigger -- lets a guard compare a continuous quantity a behavior tracks
        // (a tank's "damage", "ammo") that no other op can sense. 0 if unpublished.
        ReadState,
    };

    struct PureOp
    {
        OpKind op = OpKind::Marginal;
        uint16_t agent = 0;   // for reads: index into Chart::agents
        uint16_t slot = 0;    // decision slot / memory qubit
        std::string name;     // ReadState: the published scalar name to read on self
        Ref in0, in1, in2;    // fixed 3 inputs cover every v0 op
    };

    enum class EffectKind : uint8_t
    {
        // agent writes (owner-only, R2)
        SetGoal, SetDecoherence, Rearm, Reward, Reshape, MeasureAt,
        // actuators
        SetScale, SetVisible, Move, PlaySound,
        // a behavior-REGISTERED actuator, called by name with resolved args (the
        // open actuator vocabulary -- resolved against the runtime registry in the
        // runner, so parsing stays registry-free and the emitter shares this parser)
        Call,
    };

    // One resolved argument to a Call effect. The kind mirrors the actuator's param
    // schema: Scalar is a const/op Ref; Binding is a bound entity; Agent is an
    // agent's host entity. The runner marshals these into WzActuatorArg at call time.
    struct CallArg
    {
        enum class Kind : uint8_t { Scalar, Binding, Agent };
        Kind kind = Kind::Scalar;
        Ref scalar;            // Kind::Scalar
        uint16_t binding = 0;  // Kind::Binding -> Chart::bindings index
        uint16_t agent = 0;    // Kind::Agent   -> Chart::agents index
    };

    struct Effect
    {
        EffectKind kind = EffectKind::SetScale;
        uint16_t agent = 0;     // agent-targeted effects -> Chart::agents index
        uint16_t binding = 0;   // actuator target -> Chart::bindings index
        uint16_t slot = 0;      // SetGoal/MeasureAt slot / Reward qubit
        Ref value;              // SetGoal/SetDecoherence/SetScale/SetVisible/Reward-strength/MeasureAt-angle
        uint8_t flag = 0;       // Reward: toward (|0> == 1)
        // Kind::Call: the registered actuator name + its resolved args, in order.
        std::string call;
        std::vector<CallArg> call_args;
    };

    enum class TriggerKind : uint8_t { Commit, After, Guard, Event };

    struct Trigger
    {
        TriggerKind kind = TriggerKind::Commit;
        uint16_t agent = 0;     // Commit
        uint16_t slot = 0;      // Commit
        int8_t outcome = -2;    // Commit: -2 = any, else 0/1
        float seconds = 0.0f;   // After
        Ref cond;               // Guard (bool)
        std::string event_name; // Event: a behavior-emitted event name to match
    };

    struct Transition
    {
        Trigger trigger;
        std::vector<Effect> actions;   // one-shot, in order
        uint16_t target = 0;           // index into Chart::states
    };

    struct State
    {
        std::string id;
        std::vector<Effect> doe;       // continuous (channel-resolved)
        std::vector<Effect> entry;     // one-shot
        std::vector<Effect> exit;      // one-shot
        std::vector<Transition> transitions;
    };

    struct Region
    {
        std::string id;
        uint16_t initial = 0;          // index into Chart::states
        std::vector<uint16_t> states;  // indices into Chart::states
    };

    // A B1 entity port, resolved by name at runtime. `self` is implicit and is
    // referenced by the sentinel below rather than a binding row.
    struct Binding
    {
        std::string port;
        std::string find;    // node name to resolve
        bool subtree = true; // true = wz_find_descendant_by_name from self, else global
    };

    // Option A: an owned agent is a co-located quantum_agent binding (host = self);
    // a ref reads another entity's quantum_agent (host = a binding index).
    struct AgentDecl
    {
        std::string id;
        bool owned = true;
        uint16_t host_binding = 0;   // Binding index, or kSelfBinding for `self`
        // A REF (owned=false) may NAME which quantum_agent to read on the host node --
        // matched against the behavior's label. Empty = the first quantum_agent found
        // (back-compat). Disambiguates a node that hosts several agents; makes the
        // reference explicit instead of "grab the first one".
        std::string agent_name;
    };

    inline constexpr uint16_t kSelfBinding = 0xFFFFu;

    struct Chart
    {
        std::string name;
        std::vector<Binding> bindings;
        std::vector<AgentDecl> agents;
        std::vector<PureOp> pure;
        std::vector<Region> regions;
        std::vector<State> states;

        // Resolved during parse (id/port -> index); -1 / npos on miss.
        int index_of_state(std::string_view id) const;
        int index_of_agent(std::string_view id) const;
        int index_of_binding(std::string_view port) const;
    };

    // Parse "wozzits.statechart.ir.v0" JSON text into `out`. Returns false + fills
    // `error` on malformed input or an unresolved id/ref. Shared by the interpreter
    // and the emitter.
    bool parse_chart(const std::string& json_text, Chart& out, std::string& error);
}
