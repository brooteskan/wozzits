#pragma once

// Design note only.
//
// This header intentionally declares no engine API. It preserves early notes for
// possible scene-level maintenance passes without promising a concrete Scene type
// or callable functions. Production code should use scene_graph.h, scene_ecs.h,
// and scene/compile/* until these ideas are promoted into implemented APIs.

namespace wz::scene::design_notes
{
    // Potential future scene maintenance passes:
    // - classify static, animated, and collision-relevant nodes
    // - build linear update orders for dirty dynamic subtrees
    // - update world transforms from dirty roots
    // - compute world-space bounds for culling and collision broad phase
    // - optimize hierarchy layout for traversal locality
    // - rebuild static/dynamic partitions after structural edits
}
