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

    // Build a layout from each agent's disposition (qubit) count.
    AgentLayout make_agent_layout(
        const std::vector<uint32_t>& dispositions_per_agent);

    // The joint-register qubit for a given (agent, disposition).
    uint32_t qubit_of(
        const AgentLayout& layout, uint32_t agent, uint32_t disposition);

    uint32_t agent_count(const AgentLayout& layout);
}
