#pragma once

// wz/asset/system.h
//
// AssetSystem — top-level façade for the Asset System DAG.
//
// ── Lifecycle ─────────────────────────────────────────────────────────────────
//
//   1. Registration  — call register_asset() to declare nodes and their deps.
//   2. Commit        — call commit() to build the immutable DAG.
//                      Returns false on cycle detection or missing dep keys.
//   3. Runtime       — call resolve() for demand-driven, cached compilation.
//                      call resolve_all() to eagerly compile every node in
//                      topological order (useful for offline / load-time bake).
//
// ── Thread safety ─────────────────────────────────────────────────────────────
//
//   Phases 1 and 2 are single-threaded.
//   Phase 3 is currently single-threaded; multi-threaded compilation is listed
//   as an open extension in the design doc (§15).

#include "dag.h"
#include "compiler.h"
#include "cache.h"
#include <cassert>
#include <optional>
#include <variant>
#include <vector>

namespace wz::asset {

    // ─── ResolveError ─────────────────────────────────────────────────────────────

    enum class ResolveError : uint8_t {
        NodeNotFound,      // key is absent from the committed DAG
        CompilerNotFound,  // no compiler registered for (schema, type)
        CompileFailed,     // compiler returned an invalid or wrong-stage node
        DependencyFailed,  // at least one prerequisite failed to resolve
    };

    template<typename T>
    using Result = std::variant<T, ResolveError>;

    // ─── AssetSystem ──────────────────────────────────────────────────────────────

    class AssetSystem {
    public:
        explicit AssetSystem(CompilerRegistry registry, uint32_t cache_reserve = 256)
            : registry_(std::move(registry))
            , cache_(cache_reserve)
        {
        }

        // ── Registration phase ────────────────────────────────────────────────────
        // Must be called before commit(). Order of registration does not matter;
        // the DAG builder resolves ordering during commit().
        //
        // dep_keys: AssetKeys of all direct prerequisites for this node.
        //           Every listed key must itself be registered before commit().
        //
        // Returns false (and does nothing) if the node's key is already registered.

        inline bool register_asset(AssetNode node, std::vector<AssetKey> dep_keys = {}) {
            assert(!committed_ && "register_asset() called after commit()");

            if (pending_index_.count(node.key)) return false;

            const uint32_t slot = static_cast<uint32_t>(pending_.size());
            pending_index_.emplace(node.key, slot);
            pending_.push_back({ std::move(node), std::move(dep_keys) });
            return true;
        }

        // ── Commit phase ──────────────────────────────────────────────────────────
        // Freezes the DAG. May only be called once.
        //
        // Returns false if:
        //   • A declared dep_key was never registered (missing node).
        //   • The dependency graph contains a cycle (impossible to resolve).
        //
        // On false, the system remains in the registration phase so the caller can
        // inspect / correct the problem and retry.

        bool commit();

        inline bool committed() const { return committed_; }

        // ── Runtime phase — demand-driven resolve ─────────────────────────────────
        //
        // Recursively resolves all prerequisites, compiles this node, stores the
        // result in the cache, and returns the ResourceHandle.
        //
        // Memoization: a cache hit short-circuits recursion immediately, so calling
        // resolve() on the same key multiple times is cheap after the first call.
        //
        // The recursion depth is bounded by the longest dependency chain in the DAG.
        // For very deep graphs, prefer resolve_all() which uses the pre-computed
        // topological order instead of the call stack.

        Result<ResourceHandle> resolve(const AssetKey& key);

        // ── Runtime phase — eager batch resolve ───────────────────────────────────
        //
        // Iterates the pre-computed topological order (prerequisites before
        // dependents) and resolves every node. Nodes already in the cache are
        // skipped. Because topo order guarantees dep-before-dependent, resolve()
        // calls here never recurse — they always find their deps in the cache.
        //
        // errors (optional): receives (key, error) pairs for any nodes that failed.
        // Returns the count of successfully compiled nodes.

        uint32_t resolve_all(
            std::vector<std::pair<AssetKey, ResolveError>>* errors = nullptr);
        


        // ── Queries ───────────────────────────────────────────────────────────────
        //
        // All query methods operate on compiled_nodes_ — they only return nodes
        // that have already been resolved. Call resolve_all() first for a complete
        // result, or resolve() individual keys to populate on demand.
        //
        // Carrier nodes (file sources with no ResourceHandle) are excluded —
        // only nodes with a valid handle are returned.

        struct CompiledAsset {
            AssetKey       key;
            ResourceHandle handle;
            const AssetNode* node;   // pointer into compiled_nodes_ — stable for session
        };

        // All compiled assets of the given type.
        inline std::vector<CompiledAsset> query(AssetType type) const {
            std::vector<CompiledAsset> out;
            for (const auto& [key, node] : compiled_nodes_) {
                if (node.type != type) continue;
                const auto* h = std::get_if<ResourceHandle>(&node.payload);
                if (!h || !h->valid()) continue;
                out.push_back({ key, *h, &node });
            }
            return out;
        }

        // All compiled assets of the given type produced by a specific schema.
        // Useful when multiple schemas produce the same AssetType (e.g. HLSL vs
        // SPIRV shaders both produce AssetType::Shader).
        inline std::vector<CompiledAsset> query(AssetType type, SchemaID schema) const {
            std::vector<CompiledAsset> out;
            for (const auto& [key, node] : compiled_nodes_) {
                if (node.type != type || node.schema != schema) continue;
                const auto* h = std::get_if<ResourceHandle>(&node.payload);
                if (!h || !h->valid()) continue;
                out.push_back({ key, *h, &node });
            }
            return out;
        }

        // Terminal compiled assets — nodes that nothing else depends on.
        // These are the outputs the renderer consumes directly. Carrier nodes
        // (file sources) are excluded even if they happen to be terminal.
        inline std::vector<CompiledAsset> compiled_terminals() const {
            if (!committed_) return {};
            std::vector<CompiledAsset> out;
            const AssetGraph& g = storage_->dag;
            for (const auto& [key, node] : compiled_nodes_) {
                const NodeHandle nh = find_asset_node(index_, key);
                if (nh == INVALID_ASSET_NODE) continue;
                if (!is_terminal(g, nh)) continue;
                const auto* h = std::get_if<ResourceHandle>(&node.payload);
                if (!h || !h->valid()) continue;
                out.push_back({ key, *h, &node });
            }
            return out;
        }

        // Look up a single compiled asset by key without triggering compilation.
        // Returns nullptr if the key has not been resolved yet.
        inline const CompiledAsset* find_compiled(const AssetKey& key) const {
            auto it = compiled_nodes_.find(key);
            if (it == compiled_nodes_.end()) return nullptr;
            const auto* h = std::get_if<ResourceHandle>(&it->second.payload);
            if (!h || !h->valid()) return nullptr;
            // CompiledAsset is not stored — build on demand into a small cache.
            // Since this is a const query, we use a mutable lookup buffer.
            lookup_buf_ = { key, *h, &it->second };
            return &lookup_buf_;
        }

        // ── Accessors ─────────────────────────────────────────────────────────────

        // Returns the committed graph, or nullptr if not yet committed.
        const AssetGraph* graph() const {
            return committed_ ? &storage_->dag : nullptr;
        }

        const AssetIndex& index()    const { return index_; }
        AssetCache& cache() { return cache_; }
        const AssetCache& cache()    const { return cache_; }
        const CompilerRegistry& registry() const { return registry_; }

    private:
        // ── Registration phase state (cleared after commit) ───────────────────────

        struct RegistrationEntry {
            AssetNode            node;
            std::vector<AssetKey> dep_keys;
        };

        std::vector<RegistrationEntry>                        pending_;
        std::unordered_map<AssetKey, uint32_t, AssetKeyHash>  pending_index_;

        // ── Post-commit state ─────────────────────────────────────────────────────

        std::optional<AssetStorage> storage_;
        AssetIndex                  index_;
        bool                        committed_ = false;

        // ── Persistent state ──────────────────────────────────────────────────────

        CompilerRegistry registry_;
        AssetCache       cache_;

        // Stores each node after compilation so its live payload (e.g. preserved
        // bytes in a carrier node) is available to dependents via dep_nodes.
        // Keyed by AssetKey, parallel to the cache.
        std::unordered_map<AssetKey, AssetNode, AssetKeyHash> compiled_nodes_;

        // Scratch buffer for find_compiled() — avoids allocating a CompiledAsset
        // on the heap for single-key lookups.
        mutable CompiledAsset lookup_buf_{};
    };

} // namespace wz::asset
