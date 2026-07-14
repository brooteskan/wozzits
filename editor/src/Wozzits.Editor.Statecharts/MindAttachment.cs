namespace Wozzits.Editor.Statecharts;

// The config keys the engine's quantum_agent reads for an AUTHORED mind
// (mind_ir.cpp + quantum_agent_behaviors.h). The editor writes these when a mind is
// attached to a node's quantum_agent: `mind_ir` is the compiled graph the engine
// builds the wave function from (it supersedes the scalar config), and `mind` is the
// source mind's name, kept only so the inspector can reflect which mind is attached.
// Parallel to StatechartRunnerAttachment (chart / chart_ir).
public static class QuantumAgentMindAttachment
{
    public const string Module = "quantum_agent";
    public const string ConfigMind = "mind";
    public const string ConfigMindIr = "mind_ir";
}
