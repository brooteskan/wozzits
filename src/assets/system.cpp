#include <asset/system.h>

#include <iomanip>
#include <sstream>
#include <string>

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

        const char* dot_fill_color(
            bool source_root,
            bool terminal,
            bool resident) noexcept
        {
            if (terminal) {
                return resident ? "#b7e4c7" : "#d8f3dc";
            }
            if (source_root) {
                return resident ? "#bfdbfe" : "#dbeafe";
            }
            return resident ? "#fde68a" : "#f8fafc";
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
                auto it = registered_index_.find(dep_key);
                if (it == registered_index_.end()) return false;  // missing dep

                // add_edge(from=prerequisite, to=dependent)
                wz::core::graph::add_edge(
                    builder,
                    static_cast<NodeHandle>(it->second),   // prerequisite
                    static_cast<NodeHandle>(i));            // dependent
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
        return true;
    }


    Result<ResourceHandle> AssetSystem::resolve(const AssetKey& key) {
        // Resolving before commit is not a crash — the node simply cannot exist
        // in a graph that hasn't been built yet.
        if (!committed_) return ResolveError::NodeNotFound;

        // Locate node in committed DAG.
        const AssetGraph& g = storage_->dag();
        const NodeHandle  nh = find_asset_node(index_, key);
        if (nh == INVALID_ASSET_NODE) return ResolveError::NodeNotFound;

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
                    if (*compiled_handle == *h)
                        return *h;
                }
                else if (std::holds_alternative<std::vector<uint8_t>>(
                    it->second.payload))
                {
                    if (!h->valid())
                        return *h;
                }
            }

            cache_.evict(key);
            compiled_nodes_.erase(key);
        }

        // Find the compiler for this (schema, type) pair.
        const AssetCompiler* compiler = registry_.find(node.schema, node.type);
        if (!compiler) return ResolveError::CompilerNotFound;

        // This resolve attempt is rebuilding the node. If it fails, stale query
        // results from a previous successful compile must not remain visible.
        compiled_nodes_.erase(key);

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

        if (!(compiled.key == key)
            || compiled.type != node.type
            || !(compiled.schema == node.schema))
            return ResolveError::CompileFailed;

        ResourceHandle handle{};
        if (const auto* h = std::get_if<ResourceHandle>(&compiled.payload)) {
            if (!h->valid())
                return ResolveError::CompileFailed;
            handle = *h;
        }
        else if (!std::holds_alternative<std::vector<uint8_t>>(compiled.payload)) {
            return ResolveError::CompileFailed;
        }
        // Carrier nodes (bytes payload) legitimately have no handle — that is fine.

        // Store the compiled node so dependents can read its payload.
        compiled_nodes_.insert_or_assign(key, std::move(compiled));

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

        for (uint32_t i = 0; i < count; ++i) {
            const AssetNode& node = wz::core::graph::node_data(g, i);
            const auto prereqs = prerequisites(g, i);
            const auto deps = dependents(g, i);
            const bool source_root = prereqs.empty();
            const bool terminal = deps.empty();
            const bool resident = compiled_nodes_.find(node.key) != compiled_nodes_.end();
            const bool cache_hit = cache_.contains(node.key);

            out << "  n" << i << " [label=\"";
            out << "#" << i
                << "\\ntype=" << static_cast<uint16_t>(node.type)
                << " schema=" << schema_hex(node.schema)
                << "\\nstage=" << stage_name(node.stage)
                << " prereq=" << prereqs.size()
                << " dep=" << deps.size()
                << "\\nkey=" << short_hash_hex(node.key.content_hash).substr(0, 12);
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
            out << "\", fillcolor=\""
                << dot_fill_color(source_root, terminal, resident)
                << "\"];\n";
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
