#include <cognition/agent_layout.h>

namespace wz::engine::cognition
{
    AgentLayout make_agent_layout(
        const std::vector<uint32_t>& dispositions_per_agent)
    {
        AgentLayout layout;
        layout.dispositions = dispositions_per_agent;
        layout.offset.resize(dispositions_per_agent.size());

        // Accumulate in 64 bits and SATURATE rather than wrap. An unchecked
        // uint32 sum let dispositions like {4294967295, 33} wrap to total_qubits
        // == 32, a value small enough to clear every downstream cap
        // (create()'s agent_count > kMaxAgentCount, the layout==agent_count
        // cross-check) while add_one_hot still looped on the raw 4294967295 =>
        // a ~1e11-iteration freeze on self.start. Saturating to UINT32_MAX keeps
        // the disguise from working: the true magnitude reaches create()'s caps,
        // which reject it. (A3-S1, #77 visit 2)
        uint64_t acc = 0;
        for (std::size_t i = 0; i < dispositions_per_agent.size(); ++i) {
            layout.offset[i] = static_cast<uint32_t>(acc);
            acc += dispositions_per_agent[i];
        }
        layout.total_qubits = acc > 0xFFFFFFFFull
            ? 0xFFFFFFFFu
            : static_cast<uint32_t>(acc);
        return layout;
    }

    uint32_t qubit_of(
        const AgentLayout& layout, uint32_t agent, uint32_t disposition)
    {
        if (agent >= layout.offset.size()
            || disposition >= layout.dispositions[agent])
        {
            return kInvalidQubit;
        }
        return layout.offset[agent] + disposition;
    }

    uint32_t agent_count(const AgentLayout& layout)
    {
        return static_cast<uint32_t>(layout.dispositions.size());
    }
}
