#pragma once

// engine/editor/asset_graph_schema_registry.h
//
// Builds a device-free CompilerRegistry carrying only the engine's compiler
// SCHEMAS (input ports / parameters / output types) — no GPU device, no asset
// tables, no runnable compile functions. The in-process editor ABI uses it to
// validate asset-graph connections (can_connect / snapshot read input_ports)
// without standing up an EngineAssetLibrary or a GPU device (issues #187 / #184).
//
// The schemas are the SAME declarations make_engine_compiler_registry uses, so
// the two cannot drift: the builder constructs the real registry against
// throwaway CPU stand-ins and a null device, then strips the (now-dangling)
// compile functions before returning. No GPU work runs and no real device is
// created — this header is intentionally gpu-free so callers stay device-free.

#include <asset/compiler.h>

namespace wz::engine::editor
{
    [[nodiscard]] wz::asset::CompilerRegistry build_asset_graph_schema_registry();
}
