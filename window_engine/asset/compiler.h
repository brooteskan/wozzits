#pragma once

// wz/asset/compiler.h
//
// Compiler interface and registry for the Asset System DAG.
//
// A compiler is a pure function:  (input node, resolved deps) → next-stage node
//
// "Pure" means: deterministic, no hidden global state, no I/O side effects.
// Given the same inputs and the same compiler version (reflected in compiler_hash),
// the output must be identical — this invariant is what makes content-addressed
// caching correct.

#include "types.h"
#include <functional>
#include <unordered_map>
#include <span>

namespace wz::asset {

// ─── CompileFn ────────────────────────────────────────────────────────────────
//
//   input        — the node being compiled (Source or Intermediate stage)
//   dep_nodes    — prerequisite AssetNodes as stored in the DAG
//                  (parallel with dep_handles, same ordering as DAG edges)
//   dep_handles  — resolved GPUHandles for each prerequisite
//                  (INVALID_GPU_HANDLE when the prereq is CPU-only)
//
// Returns the next-stage AssetNode. If transitioning to AssetStage::Compiled,
// the returned node's payload must hold a valid GPUHandle.

using CompileFn = std::function<AssetNode(
    const AssetNode&            input,
    std::span<const AssetNode>  dep_nodes,
    std::span<const ResourceHandle>  dep_handles
)>;

// ─── AssetCompiler ────────────────────────────────────────────────────────────

struct AssetCompiler {
    SchemaID  input_schema;   // schema the input node must carry
    AssetType output_type;    // asset type this compiler produces
    CompileFn compile;
};

// ─── CompilerKey / Hash ───────────────────────────────────────────────────────

struct CompilerKey {
    SchemaID  schema;
    AssetType type;
    bool operator==(const CompilerKey&) const = default;
};

struct CompilerKeyHash {
    size_t operator()(const CompilerKey& k) const noexcept {
        size_t h = SchemaIDHash{}(k.schema);
        h ^= std::hash<uint16_t>{}(static_cast<uint16_t>(k.type))
           + 0x9e3779b9ull + (h << 6) + (h >> 2);
        return h;
    }
};

// ─── CompilerRegistry ─────────────────────────────────────────────────────────
// Maps (SchemaID, AssetType) → AssetCompiler.
//
// Intended to be populated during engine startup before the first AssetSystem
// commit(). Thread-safe for concurrent reads once all registrations are done.

class CompilerRegistry {
public:
    // Register (or overwrite) a compiler for its (schema, output_type) pair.
    void register_compiler(AssetCompiler c) {
        table_[{ c.input_schema, c.output_type }] = std::move(c);
    }

    // Look up by (schema, desired output type). Returns nullptr on miss.
    const AssetCompiler* find(SchemaID schema, AssetType type) const {
        auto it = table_.find({ schema, type });
        return it != table_.end() ? &it->second : nullptr;
    }

    bool has(SchemaID schema, AssetType type) const {
        return find(schema, type) != nullptr;
    }

    uint32_t size() const { return static_cast<uint32_t>(table_.size()); }

private:
    std::unordered_map<CompilerKey, AssetCompiler, CompilerKeyHash> table_;
};

} // namespace wz::asset
