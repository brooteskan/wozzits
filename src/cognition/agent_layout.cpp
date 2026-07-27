#include <cognition/agent_layout.h>

namespace wz::engine::cognition
{
    AgentLayout make_agent_layout(
        const std::vector<uint32_t>& dispositions_per_agent)
    {
        AgentLayout layout;
        layout.dispositions = dispositions_per_agent;
        layout.offset.resize(dispositions_per_agent.size());

        uint32_t acc = 0;
        for (std::size_t i = 0; i < dispositions_per_agent.size(); ++i) {
            layout.offset[i] = acc;
            acc += dispositions_per_agent[i];
        }
        layout.total_qubits = acc;
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
