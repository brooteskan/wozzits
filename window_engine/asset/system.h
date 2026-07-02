#pragma once

// wz/asset/system.h
//
// AssetSystem — top-level façade for the Asset System DAG.
//
// ── Lifecycle ─────────────────────────────────────────────────────────────────
//
//   1. Registration  — call register_asset() to declare nodes and their deps.
//   2. Commit        — call commit() to build/rebuild the immutable DAG view.
//                      Returns false on cycle detection or missing dep keys.
//   3. Runtime       — call resolve() for demand-driven, cached compilation.
//                      call resolve_all() to eagerly compile every node in
//                      topological order (useful for offline / load-time bake).
//
// Additional nodes may be registered after commit(). A later commit() rebuilds
// the DAG/index from all registered source nodes while preserving compiled cache
// entries by AssetKey.
//
// ── Thread safety ─────────────────────────────────────────────────────────────
//
//   Phases 1 and 2 are single-threaded.
//   Phase 3 is currently single-threaded; multi-threaded compilation is listed
//   as an open extension in the design doc (§15).

#include "dag.h"
#include "compiler.h"
#include "cache.h"
#include "external_cache_provider.h"
#include <cassert>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace wz::asset {

    // ─── ResolveError ─────────────────────────────────────────────────────────────

    enum class ResolveError : uint8_t {
        NodeNotFound,      // key is absent from the committed DAG
        CompilerNotFound,  // no compiler registered for (schema, type)
        CompileFailed,     // compiler returned an invalid or wrong-stage node
        DependencyFailed,  // at least one prerequisite failed to resolve
        ExternalCacheMiss, // required external cache entry was absent
        ExternalCacheLoadFailed, // external cache entry was invalid or failed to load
    };

    // Stable, human-readable name for a ResolveError (for logs / diagnostics).
    [[nodiscard]] constexpr std::string_view resolve_error_name(
        ResolveError error) noexcept
    {
        switch (error) {
        case ResolveError::NodeNotFound:            return "NodeNotFound";
        case ResolveError::CompilerNotFound:        return "CompilerNotFound";
        case ResolveError::CompileFailed:           return "CompileFailed";
        case ResolveError::DependencyFailed:        return "DependencyFailed";
        case ResolveError::ExternalCacheMiss:       return "ExternalCacheMiss";
        case ResolveError::ExternalCacheLoadFailed: return "ExternalCacheLoadFailed";
        }
        return "Unknown";
    }

    // Structured event emitted once per node visited by resolve() (compiled /
    // cache hit / failed), so a host (e.g. the engine library) can log every
    // compiler run. Optional — only fires when a sink is installed.
    struct ResolveLogEvent {
        enum class Phase : uint8_t { Compiled, CacheHit, Failed };
        Phase        phase = Phase::Compiled;
        SchemaID     schema{};
        AssetType    type = AssetType::Unknown;
        AssetKey     key{};
        uint64_t     duration_us = 0;
        ResolveError error = ResolveError::NodeNotFound;
        // Human-readable failure reason for Phase::Failed, borrowed from the
        // failed node's error_detail. Empty otherwise. A view is safe because the
        // sink is invoked synchronously within resolve(), while the node is alive.
        std::string_view detail;
    };
    using ResolveLogFn = std::function<void(const ResolveLogEvent&)>;

    enum class ResolvePolicy : uint8_t {
        CacheRequired,  // disk-cache miss is an error; do not wake prerequisites
        CachePreferred, // disk-cache hit skips prerequisites; miss resolves from source
        ForceRecompile, // ignore disk cache and resolve from source
    };

    enum class NodeResolveStatus : uint8_t {
        Pending = 0,
        Ready = 1,
        Queued = 2,
        Running = 3,
        Done = 4,
        Skipped = 5,
        Failed = 6,
        Cancelled = 7,
    };

    struct NodeResolveState {
        NodeResolveStatus status = NodeResolveStatus::Pending;
        std::optional<ResolveError> error{};
        std::optional<uint64_t> compile_duration_us{};
        // Human-readable compile-failure reason, captured from the failed node's
        // error_detail. Empty unless status == Failed with a reason.
        std::string detail;
    };

    template<typename T>
    using Result = std::variant<T, ResolveError>;

    // ─── AssetSystem ──────────────────────────────────────────────────────────────

    class AssetSystem {
    public:
        struct RegistrationEntry {
            AssetNode            node;
            std::vector<AssetKey> dep_keys;
        };

        explicit AssetSystem(CompilerRegistry registry, uint32_t cache_reserve = 256)
            : registry_(std::move(registry))
            , cache_(cache_reserve)
        {
        }

        // ── Registration phase ────────────────────────────────────────────────────
        // Order of registration does not matter; the DAG builder resolves
        // ordering during commit(). If called after commit(), the new node is
        // staged for the next commit() rebuild and is not visible to resolve()
        // until that rebuild succeeds.
        //
        // dep_keys: AssetKeys of all direct prerequisites for this node.
        //           Empty keys are explicit optional dependency slots and do
        //           not create DAG edges. Every non-empty listed key must
        //           itself be registered before commit().
        //
        // Returns false (and does nothing) if the node's key is already registered.

        inline bool register_asset(AssetNode node, std::vector<AssetKey> dep_keys = {}) {
            if (registered_index_.count(node.key)) return false;

            const uint32_t slot = static_cast<uint32_t>(registered_.size());
            registered_index_.emplace(node.key, slot);
            registered_.push_back({ std::move(node), std::move(dep_keys) });
            ++registration_epoch_;
            return true;
        }

        // Atomically replace the registered authoring DAG and rebuild the
        // committed graph. On failure, the previous registrations and committed
        // graph remain active.
        bool replace_registered_assets(std::vector<RegistrationEntry> entries);

        [[nodiscard]] inline bool is_registered(const AssetKey& key) const
        {
            return registered_index_.count(key) != 0u;
        }

        [[nodiscard]] inline std::span<const RegistrationEntry>
        registered_assets() const
        {
            return registered_;
        }

        [[nodiscard]] inline uint32_t registration_epoch() const noexcept
        {
            return registration_epoch_;
        }

        inline bool register_demand_root(
            DemandRoot root,
            std::vector<AssetKey> dep_keys = {})
        {
            if (root == DemandRoot::None) return false;

            AssetNode node{};
            node.key = make_demand_root_key(root);
            node.type = AssetType::Unknown;
            node.schema = {};
            node.stage = AssetStage::Source;
            node.residency = root == DemandRoot::Editor
                ? ResidencyIntent::EditorResident
                : ResidencyIntent::RuntimeResident;
            node.kind = AssetNodeKind::DemandRoot;
            node.demand_root = root;
            node.payload = std::vector<uint8_t>{};
            return register_asset(std::move(node), std::move(dep_keys));
        }

        // ── Commit phase ──────────────────────────────────────────────────────────
        // Builds or rebuilds the immutable DAG from all registered source nodes.
        //
        // Returns false if:
        //   • A declared dep_key was never registered (missing node).
        //   • The dependency graph contains a cycle (impossible to resolve).
        //
        // On false, the previous committed DAG (if any) remains active so the
        // caller can inspect / correct the problem and retry.

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

        uint32_t evict_evictable_not_demanded(
            std::span<const AssetKey> roots);

        uint32_t resolve_roots(
            std::span<const AssetKey> roots,
            ResolvePolicy policy,
            ExternalCacheProvider* provider = nullptr,
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
            const AssetGraph& g = storage_->dag();
            for (const auto& [key, node] : compiled_nodes_) {
                const NodeHandle nh = find_asset_node(index_, key);
                if (nh == INVALID_ASSET_NODE) continue;
                bool has_asset_dependent = false;
                for (const NodeHandle child : dependents(g, nh)) {
                    if (wz::core::graph::node_data(g, child).kind
                        == AssetNodeKind::Asset)
                    {
                        has_asset_dependent = true;
                        break;
                    }
                }
                if (has_asset_dependent) continue;
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

        // Look up a compiled node by key, including carrier nodes whose payload
        // is bytes rather than a ResourceHandle.
        inline const AssetNode* find_compiled_node(const AssetKey& key) const {
            auto it = compiled_nodes_.find(key);
            if (it == compiled_nodes_.end()) return nullptr;
            return &it->second;
        }

        [[nodiscard]] inline std::optional<NodeResolveState>
        node_resolve_state(const AssetKey& key) const
        {
            auto it = node_resolve_states_.find(key);
            if (it == node_resolve_states_.end()) return std::nullopt;
            return it->second;
        }

        // ── Accessors ─────────────────────────────────────────────────────────────

        // Returns the committed graph, or nullptr if not yet committed.
        const AssetGraph* graph() const {
            return committed_ ? &storage_->dag() : nullptr;
        }

        // Diagnostic Graphviz/DOT view of the committed asset DAG.
        //
        // Edge direction is prerequisite -> dependent. This method does not
        // resolve assets or touch runtime tables; it only reports graph shape
        // plus current resident/cache state.
        [[nodiscard]] std::string debug_graph_dot() const;

        const AssetIndex& index()    const { return index_; }
        AssetCache& cache() { return cache_; }
        const AssetCache& cache()    const { return cache_; }
        const CompilerRegistry& registry() const { return registry_; }

        // Install an optional sink called once per node visited by resolve()
        // (compiled / cache hit / failed). Lets a host log every compiler run.
        void set_resolve_logger(ResolveLogFn fn) { resolve_log_ = std::move(fn); }

    private:
        void emit_resolve_log(
            ResolveLogEvent::Phase phase,
            const AssetNode& node,
            uint64_t duration_us = 0,
            ResolveError error = ResolveError::NodeNotFound,
            std::string_view detail = {}) const;

        // ── Registered source graph state ─────────────────────────────────────────

        std::vector<RegistrationEntry>                        registered_;
        std::unordered_map<AssetKey, uint32_t, AssetKeyHash>  registered_index_;
        uint32_t                                              registration_epoch_ = 0u;

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
        std::unordered_map<AssetKey, NodeResolveState, AssetKeyHash>
            node_resolve_states_;

        ResolveLogFn resolve_log_;

        void set_node_resolve_pending(const AssetKey& key);
        void set_node_resolve_done(
            const AssetKey& key,
            std::optional<uint64_t> compile_duration_us = std::nullopt);
        void set_node_resolve_failed(
            const AssetKey& key,
            ResolveError error,
            std::optional<uint64_t> compile_duration_us = std::nullopt,
            std::string detail = {});

        // Scratch buffer for find_compiled() — avoids allocating a CompiledAsset
        // on the heap for single-key lookups.
        mutable CompiledAsset lookup_buf_{};
    };

} // namespace wz::asset
