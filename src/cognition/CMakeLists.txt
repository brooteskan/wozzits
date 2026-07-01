# wz::engine::cognition -- quantum-agent NPC cognition, its own subsystem tree
# (headers at window_engine/cognition/) mirroring the wz::audio layout. qstate is
# nested (wz::engine::cognition::qstate) because only cognition uses it.
target_sources(window_engine PRIVATE
    qstate/qstate.cpp
    mean_field.cpp
    exact_group.cpp
    anneal.cpp
    commit.cpp
    agent_layout.cpp
    learning.cpp
    exclusivity.cpp
    sequential_goals.cpp
    conditional_policy.cpp
    tree_bp.cpp
    ttn.cpp
    tree_tn.cpp
    coordination.cpp
    cognition_clock.cpp
    agent_cognition.cpp
)
