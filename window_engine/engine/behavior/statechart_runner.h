#pragma once

// engine/behavior/statechart_runner.h
//
// The INTERPRETER back-end for cognition-behavior statecharts: a builtin behavior
// module that loads a compiled IR (statechart_ir.h) and runs the S3 frame
// algorithm each frame. It is a GENERIC actuator -- it reads agents and drives the
// world through the SAME ABI helpers a plugin uses (wz_agent_decision_at,
// wz_write_set_local_scale, wz_set_agent_goal, wz_rearm_agent, ...), so it is a
// faithful oracle for the C++ emitter that will lower the identical IR to a plugin.
//
// Mirrors quantum_agent's split: a process-wide store holds the non-POD Runtime,
// the behavior's trivially-copyable instance state holds only a handle.

#include <engine/behavior/behavior_plugin_abi.h>
#include <engine/behavior/statechart_ir.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wz::engine::behavior
{
    // Per-binding POD instance state (trivially copyable; preserved across reloads).
    struct StatechartRunnerState
    {
        uint64_t handle = 0;   // StatechartRunnerStore handle (0 = not built)
    };

    class StatechartRunnerStore
    {
    public:
        // Parse-or-fetch a chart by name (cached; many instances share one parse).
        // Returns nullptr if the IR text is malformed (logs nothing here -- the
        // caller reports; keep the store side-effect-light).
        const statechart::Chart* load(
            const std::string& name, const std::string& ir_text);

        // Build a fresh Runtime for `chart` (initial states, timers, commit-refs).
        uint64_t create(const statechart::Chart* chart);

        // Run one frame: pure pass -> per-region control step -> continuous effects.
        void tick(
            uint64_t handle,
            const WzBehaviorFrameFacts* facts,
            const WzBehaviorEvent* event);

        bool destroy(uint64_t handle);
        std::size_t retain(const std::vector<uint64_t>& live);

    private:
        struct Runtime
        {
            const statechart::Chart* chart = nullptr;
            std::vector<uint16_t> active_state;   // per region
            std::vector<float>    time_in_state;  // per region (for `after`)
            // (agent,slot) pairs referenced by commit triggers + their last value,
            // for rising-edge detection.
            std::vector<std::pair<uint16_t, uint16_t>> commit_refs;
            std::vector<int8_t>   last_committed;  // parallel to commit_refs
            std::vector<double>   pure_out;        // scratch, recomputed each tick
        };

        std::unordered_map<std::string, statechart::Chart> charts_;
        std::unordered_map<uint64_t, Runtime> runtimes_;
        uint64_t next_ = 1;
    };

    StatechartRunnerStore& statechart_runner_store();

    // Register the "statechart_runner" builtin module (self.start + frame.update).
    uint8_t register_statechart_runner_behaviors(WzBehaviorPluginApi* api);
}
