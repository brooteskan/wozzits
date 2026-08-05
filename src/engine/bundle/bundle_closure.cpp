// src/engine/bundle/bundle_closure.cpp

#include <engine/bundle/bundle_closure.h>

#include <engine/assets/engine_asset_library_internal.h>  // internal::FileSourceDesc
#include <engine/assets/engine_disk_cache_provider.h>      // is_disk_cacheable
#include <engine/assets/schema_ids.h>

#include <any>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wz::engine::bundle
{
    namespace
    {
        using wz::asset::AssetGraphDraft;
        using wz::asset::AssetGraphDraftNode;
        using wz::asset::AssetGraphDraftNodeId;
        using wz::asset::AssetGraphDraftNodeState;

        // Verbatim copy of the file carrier's path normaliser
        // (engine_asset_library_file_carriers.cpp::trim_quoted_path). It MUST stay
        // byte-identical: the closure resolves the SAME path the carrier reads at
        // runtime, so any divergence ships or omits the wrong file. The
        // bundle_closure tests exercise the quote/whitespace cases to catch drift.
        std::string trim_quoted_path(std::string_view raw)
        {
            while (!raw.empty()
                && std::isspace(static_cast<unsigned char>(raw.front())))
            {
                raw.remove_prefix(1);
            }
            while (!raw.empty()
                && std::isspace(static_cast<unsigned char>(raw.back())))
            {
                raw.remove_suffix(1);
            }
            if (raw.size() >= 2
                && ((raw.front() == '"' && raw.back() == '"')
                    || (raw.front() == '\'' && raw.back() == '\'')))
            {
                raw.remove_prefix(1);
                raw.remove_suffix(1);
            }
            else {
                const size_t first_quote = raw.find('"');
                const size_t last_quote = raw.rfind('"');
                if (first_quote != std::string_view::npos
                    && last_quote != first_quote)
                {
                    const std::string embedded{
                        raw.substr(first_quote + 1u,
                                   last_quote - first_quote - 1u) };
                    if (wz::fs::is_absolute(embedded)) {
                        return embedded;
                    }
                }
            }
            return std::string(raw);
        }

        // The audio directory importer strips only a single surrounding pair of
        // double quotes (audio_clip_bank_compilers.cpp), not the carrier's fuller
        // normalisation. Mirror it so a wav directory resolves identically.
        std::string strip_surrounding_quotes(std::string_view raw)
        {
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                raw.remove_prefix(1);
                raw.remove_suffix(1);
            }
            return std::string(raw);
        }

        bool is_file_carrier_schema(wz::asset::SchemaID schema)
        {
            using namespace wz::engine::assets;
            return schema == kRawFileSchema
                || schema == kTextFileSchema
                || schema == kHLSLFileSchema
                || schema == kBinaryBlobSchema
                || schema == kImportedSourceFileSchema
                || schema == kCustomBinaryFileSchema
                || schema == kCSVFileSchema;
        }

        // The authored source path a carrier node stores: FileSourceDesc.full_path,
        // or the ParamBlock "source_path" param — matching compile_file_byte_carrier.
        std::string carrier_source_path(const wz::asset::AssetNode& node)
        {
            if (const auto* file =
                    std::any_cast<wz::engine::assets::internal::FileSourceDesc>(
                        &node.meta))
            {
                return std::string(file->full_path);
            }
            if (const auto* params =
                    std::any_cast<wz::asset::ParamBlock>(&node.meta))
            {
                return params->get<std::string>("source_path", {});
            }
            return {};
        }

        wz::fs::Path resolve_against_root(
            const wz::fs::Path& resource_root,
            const std::string& path)
        {
            // Relative paths join the project root; absolute pass through; an empty
            // root leaves the path as-is (matches the carriers' runtime behavior).
            if (!resource_root.empty() && !wz::fs::is_absolute(path)) {
                return wz::fs::join(resource_root, path);
            }
            return path;
        }

        std::vector<wz::fs::Path> unique_paths_with_disposition(
            const std::vector<BundleSourceRef>& sources,
            BundleFileDisposition want,
            const std::unordered_set<std::string>* exclude)
        {
            std::vector<wz::fs::Path> out;
            std::unordered_set<std::string> seen;
            for (const BundleSourceRef& ref : sources) {
                if (ref.disposition != want) {
                    continue;
                }
                const std::string key = ref.resolved_path;
                if (exclude && exclude->count(key) != 0u) {
                    continue;
                }
                if (seen.insert(key).second) {
                    out.push_back(ref.resolved_path);
                }
            }
            return out;
        }
    }

    std::vector<wz::fs::Path> BundleClosure::copy_paths() const
    {
        return unique_paths_with_disposition(
            sources, BundleFileDisposition::Copy, nullptr);
    }

    std::vector<wz::fs::Path> BundleClosure::strip_paths() const
    {
        // A path that ANY node reads (Copy) must never be reported strippable,
        // even if another node treats it as cache-served — some consumer still
        // needs the bytes at load.
        std::unordered_set<std::string> copy_keys;
        for (const BundleSourceRef& ref : sources) {
            if (ref.disposition == BundleFileDisposition::Copy) {
                copy_keys.insert(std::string(ref.resolved_path));
            }
        }
        return unique_paths_with_disposition(
            sources, BundleFileDisposition::Strip, &copy_keys);
    }

    BundleClosure compute_bundle_closure(
        const AssetGraphDraft& graph,
        const wz::fs::Path& resource_root)
    {
        // Live (non-deleted) nodes, indexed by draft id.
        std::unordered_map<AssetGraphDraftNodeId, const AssetGraphDraftNode*> live;
        live.reserve(graph.nodes.size());
        for (const AssetGraphDraftNode& node : graph.nodes) {
            if (node.state != AssetGraphDraftNodeState::Deleted) {
                live.emplace(node.id, &node);
            }
        }

        // Prerequisite adjacency (edge.from is a prereq of edge.to) and the set of
        // nodes that have a consumer. Both endpoints must be live.
        std::unordered_map<AssetGraphDraftNodeId, std::vector<AssetGraphDraftNodeId>>
            prereqs;
        std::unordered_set<AssetGraphDraftNodeId> has_consumer;
        for (const wz::asset::AssetGraphDraftEdge& edge : graph.edges) {
            if (live.find(edge.from) == live.end()
                || live.find(edge.to) == live.end())
            {
                continue;
            }
            prereqs[edge.to].push_back(edge.from);
            has_consumer.insert(edge.from);
        }

        auto cache_served = [](const AssetGraphDraftNode& node) {
            return wz::engine::assets::is_disk_cacheable(
                node.node.schema, node.node.type);
        };

        // Reachability = the runtime resolve descent. resolve_all_cached resolves
        // graph SINKS (no consumer) with CachePreferred; a cache HIT prunes a
        // node's prerequisites. So descend from every sink and STOP at each
        // cache-served node — the nodes we reach are exactly those still compiled
        // from source at load, hence whose sources are actually read.
        std::unordered_set<AssetGraphDraftNodeId> reached;
        std::vector<AssetGraphDraftNodeId> stack;
        for (const auto& [id, node] : live) {
            if (has_consumer.find(id) == has_consumer.end()) {
                if (reached.insert(id).second) {
                    stack.push_back(id);
                }
            }
        }
        while (!stack.empty()) {
            const AssetGraphDraftNodeId id = stack.back();
            stack.pop_back();
            if (cache_served(*live.at(id))) {
                continue;  // served from cache -> prerequisites pruned at runtime
            }
            const auto it = prereqs.find(id);
            if (it == prereqs.end()) {
                continue;
            }
            for (const AssetGraphDraftNodeId prereq : it->second) {
                if (reached.insert(prereq).second) {
                    stack.push_back(prereq);
                }
            }
        }

        BundleClosure closure;
        for (const AssetGraphDraftNode& node : graph.nodes) {
            if (node.state == AssetGraphDraftNodeState::Deleted) {
                continue;
            }

            const wz::asset::SchemaID schema = node.node.schema;
            const bool carrier = is_file_carrier_schema(schema);
            const bool audio =
                schema == wz::engine::assets::kAudioClipBankFromDirectorySchema;
            if (!carrier && !audio) {
                continue;
            }

            BundleSourceRef ref;
            ref.node = node.id;
            ref.schema = schema;
            ref.type = node.node.type;
            ref.cache_served = cache_served(node);
            ref.reached = reached.find(node.id) != reached.end();

            if (carrier) {
                const std::string trimmed =
                    trim_quoted_path(carrier_source_path(node.node));
                if (trimmed.empty()) {
                    continue;  // a carrier with no source path is not a file ref
                }
                ref.authored_path = trimmed;
                ref.resolved_path = resolve_against_root(resource_root, trimmed);
            }
            else {
                const auto* params =
                    std::any_cast<wz::asset::ParamBlock>(&node.node.meta);
                if (!params) {
                    continue;
                }
                const std::string trimmed = strip_surrounding_quotes(
                    params->get<std::string>("directory", {}));
                if (trimmed.empty()) {
                    continue;
                }
                ref.authored_path = trimmed;
                ref.resolved_path = resolve_against_root(resource_root, trimmed);
                ref.is_directory = true;
                ref.recursive = params->get<bool>("recursive", false);
            }

            // A source is read at load iff the node that reads it is resolved from
            // source: reached by a live path AND not itself cache-served. Carriers
            // and the audio importer are never cache-served, so this reduces to
            // "reached"; the full predicate keeps it correct should a future
            // source-reading node ever be cacheable in its own right.
            ref.disposition = (ref.reached && !ref.cache_served)
                ? BundleFileDisposition::Copy
                : BundleFileDisposition::Strip;

            closure.sources.push_back(std::move(ref));
        }
        return closure;
    }
}
