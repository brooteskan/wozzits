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
    {}

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
    // result in the cache, and returns the GPUHandle.
    //
    // Memoization: a cache hit short-circuits recursion immediately, so calling
    // resolve() on the same key multiple times is cheap after the first call.
    //
    // The recursion depth is bounded by the longest dependency chain in the DAG.
    // For very deep graphs, prefer resolve_all() which uses the pre-computed
    // topological order instead of the call stack.

    Result<GPUHandle> resolve(const AssetKey& key);

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

    // ── Accessors ─────────────────────────────────────────────────────────────

    // Returns the committed graph, or nullptr if not yet committed.
    inline const AssetGraph* graph() const {
        return committed_ ? &storage_->dag : nullptr;
    }

    const AssetIndex&       index()    const { return index_; }
    AssetCache&             cache()          { return cache_; }
    const AssetCache&       cache()    const { return cache_; }
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
};

} // namespace wz::asset
