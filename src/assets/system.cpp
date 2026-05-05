#include <asset/system.h>
namespace wz::asset
{


    bool AssetSystem::commit() {
        assert(!committed_ && "commit() already called");

        AssetBuilder builder;

        // Pass 1: add every node (registration order = provisional handle).
        for (auto& e : pending_)
            wz::core::graph::add_node(builder, std::move(e.node));

        // Pass 2: wire prerequisite → dependent edges.
        // Edge direction: prerequisite(A) → dependent(B)
        //   → parents(g, B) == prerequisites of B   (resolved first)
        //   → children(g, A) == dependents of A     (compiled after A)
        for (uint32_t i = 0; i < static_cast<uint32_t>(pending_.size()); ++i) {
            for (const AssetKey& dep_key : pending_[i].dep_keys) {
                auto it = pending_index_.find(dep_key);
                if (it == pending_index_.end()) return false;  // missing dep

                // add_edge(from=prerequisite, to=dependent)
                wz::core::graph::add_edge(
                    builder,
                    static_cast<NodeHandle>(it->second),   // prerequisite
                    static_cast<NodeHandle>(i));            // dependent
            }
        }

        // Build immutable DAG. kahn_topo inside build() rejects cycles.
        auto result = wz::core::graph::build(std::move(builder));
        if (!result.has_value()) return false;   // cycle detected

        storage_ = AssetStorage(std::move(*result));
        index_ = build_asset_index(storage_->dag());
        committed_ = true;

        pending_.clear();
        pending_index_.clear();
        return true;
    }


    Result<ResourceHandle> AssetSystem::resolve(const AssetKey& key) {
        // Resolving before commit is not a crash — the node simply cannot exist
        // in a graph that hasn't been built yet.
        if (!committed_) return ResolveError::NodeNotFound;

        // Fast path: already compiled and cached.
        if (auto h = cache_.lookup(key)) return *h;

        // Locate node in committed DAG.
        const AssetGraph& g = storage_->dag();
        const NodeHandle  nh = find_asset_node(index_, key);
        if (nh == INVALID_ASSET_NODE) return ResolveError::NodeNotFound;

        const AssetNode& node = wz::core::graph::node_data(g, nh);

        // Find the compiler for this (schema, type) pair.
        const AssetCompiler* compiler = registry_.find(node.schema, node.type);
        if (!compiler) return ResolveError::CompilerNotFound;

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
            if (std::holds_alternative<ResolveError>(dep_result))
                return ResolveError::DependencyFailed;

            // Use the post-compile node from compiled_nodes_ so compilers
            // see the live payload (e.g. bytes preserved by a carrier compiler),
            // not the original source-stage data from the DAG.
            dep_nodes.push_back(compiled_nodes_.at(dep_key));
            dep_handles.push_back(std::get<ResourceHandle>(dep_result));
        }

        // Compile.
        AssetNode compiled = compiler->compile(node, dep_nodes, dep_handles);

        // Validate: stage must be Compiled. Payload may be either a
        // ResourceHandle (GPU-backed asset) or vector<uint8_t> (carrier node
        // that carries bytes for its dependents but has no GPU resource itself).
        if (compiled.stage != AssetStage::Compiled)
            return ResolveError::CompileFailed;

        ResourceHandle handle{};
        if (const auto* h = std::get_if<ResourceHandle>(&compiled.payload))
            handle = *h;
        // Carrier nodes (bytes payload) legitimately have no handle — that is fine.

        // Store the compiled node so dependents can read its payload.
        compiled_nodes_.emplace(key, compiled);

        cache_.store(key, handle);
        return handle;
    }

    uint32_t AssetSystem::resolve_all(
        std::vector<std::pair<AssetKey, ResolveError>>* errors)
    {
        assert(committed_);

        uint32_t ok = 0;
        for (NodeHandle nh : compilation_order(storage_->dag())) {
            const AssetKey& key = wz::core::graph::node_data(storage_->dag(), nh).key;

            if (cache_.contains(key)) { ++ok; continue; }

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

} // namespace wz::asset