#pragma once

// wz/asset/dag.h
//
// Asset-specific type aliases and helpers layered over wz::core::graph::DAG.
//
// Edge direction convention (design doc §6.2):
//   A → B  means  "B depends on A"  /  "A is a prerequisite of A"
//
// In DAGBuilder terms:  add_edge(builder, A, B)
//   parents(g, B)   == prerequisites of B  — nodes that must compile first
//   children(g, A)  == dependents of A     — nodes that consume A's output
//
// The wrappers below use this domain vocabulary instead of graph vocabulary
// so call sites read as asset-system semantics, not graph theory.

#include "types.h"
#include <graph/static_dag.h>
#include <unordered_map>
#include <span>

namespace wz::asset {

// ─── Type aliases ─────────────────────────────────────────────────────────────
// Edges are structural only — dependency has no payload.

using AssetEdge    = std::monostate;
using AssetGraph   = wz::core::graph::DAG<AssetNode, AssetEdge>;
using AssetStorage = wz::core::graph::DAGStorage<AssetNode, AssetEdge>;
using AssetBuilder = wz::core::graph::DAGBuilder<AssetNode, AssetEdge>;
using NodeHandle   = wz::core::graph::NodeHandle;

inline constexpr NodeHandle INVALID_ASSET_NODE = wz::core::graph::INVALID_NODE;

// ─── AssetIndex ───────────────────────────────────────────────────────────────
// Maps AssetKey → NodeHandle for O(1) post-commit lookups.
// Built once from the committed graph; rebuilt on hot-reload.

using AssetIndex = std::unordered_map<AssetKey, NodeHandle, AssetKeyHash>;

// Build a full index from a committed graph.
inline AssetIndex build_asset_index(const AssetGraph& g) {
    AssetIndex idx;
    idx.reserve(wz::core::graph::node_count(g));
    const uint32_t nc = wz::core::graph::node_count(g);
    for (uint32_t i = 0; i < nc; ++i)
        idx.emplace(wz::core::graph::node_data(g, i).key, static_cast<NodeHandle>(i));
    return idx;
}

// Look up a single node. Returns INVALID_ASSET_NODE if the key is absent.
inline NodeHandle find_asset_node(const AssetIndex& idx, const AssetKey& key) {
    auto it = idx.find(key);
    return it != idx.end() ? it->second : INVALID_ASSET_NODE;
}

// ─── Dependency queries ───────────────────────────────────────────────────────

// Nodes that n depends on — must be compiled before n.
inline std::span<const NodeHandle> prerequisites(const AssetGraph& g, NodeHandle n) {
    return wz::core::graph::parents(g, n);
}

// Nodes that consume n's output — become invalid if n changes.
inline std::span<const NodeHandle> dependents(const AssetGraph& g, NodeHandle n) {
    return wz::core::graph::children(g, n);
}

// True if n has no prerequisites (raw source asset, e.g. a standalone texture).
inline bool is_source_root(const AssetGraph& g, NodeHandle n) {
    return wz::core::graph::is_root(g, n);
}

// True if nothing depends on n (top-level output consumed by the scene system).
inline bool is_terminal(const AssetGraph& g, NodeHandle n) {
    return wz::core::graph::is_leaf(g, n);
}

// Iterate all nodes in prerequisite-first topological order.
// Guaranteed: when node n is visited, all prerequisites of n have been visited.
inline std::span<const NodeHandle> compilation_order(const AssetGraph& g) {
    return wz::core::graph::topo_order(g);
}

} // namespace wz::asset
