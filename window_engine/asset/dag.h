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
// ── CRITICAL: ownership rule ──────────────────────────────────────────────────
//
//   AssetNode contains non-trivial members (std::vector, std::any, AssetIR).
//   DAGStorage<AssetNode,AssetEdge> stores nodes via placement-new in a raw
//   byte buffer — delete[] does NOT invoke their destructors.
//
//   Therefore: NEVER hold a raw DAGStorage<AssetNode,AssetEdge>.
//              ALWAYS obtain the DAG through asset_build(), which wraps the
//              storage in AssetStorage and guarantees correct destruction.
//
//   Bypassing asset_build() and calling wz::core::graph::build() directly
//   will leak every heap allocation inside every AssetNode in the graph.

#include "types.h"
#include <graph/static_dag.h>
#include <unordered_map>
#include <optional>
#include <span>

namespace wz::asset {

    using AssetEdge = std::monostate;
    using AssetGraph = wz::core::graph::DAG<AssetNode, AssetEdge>;

    // AssetBuilder is exposed so callers can add nodes and edges, but it MUST
    // be consumed through asset_build() — never via wz::core::graph::build().
    using AssetBuilder = wz::core::graph::DAGBuilder<AssetNode, AssetEdge>;

    using NodeHandle = wz::core::graph::NodeHandle;

    // ─── AssetStorage ─────────────────────────────────────────────────────────────
    // RAII owner of the DAG backing buffer.
    //
    // DAGStorage<N,E> lays out node objects via placement new into a raw
    // unique_ptr<byte[]>. When the buffer is freed, delete[] does NOT invoke
    // destructors — correct for trivially-destructible types, but a leak for
    // AssetNode, which contains std::vector, std::any, and AssetIR (all with
    // heap allocations of their own).
    //
    // This wrapper explicitly calls the placement destructor on every AssetNode
    // before the underlying buffer is released, and nulls the span so nothing
    // can access the destroyed objects afterward.

    struct AssetStorage {
        wz::core::graph::DAGStorage<AssetNode, AssetEdge> base;

        ~AssetStorage() {
            // node_data is a span<const AssetNode> into the raw buffer.
            // Cast away const to invoke destructors — we own this memory.
            auto* begin = const_cast<AssetNode*>(base.dag.node_data.data());
            auto* end = begin + base.dag.node_data.size();
            for (AssetNode* p = begin; p != end; ++p)
                p->~AssetNode();

            // Null the span before base releases the buffer, so nothing
            // can access the now-destroyed objects.
            base.dag.node_data = {};
        }

        AssetStorage(const AssetStorage&) = delete;
        AssetStorage& operator=(const AssetStorage&) = delete;

        explicit AssetStorage(wz::core::graph::DAGStorage<AssetNode, AssetEdge> s)
            : base(std::move(s)) {
        }

        AssetStorage(AssetStorage&& other) noexcept
            : base(std::move(other.base))
        {
            // Prevent the moved-from instance from double-destroying nodes.
            other.base.dag.node_data = {};
        }

        AssetStorage& operator=(AssetStorage&& other) noexcept {
            if (this != &other) {
                // Destroy our current nodes before overwriting.
                auto* begin = const_cast<AssetNode*>(base.dag.node_data.data());
                auto* end = begin + base.dag.node_data.size();
                for (AssetNode* p = begin; p != end; ++p)
                    p->~AssetNode();
                base.dag.node_data = {};

                base = std::move(other.base);
                other.base.dag.node_data = {};
            }
            return *this;
        }

        const AssetGraph& dag() const { return base.dag; }
    };

    inline constexpr NodeHandle INVALID_ASSET_NODE = wz::core::graph::INVALID_NODE;

    // ─── asset_build ──────────────────────────────────────────────────────────────
    // The ONLY sanctioned way to finalise an AssetBuilder into a usable DAG.
    //
    // Wraps wz::core::graph::build() and immediately transfers ownership into an
    // AssetStorage, ensuring that AssetNode destructors are always called when the
    // storage is released.  Returns nullopt on cycle detection.
    //
    // Do NOT call wz::core::graph::build() directly on an AssetBuilder — doing so
    // yields a raw DAGStorage<AssetNode,AssetEdge> that will silently leak every
    // heap allocation inside every AssetNode when it is destroyed.

    [[nodiscard]] inline std::optional<AssetStorage> asset_build(AssetBuilder builder) {
        auto result = wz::core::graph::build(std::move(builder));
        if (!result.has_value()) return std::nullopt;
        return AssetStorage(std::move(*result));
    }

    // ─── AssetIndex ───────────────────────────────────────────────────────────────

    using AssetIndex = std::unordered_map<AssetKey, NodeHandle, AssetKeyHash>;

    inline AssetIndex build_asset_index(const AssetGraph& g) {
        AssetIndex idx;
        idx.reserve(wz::core::graph::node_count(g));
        const uint32_t nc = wz::core::graph::node_count(g);
        for (uint32_t i = 0; i < nc; ++i)
            idx.emplace(wz::core::graph::node_data(g, i).key, static_cast<NodeHandle>(i));
        return idx;
    }

    inline NodeHandle find_asset_node(const AssetIndex& idx, const AssetKey& key) {
        auto it = idx.find(key);
        return it != idx.end() ? it->second : INVALID_ASSET_NODE;
    }

    // ─── Dependency queries ───────────────────────────────────────────────────────

    inline std::span<const NodeHandle> prerequisites(const AssetGraph& g, NodeHandle n) {
        return wz::core::graph::parents(g, n);
    }

    inline std::span<const NodeHandle> dependents(const AssetGraph& g, NodeHandle n) {
        return wz::core::graph::children(g, n);
    }

    inline bool is_source_root(const AssetGraph& g, NodeHandle n) {
        return wz::core::graph::is_root(g, n);
    }

    inline bool is_terminal(const AssetGraph& g, NodeHandle n) {
        return wz::core::graph::is_leaf(g, n);
    }

    inline std::span<const NodeHandle> compilation_order(const AssetGraph& g) {
        return wz::core::graph::topo_order(g);
    }

} // namespace wz::asset
