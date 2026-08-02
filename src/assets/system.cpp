#include <asset/system.h>

#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace wz::asset
{
    namespace
    {
        std::string short_hash_hex(const Hash& hash)
        {
            std::ostringstream out;
            out << std::hex << std::setfill('0')
                << std::setw(8) << static_cast<uint32_t>(hash.hi >> 32)
                << std::setw(8) << static_cast<uint32_t>(hash.hi)
                << std::setw(8) << static_cast<uint32_t>(hash.lo >> 32)
                << std::setw(8) << static_cast<uint32_t>(hash.lo);
            return out.str();
        }

        std::string schema_hex(SchemaID schema)
        {
            std::ostringstream out;
            out << "0x"
                << std::hex << std::setfill('0') << std::setw(16)
                << schema.value;
            return out.str();
        }

        const char* stage_name(AssetStage stage) noexcept
        {
            switch (stage) {
            case AssetStage::Source:
                return "source";
            case AssetStage::Intermediate:
                return "intermediate";
            case AssetStage::Compiled:
                return "compiled";
            }
            return "unknown";
        }

        const char* residency_intent_name(ResidencyIntent intent) noexcept
        {
            switch (intent) {
            case ResidencyIntent::RuntimeResident:
                return "runtime";
            case ResidencyIntent::EditorResident:
                return "editor";
            case ResidencyIntent::CompileOnly:
                return "compile-only";
            case ResidencyIntent::Transient:
                return "transient";
            }
            return "unknown";
        }

        const char* asset_node_kind_name(AssetNodeKind kind) noexcept
        {
            switch (kind) {
            case AssetNodeKind::Asset:
                return "asset";
            case AssetNodeKind::DemandRoot:
                return "demand-root";
            }
            return "unknown";
        }

        const char* demand_root_name(DemandRoot root) noexcept
        {
            switch (root) {
            case DemandRoot::None:
                return "none";
            case DemandRoot::GPURuntime:
                return "gpu-runtime";
            case DemandRoot::CPURuntime:
                return "cpu-runtime";
            case DemandRoot::Editor:
                return "editor";
            }
            return "unknown";
        }

        const char* dot_fill_color(
            bool demand_root,
            bool source_root,
            bool terminal,
            bool resident) noexcept
        {
            if (demand_root) {
                return "#e9d5ff";
            }
            if (terminal) {
                return resident ? "#b7e4c7" : "#d8f3dc";
            }
            if (source_root) {
                return resident ? "#bfdbfe" : "#dbeafe";
            }
            return resident ? "#fde68a" : "#f8fafc";
        }

        struct DebugGraphCommunities
        {
            std::vector<uint32_t> node_community;
            std::vector<uint32_t> sizes;
        };

        DebugGraphCommunities compute_debug_graph_communities(const AssetGraph& g)
        {
            const uint32_t count = wz::core::graph::node_count(g);
            DebugGraphCommunities result;
            result.node_community.assign(count, UINT32_MAX);

            std::vector<uint32_t> stack;
            for (uint32_t root = 0; root < count; ++root) {
                if (result.node_community[root] != UINT32_MAX) {
                    continue;
                }

                const uint32_t community =
                    static_cast<uint32_t>(result.sizes.size());
                uint32_t size = 0u;
                stack.clear();
                stack.push_back(root);
                result.node_community[root] = community;

                while (!stack.empty()) {
                    const uint32_t node = stack.back();
                    stack.pop_back();
                    ++size;

                    for (const NodeHandle parent : prerequisites(g, node)) {
                        if (result.node_community[parent] == UINT32_MAX) {
                            result.node_community[parent] = community;
                            stack.push_back(parent);
                        }
                    }
                    for (const NodeHandle child : dependents(g, node)) {
                        if (result.node_community[child] == UINT32_MAX) {
                            result.node_community[child] = community;
                            stack.push_back(child);
                        }
                    }
                }

                result.sizes.push_back(size);
            }

            return result;
        }
    }


    bool AssetSystem::commit() {
        AssetBuilder builder;

        // Pass 1: add every node (registration order = provisional handle).
        for (const auto& e : registered_)
            wz::core::graph::add_node(builder, e.node);

        // Pass 2: wire prerequisite → dependent edges.
        // Edge direction: prerequisite(A) → dependent(B)
        //   → parents(g, B) == prerequisites of B   (resolved first)
        //   → children(g, A) == dependents of A     (compiled after A)
        for (uint32_t i = 0; i < static_cast<uint32_t>(registered_.size()); ++i) {
            for (const AssetKey& dep_key : registered_[i].dep_keys) {
                if (dep_key == AssetKey{}) {
                    continue;
                }

                auto it = registered_index_.find(dep_key);
                if (it == registered_index_.end()) return false;  // missing dep

                // add_edge(from=prerequisite, to=dependent).
                // Its return value is load-bearing: it refuses from == to, so a
                // node naming ITSELF as a prerequisite used to have that edge
                // silently dropped -- commit() then succeeded, the compiler saw
                // one fewer dep than the registration declared, and the node's
                // deps_hash folded a prerequisite the committed DAG did not
                // have. A declared dependency that produces no edge is a
                // rejected graph, not a quietly reduced one.
                if (!wz::core::graph::add_edge(
                        builder,
                        static_cast<NodeHandle>(it->second),   // prerequisite
                        static_cast<NodeHandle>(i)))           // dependent
                {
                    return false;
                }
            }
        }

        // Build immutable DAG. asset_build() wraps the raw DAGStorage in
        // AssetStorage so AssetNode destructors are guaranteed to run on release.
        // kahn_topo inside build() rejects cycles — nullopt means a cycle was found.
        auto result = asset_build(std::move(builder));
        if (!result.has_value()) return false;   // cycle detected

        storage_ = std::move(*result);
        index_ = build_asset_index(storage_->dag());
        committed_ = true;

        for (auto it = node_resolve_states_.begin();
             it != node_resolve_states_.end();)
        {
            if (find_asset_node(index_, it->first) == INVALID_ASSET_NODE) {
                it = node_resolve_states_.erase(it);
            }
            else {
                ++it;
            }
        }

        return true;
    }

    bool AssetSystem::deregister_asset(const AssetKey& key)
    {
        const auto it = registered_index_.find(key);
        if (it == registered_index_.end()) {
            return false;
        }

        registered_.erase(registered_.begin() + it->second);

        // Every slot after the erased one shifts down, so the index is rebuilt
        // rather than patched -- registration is not a hot path, and a stale
        // slot number here would silently wire an edge to the wrong node in the
        // next commit().
        registered_index_.clear();
        registered_index_.reserve(registered_.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(registered_.size()); ++i) {
            registered_index_.emplace(registered_[i].node.key, i);
        }

        ++registration_epoch_;
        return true;
    }

    bool AssetSystem::replace_registered_assets(
        std::vector<RegistrationEntry> entries)
    {
        std::unordered_map<AssetKey, uint32_t, AssetKeyHash> index;
        index.reserve(entries.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(entries.size()); ++i) {
            const AssetKey& key = entries[i].node.key;
            if (key == AssetKey{} || index.count(key) != 0u) {
                return false;
            }
            index.emplace(key, i);
        }

        AssetBuilder builder;
        for (const auto& e : entries) {
            wz::core::graph::add_node(builder, e.node);
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(entries.size()); ++i) {
            for (const AssetKey& dep_key : entries[i].dep_keys) {
                if (dep_key == AssetKey{}) {
                    continue;
                }

                auto it = index.find(dep_key);
                if (it == index.end()) {
                    return false;
                }

                // Same contract as commit(): a declared dependency that yields
                // no edge (self-dependency) rejects the replacement rather than
                // committing a graph that is quietly missing it.
                if (!wz::core::graph::add_edge(
                        builder,
                        static_cast<NodeHandle>(it->second),
                        static_cast<NodeHandle>(i)))
                {
                    return false;
                }
            }
        }

        auto result = asset_build(std::move(builder));
        if (!result.has_value()) {
            return false;
        }

        registered_ = std::move(entries);
        registered_index_ = std::move(index);
        ++registration_epoch_;
        storage_ = std::move(*result);
        index_ = build_asset_index(storage_->dag());
        committed_ = true;

        for (auto it = node_resolve_states_.begin();
             it != node_resolve_states_.end();)
        {
            if (find_asset_node(index_, it->first) == INVALID_ASSET_NODE) {
                it = node_resolve_states_.erase(it);
            }
            else {
                ++it;
            }
        }

        return true;
    }

    void AssetSystem::set_node_resolve_pending(const AssetKey& key)
    {
        node_resolve_states_.insert_or_assign(
            key,
            NodeResolveState{
                NodeResolveStatus::Pending,
                std::nullopt,
                std::nullopt });
    }

    void AssetSystem::set_node_resolve_done(
        const AssetKey& key,
        std::optional<uint64_t> compile_duration_us)
    {
        node_resolve_states_.insert_or_assign(
            key,
            NodeResolveState{
                NodeResolveStatus::Done,
                std::nullopt,
                compile_duration_us });
    }

    void AssetSystem::set_node_resolve_failed(
        const AssetKey& key,
        ResolveError error,
        std::optional<uint64_t> compile_duration_us,
        std::string detail)
    {
        node_resolve_states_.insert_or_assign(
            key,
            NodeResolveState{
                NodeResolveStatus::Failed,
                error,
                compile_duration_us,
                std::move(detail) });
    }


    void AssetSystem::emit_resolve_log(
        ResolveLogEvent::Phase phase,
        const AssetNode& node,
        uint64_t duration_us,
        ResolveError error,
        std::string_view detail) const
    {
        if (resolve_log_) {
            resolve_log_(ResolveLogEvent{
                .phase = phase,
                .schema = node.schema,
                .type = node.type,
                .key = node.key,
                .duration_us = duration_us,
                .error = error,
                .detail = detail,
            });
        }
    }

    Result<ResourceHandle> AssetSystem::resolve(const AssetKey& key) {
        auto fail = [&](ResolveError error) -> Result<ResourceHandle> {
            set_node_resolve_failed(key, error);
            return error;
        };

        // Resolving before commit is not a crash — the node simply cannot exist
        // in a graph that hasn't been built yet.
        if (!committed_) return fail(ResolveError::NodeNotFound);

        // Locate node in committed DAG.
        const AssetGraph& g = storage_->dag();
        const NodeHandle  nh = find_asset_node(index_, key);
        if (nh == INVALID_ASSET_NODE) return fail(ResolveError::NodeNotFound);

        const AssetNode& node = wz::core::graph::node_data(g, nh);

        // Fast path: already compiled and cached. The cache is public mutable
        // state, so only trust a cache entry when it has matching compiled-node
        // state for this committed DAG key.
        if (auto h = cache_.lookup(key)) {
            auto it = compiled_nodes_.find(key);
            if (it != compiled_nodes_.end()) {
                if (const auto* compiled_handle =
                    std::get_if<ResourceHandle>(&it->second.payload))
                {
                    if (*compiled_handle == *h) {
                        emit_resolve_log(ResolveLogEvent::Phase::CacheHit, node);
                        set_node_resolve_done(key);
                        return *h;
                    }
                }
                else if (std::holds_alternative<std::vector<uint8_t>>(
                    it->second.payload))
                {
                    if (!h->valid()) {
                        emit_resolve_log(ResolveLogEvent::Phase::CacheHit, node);
                        set_node_resolve_done(key);
                        return *h;
                    }
                }
            }

            cache_.evict(key);
            compiled_nodes_.erase(key);
        }

        // Find the compiler for this (schema, type) pair.
        const AssetCompiler* compiler = registry_.find(node.schema, node.type);
        if (!compiler) {
            emit_resolve_log(
                ResolveLogEvent::Phase::Failed, node, 0,
                ResolveError::CompilerNotFound);
            return fail(ResolveError::CompilerNotFound);
        }

        // This resolve attempt is rebuilding the node. If it fails, stale query
        // results from a previous successful compile must not remain visible.
        compiled_nodes_.erase(key);
        set_node_resolve_pending(key);

        // Recursively resolve all prerequisites.
        // Because we call resolve() on each, they are memoized in the cache
        // before we proceed — no prerequisite is compiled more than once.
        const auto prereqs = prerequisites(g, nh);

        std::vector<AssetNode>  dep_nodes;
        std::vector<ResourceHandle> dep_handles;
        dep_nodes.reserve(prereqs.size());
        dep_handles.reserve(prereqs.size());

        for (NodeHandle ph : prereqs) {
            const AssetKey& dep_key = wz::core::graph::node_data(g, ph).key;

            auto dep_result = resolve(dep_key);
            if (std::holds_alternative<ResolveError>(dep_result)) {
                emit_resolve_log(
                    ResolveLogEvent::Phase::Failed, node, 0,
                    ResolveError::DependencyFailed);
                return fail(ResolveError::DependencyFailed);
            }

            // Use the post-compile node from compiled_nodes_ so compilers
            // see the live payload (e.g. bytes preserved by a carrier compiler),
            // not the original source-stage data from the DAG.
            dep_nodes.push_back(compiled_nodes_.at(dep_key));
            dep_handles.push_back(std::get<ResourceHandle>(dep_result));
        }

        // Compile.
        const auto compile_started = std::chrono::steady_clock::now();
        AssetNode compiled = compiler->compile(node, dep_nodes, dep_handles);
        const uint64_t compile_duration_us =
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - compile_started)
                    .count());

        auto fail_after_compile =
            [&](ResolveError error) -> Result<ResourceHandle>
        {
            // The reason lives on the node the compiler returned (compile_failed_node
            // carries error_detail through), not the original source-stage `node`.
            emit_resolve_log(
                ResolveLogEvent::Phase::Failed, node, compile_duration_us, error,
                compiled.error_detail);
            set_node_resolve_failed(
                key, error, compile_duration_us, compiled.error_detail);
            return error;
        };

        // Validate: stage must be Compiled. Payload may be either a
        // ResourceHandle (GPU-backed asset) or vector<uint8_t> (carrier node
        // that carries bytes for its dependents but has no GPU resource itself).
        if (compiled.stage != AssetStage::Compiled)
            return fail_after_compile(ResolveError::CompileFailed);

        if (!(compiled.key == key)
            || compiled.type != node.type
            || !(compiled.schema == node.schema))
            return fail_after_compile(ResolveError::CompileFailed);

        ResourceHandle handle{};
        if (const auto* h = std::get_if<ResourceHandle>(&compiled.payload)) {
            if (!h->valid())
                return fail_after_compile(ResolveError::CompileFailed);
            handle = *h;
        }
        else if (!std::holds_alternative<std::vector<uint8_t>>(compiled.payload)) {
            return fail_after_compile(ResolveError::CompileFailed);
        }
        // Carrier nodes (bytes payload) legitimately have no handle — that is fine.

        // Store the compiled node so dependents can read its payload.
        compiled_nodes_.insert_or_assign(key, std::move(compiled));

        cache_.store(key, handle);
        emit_resolve_log(
            ResolveLogEvent::Phase::Compiled, node, compile_duration_us);
        set_node_resolve_done(key, compile_duration_us);
        return handle;
    }

    uint32_t AssetSystem::resolve_all(
        std::vector<std::pair<AssetKey, ResolveError>>* errors)
    {
        // Was assert(committed_) alone: a release no-op, and storage_ is an
        // empty optional before the first commit, so a pre-commit call
        // dereferenced it and was UB in exactly the build that ships.
        //
        // Returning 0 rather than keeping the assert makes this agree with both
        // siblings -- resolve() reports NodeNotFound ("resolving before commit
        // is not a crash"), resolve_roots() returns 0 and records NodeNotFound
        // per root. resolve_all() was the only one of the three that treated an
        // uncommitted system as a programming error, and it did so only in
        // debug.
        if (!committed_) {
            return 0;
        }

        uint32_t ok = 0;
        for (NodeHandle nh : compilation_order(storage_->dag())) {
            const AssetNode& node =
                wz::core::graph::node_data(storage_->dag(), nh);
            if (node.kind == AssetNodeKind::DemandRoot) {
                continue;
            }
            const AssetKey& key = node.key;

            auto r = resolve(key);
            if (std::holds_alternative<ResourceHandle>(r)) {
                ++ok;
            }
            else if (errors) {
                errors->emplace_back(key, std::get<ResolveError>(r));
            }
        }
        return ok;
    }

    uint32_t AssetSystem::evict_unregistered()
    {
        uint32_t evicted = 0;
        for (const AssetKey& key : cache_.keys()) {
            if (is_registered(key)) {
                continue;
            }
            cache_.evict(key);
            compiled_nodes_.erase(key);
            ++evicted;
        }
        // Compiled nodes without a cache slot (carrier nodes hold payload but
        // no handle) reconcile against the same live set.
        for (auto it = compiled_nodes_.begin(); it != compiled_nodes_.end();) {
            if (is_registered(it->first)) {
                ++it;
                continue;
            }
            it = compiled_nodes_.erase(it);
            ++evicted;
        }
        return evicted;
    }

    uint32_t AssetSystem::resolve_roots(
        std::span<const AssetKey> roots,
        ResolvePolicy policy,
        ExternalCacheProvider* provider,
        std::vector<std::pair<AssetKey, ResolveError>>* errors)
    {
        if (!committed_) {
            for (const AssetKey& root : roots) {
                set_node_resolve_failed(root, ResolveError::NodeNotFound);
            }
            if (errors) {
                for (const AssetKey& root : roots) {
                    errors->emplace_back(root, ResolveError::NodeNotFound);
                }
            }
            return 0;
        }

        const AssetGraph& g = storage_->dag();
        std::unordered_set<AssetKey, AssetKeyHash> resolved_assets;
        std::unordered_set<AssetKey, AssetKeyHash> error_keys;
        uint32_t ok = 0;

        auto record_error = [&](const AssetKey& key, ResolveError error) {
            set_node_resolve_failed(key, error);
            if (errors && error_keys.insert(key).second) {
                errors->emplace_back(key, error);
            }
        };

        auto valid_cached_handle =
            [&](const AssetNode& node) -> std::optional<ResourceHandle>
        {
            if (policy == ResolvePolicy::ForceRecompile) {
                cache_.evict(node.key);
                compiled_nodes_.erase(node.key);
                set_node_resolve_pending(node.key);
                return std::nullopt;
            }

            if (auto h = cache_.lookup(node.key)) {
                auto it = compiled_nodes_.find(node.key);
                if (it != compiled_nodes_.end()) {
                    if (const auto* compiled_handle =
                        std::get_if<ResourceHandle>(&it->second.payload))
                    {
                        if (*compiled_handle == *h) {
                            set_node_resolve_done(node.key);
                            return *h;
                        }
                    }
                    else if (std::holds_alternative<std::vector<uint8_t>>(
                        it->second.payload))
                    {
                        if (!h->valid()) {
                            set_node_resolve_done(node.key);
                            return *h;
                        }
                    }
                }

                cache_.evict(node.key);
                compiled_nodes_.erase(node.key);
                set_node_resolve_pending(node.key);
            }
            return std::nullopt;
        };

        auto load_external =
            [&](const AssetNode& node) -> std::optional<ResolveError>
        {
            if (policy == ResolvePolicy::ForceRecompile) {
                return std::nullopt;
            }

            if (!provider
                || !provider->can_load(node.schema, node.type, node.key))
            {
                return policy == ResolvePolicy::CacheRequired
                    ? std::optional<ResolveError>{ ResolveError::ExternalCacheMiss }
                    : std::nullopt;
            }

            std::optional<ResourceHandle> loaded =
                provider->load(node.schema, node.type, node.key);
            if (!loaded || !loaded->valid()) {
                return policy == ResolvePolicy::CacheRequired
                    ? std::optional<ResolveError>{
                        ResolveError::ExternalCacheLoadFailed }
                    : std::nullopt;
            }

            AssetNode compiled = node;
            compiled.stage = AssetStage::Compiled;
            compiled.payload = *loaded;
            compiled_nodes_.insert_or_assign(node.key, std::move(compiled));
            cache_.store(node.key, *loaded);
            set_node_resolve_done(node.key);
            return std::nullopt;
        };

        std::function<std::optional<ResolveError>(NodeHandle)> resolve_node =
            [&](NodeHandle nh) -> std::optional<ResolveError>
        {
            const AssetNode& node = wz::core::graph::node_data(g, nh);

            if (node.kind == AssetNodeKind::DemandRoot) {
                for (const NodeHandle prereq : prerequisites(g, nh)) {
                    if (const auto error = resolve_node(prereq)) {
                        set_node_resolve_failed(
                            node.key,
                            ResolveError::DependencyFailed);
                        return ResolveError::DependencyFailed;
                    }
                }
                set_node_resolve_done(node.key);
                return std::nullopt;
            }

            if (resolved_assets.find(node.key) != resolved_assets.end()) {
                return std::nullopt;
            }

            if (valid_cached_handle(node).has_value()) {
                resolved_assets.insert(node.key);
                ++ok;
                return std::nullopt;
            }

            if (const auto external_error = load_external(node)) {
                set_node_resolve_failed(node.key, *external_error);
                return *external_error;
            }

            if (cache_.contains(node.key)) {
                set_node_resolve_done(node.key);
                resolved_assets.insert(node.key);
                ++ok;
                return std::nullopt;
            }

            if (policy == ResolvePolicy::CacheRequired) {
                set_node_resolve_failed(
                    node.key,
                    ResolveError::ExternalCacheMiss);
                return ResolveError::ExternalCacheMiss;
            }

            for (const NodeHandle prereq : prerequisites(g, nh)) {
                if (resolve_node(prereq).has_value()) {
                    set_node_resolve_failed(
                        node.key,
                        ResolveError::DependencyFailed);
                    return ResolveError::DependencyFailed;
                }
            }

            auto resolved = resolve(node.key);
            if (std::holds_alternative<ResolveError>(resolved)) {
                return std::get<ResolveError>(resolved);
            }

            resolved_assets.insert(node.key);
            ++ok;
            return std::nullopt;
        };

        for (const AssetKey& root : roots) {
            const NodeHandle root_node = find_asset_node(index_, root);
            if (root_node == INVALID_ASSET_NODE) {
                record_error(root, ResolveError::NodeNotFound);
                continue;
            }

            if (const auto error = resolve_node(root_node)) {
                record_error(root, *error);
            }
        }

        return ok;
    }

    uint32_t AssetSystem::evict_evictable_not_demanded(
        std::span<const AssetKey> roots)
    {
        if (!committed_) {
            return 0;
        }

        const AssetGraph& g = storage_->dag();
        std::unordered_set<AssetKey, AssetKeyHash> directly_demanded;

        for (const AssetKey& root : roots) {
            const NodeHandle root_node = find_asset_node(index_, root);
            if (root_node == INVALID_ASSET_NODE) {
                continue;
            }

            const AssetNode& node = wz::core::graph::node_data(g, root_node);
            if (node.kind == AssetNodeKind::DemandRoot) {
                for (const NodeHandle prereq : prerequisites(g, root_node)) {
                    const AssetNode& prereq_node =
                        wz::core::graph::node_data(g, prereq);
                    if (prereq_node.kind == AssetNodeKind::Asset) {
                        directly_demanded.insert(prereq_node.key);
                    }
                }
            }
            else {
                directly_demanded.insert(node.key);
            }
        }

        std::vector<AssetKey> evict_keys;
        evict_keys.reserve(compiled_nodes_.size());
        for (const auto& [key, node] : compiled_nodes_) {
            const bool evictable =
                node.residency == ResidencyIntent::CompileOnly
                || node.residency == ResidencyIntent::Transient;
            if (!evictable) {
                continue;
            }
            if (directly_demanded.find(key) != directly_demanded.end()) {
                continue;
            }
            evict_keys.push_back(key);
        }

        for (const AssetKey& key : evict_keys) {
            cache_.evict(key);
            compiled_nodes_.erase(key);
            set_node_resolve_pending(key);
        }

        return static_cast<uint32_t>(evict_keys.size());
    }

    std::string AssetSystem::debug_graph_dot() const
    {
        std::ostringstream out;
        out << "digraph asset_dag {\n";
        out << "  graph [rankdir=LR, compound=true];\n";
        out << "  node [shape=box, style=\"rounded,filled\", fontname=\"Consolas\", fontsize=10];\n";
        out << "  edge [color=\"#64748b\", arrowsize=0.7];\n";
        out << "  labelloc=\"t\";\n";
        out << "  label=\"Asset DAG: prerequisite -> dependent\";\n";

        if (!committed_ || !storage_) {
            out << "  empty [label=\"asset graph not committed\", fillcolor=\"#fee2e2\"];\n";
            out << "}\n";
            return out.str();
        }

        const AssetGraph& g = storage_->dag();
        const uint32_t count = wz::core::graph::node_count(g);
        const DebugGraphCommunities communities =
            compute_debug_graph_communities(g);

        auto emit_node = [&](uint32_t i, const char* indent) {
            const AssetNode& node = wz::core::graph::node_data(g, i);
            const auto prereqs = prerequisites(g, i);
            const auto deps = dependents(g, i);
            const bool source_root = prereqs.empty();
            const bool terminal = deps.empty();
            const bool demand_root =
                node.kind == AssetNodeKind::DemandRoot;
            const bool resident = compiled_nodes_.find(node.key) != compiled_nodes_.end();
            const bool cache_hit = cache_.contains(node.key);

            out << indent << "n" << i << " [label=\"";
            out << "#" << i
                << " community=" << communities.node_community[i]
                << "\\nkind=" << asset_node_kind_name(node.kind)
                << "\\ntype=" << static_cast<uint16_t>(node.type)
                << " schema=" << schema_hex(node.schema)
                << "\\nstage=" << stage_name(node.stage)
                << " residency=" << residency_intent_name(node.residency)
                << " prereq=" << prereqs.size()
                << " dep=" << deps.size()
                << "\\nkey=" << short_hash_hex(node.key.content_hash).substr(0, 12);
            if (demand_root) {
                out << "\\nroot=" << demand_root_name(node.demand_root);
            }
            if (source_root) {
                out << "\\nsource-root";
            }
            if (terminal) {
                out << "\\nterminal";
            }
            if (resident) {
                out << "\\nresident";
            }
            if (cache_hit) {
                out << "\\nasset-cache-hit";
            }
            out << "\", shape=\""
                << (demand_root ? "diamond" : "box")
                << "\", fillcolor=\""
                << dot_fill_color(demand_root, source_root, terminal, resident)
                << "\"];\n";
        };

        for (uint32_t community = 0;
            community < static_cast<uint32_t>(communities.sizes.size());
            ++community)
        {
            out << "  subgraph cluster_" << community << " {\n";
            out << "    label=\"community " << community
                << " (" << communities.sizes[community] << " nodes)\";\n";
            out << "    color=\"#94a3b8\";\n";
            out << "    style=\"rounded\";\n";

            for (uint32_t i = 0; i < count; ++i) {
                if (communities.node_community[i] == community) {
                    emit_node(i, "    ");
                }
            }

            out << "  }\n";
        }

        for (uint32_t i = 0; i < count; ++i) {
            for (const NodeHandle child : dependents(g, i)) {
                out << "  n" << i << " -> n" << child << ";\n";
            }
        }

        out << "}\n";
        return out.str();
    }

} // namespace wz::asset
