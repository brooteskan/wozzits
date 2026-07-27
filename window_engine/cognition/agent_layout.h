#pragma once

// cognition/agent_layout.h
//
// Multi-disposition agents. Until now each agent has been a single qubit (one
// yes/no disposition). A real agent holds several named dispositions
// (flee? cover? alert? advance?), so it owns several qubits. AgentLayout maps
// agents onto a joint register's qubits: agent i owns a contiguous block of
// dispositions[i] qubits, and disposition d of agent i is qubit
// offset[i] + d. Bonds, goals, and reads then address (agent, disposition)
// through qubit_of() and run on the existing qubit-indexed backends unchanged.

#include <cstdint>
#include <vector>

namespace wz::engine::cognition
{
    struct AgentLayout
    {
        std::vector<uint32_t> offset;        // offset[i] = first qubit of agent i
        std::vector<uint32_t> dispositions;  // dispositions[i] = qubit count
        uint32_t total_qubits = 0;
    };

    // Returned by qubit_of for an (agent, disposition) the layout does not
    // contain. Deliberately far out of range so a caller that forgets to check
    // trips the backends' own endpoint guards instead of addressing a real
    // qubit -- see the note on qubit_of.
    inline constexpr uint32_t kInvalidQubit = ~uint32_t{ 0 };

    // Build a layout from each agent's disposition (qubit) count.
    AgentLayout make_agent_layout(
        const std::vector<uint32_t>& dispositions_per_agent);

    // The joint-register qubit for a given (agent, disposition), or
    // kInvalidQubit when `agent` is not in the layout or `disposition` is past
    // that agent's block. Bounds-checked because a layout can legitimately be
    // WIDER than the group it is addressing (a group built for fewer qubits
    // than the layout describes), and an unchecked offset[agent] would be an
    // out-of-bounds vector read while an unchecked sum would silently name some
    // other agent's qubit. Callers that turn the result into a bond or a goal
    // must drop kInvalidQubit rather than pass it down: relax_step applies each
    // bond's ZZ unchecked, and a qubit index >= 64 there is shift UB.
    uint32_t qubit_of(
        const AgentLayout& layout, uint32_t agent, uint32_t disposition);

    uint32_t agent_count(const AgentLayout& layout);
}
