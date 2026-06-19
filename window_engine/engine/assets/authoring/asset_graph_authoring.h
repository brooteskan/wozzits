#pragma once

// engine/assets/authoring/asset_graph_authoring.h
//
// UI-agnostic construction of authored asset-graph draft nodes.
//
// Moved out of the imgui scene editor (#183). The editor used to build a node's
// AssetNode (default params, file-carrier source meta, residency/stage) inline
// against an EngineAssetLibrary; that logic is engine-owned authoring behavior,
// not a GUI concern. These verbs take an explicit GraphAuthoringContext — the
// CompilerRegistry plus a path resolver — so they carry no EngineAssetLibrary,
// no filesystem policy, and no imgui in their signatures.
//
// Structural draft edits (connect / remove / disconnect / set-params /
// materialize / commit) already live in asset/draft.h and the EngineAssetLibrary
// commit API and are reused as-is; this header only covers node construction,
// which previously had no engine home.

#include <asset/compiler.h>
#include <asset/draft.h>
#include <asset/types.h>
#include <file/filesystem.h>

#include <functional>
#include <optional>

namespace wz::engine::assets::authoring
{
    // Explicit, GUI-free context the graph-authoring verbs need.
    //   registry      — compiler lookup for the node's (schema, type).
    //   resolve_file  — maps an authored (possibly relative/quoted) source path
    //                   to an absolute filesystem path. EngineAssetLibrary wires
    //                   files().resolve_path; an identity function is acceptable
    //                   in tests that do not exercise file carriers.
    struct GraphAuthoringContext
    {
        const wz::asset::CompilerRegistry& registry;
        std::function<wz::fs::Path(const wz::fs::Path&)> resolve_file;
    };

    // Build a Source AssetNode for (schema, type):
    //   - fills compiler-default parameters (ensure_param_block_defaults);
    //   - file-carrier compilers (those declaring a FilePath parameter) get a
    //     FileSourceDesc meta with resolved full + canonical paths and residency
    //     CompileOnly. When such a compiler is used and `source_file` is given,
    //     a non-empty "source_path" parameter takes precedence over it;
    //   - otherwise the resolved ParamBlock becomes the meta and residency is
    //     EditorResident. A `source_file` passed with a non-file-carrier compiler
    //     is ignored (the param block is the meta).
    // The node key is left empty — key materialization assigns it at commit.
    // A null compiler (unknown schema/type) yields a bare Source node.
    [[nodiscard]] wz::asset::AssetNode make_source_asset_node(
        const GraphAuthoringContext& ctx,
        wz::asset::SchemaID schema,
        wz::asset::AssetType type,
        wz::asset::ParamBlock params = {},
        std::optional<wz::fs::Path> source_file = {});

    // Build the node via make_source_asset_node and add it to the draft as a
    // Created node. Returns the new draft node id, or
    // INVALID_ASSET_GRAPH_DRAFT_NODE if type is Unknown.
    wz::asset::AssetGraphDraftNodeId add_source_asset_node(
        wz::asset::AssetGraphDraft& draft,
        const GraphAuthoringContext& ctx,
        wz::asset::SchemaID schema,
        wz::asset::AssetType type,
        wz::asset::ParamBlock params = {},
        std::optional<wz::fs::Path> source_file = {});
}
