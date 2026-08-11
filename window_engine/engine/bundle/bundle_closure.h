#pragma once

// engine/bundle/bundle_closure.h
//
// Resource-closure computation for the standalone bundle exporter (issue #295,
// Seam 3.1). Given a project's asset graph, enumerate every filesystem SOURCE it
// references and classify each Copy vs Strip for a SEALED bundle:
//
//   Copy  — the file's bytes are read/compiled at LOAD (e.g. .hlsl shader source
//           compiled every run, or a wav directory the audio importer walks), so
//           it must ship in the bundle.
//   Strip — the file is consumed only to produce an asset the sealed baked cache
//           already serves (e.g. the Gaea .r32 -> ScalarField, the .glb -> Mesh).
//           At runtime a cache hit on the product prunes the source's subtree, so
//           the heavy source is never read and is omitted from the bundle.
//
// The classifier is PURE over the graph topology plus the static disk-cache
// predicate (is_disk_cacheable): it resolves paths but never opens them, runs no
// compiler, and needs no device — so it is headlessly unit-testable. The strip
// rule mirrors the runtime resolve exactly (resolve_all_cached resolves graph
// sinks with CachePreferred; a cache hit prunes prerequisites), by descending
// from the sinks and stopping at every cache-served node.

#include <asset/draft.h>
#include <file/filesystem.h>

#include <cstdint>
#include <vector>

namespace wz::engine::bundle
{
    enum class BundleFileDisposition : uint8_t
    {
        Copy,   // read at load -> must ship
        Strip,  // served by the sealed cache -> omit the heavy source
    };

    // One filesystem source a graph node references. `resolved_path` is
    // `authored_path` made absolute against the project resource root (absolute
    // authored paths pass through), matching how the carriers resolve at runtime.
    struct BundleSourceRef
    {
        wz::asset::AssetGraphDraftNodeId node =
            wz::asset::INVALID_ASSET_GRAPH_DRAFT_NODE;
        wz::asset::SchemaID schema{};
        wz::asset::AssetType type = wz::asset::AssetType::Unknown;
        wz::fs::Path authored_path;  // as stored in the graph (quote-trimmed)
        wz::fs::Path resolved_path;  // absolute (joined vs resource_root)
        bool is_directory = false;   // audio clip-bank directory (else a file)
        bool recursive = false;      // directory recursion (audio dirs only)
        BundleFileDisposition disposition = BundleFileDisposition::Copy;
        // Diagnostics: whether the node is demanded by a live (non-cache-served)
        // path, and whether its own product type is served by the sealed cache.
        bool reached = false;
        bool cache_served = false;
    };

    struct BundleClosure
    {
        std::vector<BundleSourceRef> sources;

        // Deduplicated resolved paths to actually ship. A file referenced by both
        // a Copy and a Strip node is Copied (some node still reads it).
        [[nodiscard]] std::vector<wz::fs::Path> copy_paths() const;
        // Deduplicated resolved paths safe to omit (Strip and never also Copy).
        [[nodiscard]] std::vector<wz::fs::Path> strip_paths() const;
    };

    // Walk `graph` and classify every source it references against `resource_root`
    // (the project dir; relative authored paths join it, absolute pass through).
    [[nodiscard]] BundleClosure compute_bundle_closure(
        const wz::asset::AssetGraphDraft& graph,
        const wz::fs::Path& resource_root);
}
