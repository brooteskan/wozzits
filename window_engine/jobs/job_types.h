#pragma once

#include <cstdint>
#include <type_traits>
#include <graph/static_dag.h>

namespace wz::jobs
{
    using NodeHandle = wz::core::graph::NodeHandle;

    inline constexpr NodeHandle INVALID_JOB = wz::core::graph::INVALID_NODE;

    // ---------------------------------------------------------------------------
    // Execution lane — where a node may run
    // ---------------------------------------------------------------------------

    enum class ExecutionLane : uint8_t
    {
        MainThread,
        GeneralWorkers,
        AssetWorkers,
        IO,
        AssetCoordinator,
        GPUOwner,
        LoggerService,
    };

    // ---------------------------------------------------------------------------
    // Job status — belongs to execution state, not to the static topology
    // ---------------------------------------------------------------------------

    enum class JobStatus : uint8_t
    {
        Pending,
        Ready,
        Queued,
        Running,
        Done,
        Skipped,
        Failed,
        Cancelled,
    };

    // ---------------------------------------------------------------------------
    // JobContext — passed to the job function at dispatch time
    // ---------------------------------------------------------------------------

    struct JobContext
    {
        NodeHandle node       = INVALID_JOB;
        void*      node_user  = nullptr; // static per-node config (from JobNode::user)
        void*      frame_user = nullptr; // per-run data (from FrameExecution::bindings)
    };

    using JobFn = void (*)(JobContext&);

    // ---------------------------------------------------------------------------
    // JobNode — static node data; lives in the committed topology
    // ---------------------------------------------------------------------------

    struct JobNode
    {
        const char*   name = nullptr;
        ExecutionLane lane = ExecutionLane::GeneralWorkers;
        JobFn         run  = nullptr;
        void*         user = nullptr; // static per-node config; valid for topology lifetime
    };

    using JobEdge = std::monostate;

    // JobNode is stored via placement-new in a raw DAGStorage byte buffer.
    // DAGStorage does not call node destructors — safe only for trivially
    // destructible types. If JobNode ever gains a std::string, std::function,
    // or any owned resource, add a JobGraphStorage wrapper (see AssetStorage).
    static_assert(
        std::is_trivially_destructible_v<JobNode>,
        "JobGraphTemplate stores JobNode in raw DAGStorage. "
        "If JobNode becomes non-trivial, add a JobGraphStorage wrapper "
        "like AssetStorage that explicitly destroys node_data."
    );

} // namespace wz::jobs
