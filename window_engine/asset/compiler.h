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
#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

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

struct InputPort {
    std::string_view     name;   // "base", "target", "source_file"  (static storage)
    wz::asset::AssetType type;
};

// closed, bounded set of editable widget kinds → enum (exhaustiveness-checked, like blend mode)
enum class ParamType : uint8_t {
    Bool, Int, Float, Float3, Color, String, FilePath, Enum
};

struct ParamDecl {
    std::string_view name;          // "tau", "iterations", "source_path"
    ParamType        type;
    std::string_view label = {};    // optional; defaults to name
    double           default_num = 0;   // Bool(0/1) / Int / Float
    std::string_view default_str = {};  // String / FilePath
    double           min = 0, max = 0;  // slider range when max > min
    std::span<const std::string_view> options = {};  // Enum choices
};

using ParamValue = std::variant<
    bool,
    int64_t,
    double,
    std::array<float, 3>,
    std::string>;

struct ParamBlock {
    std::unordered_map<std::string, ParamValue> values;

    template<class T>
    T get(std::string_view name, T fallback) const {
        const auto it = values.find(std::string(name));
        if (it == values.end()) {
            return fallback;
        }

        if constexpr (std::is_same_v<T, bool>) {
            if (const auto* value = std::get_if<bool>(&it->second)) {
                return *value;
            }
            if (const auto* value = std::get_if<int64_t>(&it->second)) {
                return *value != 0;
            }
        }
        else if constexpr (std::is_same_v<T, std::string_view>) {
            if (const auto* text = std::get_if<std::string>(&it->second)) {
                return std::string_view(*text);
            }
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            if (const auto* text = std::get_if<std::string>(&it->second)) {
                return *text;
            }
        }
        else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
            if (const auto* value =
                    std::get_if<std::array<float, 3>>(&it->second))
            {
                return *value;
            }
        }
        else if constexpr (
            std::is_integral_v<T> && !std::is_same_v<T, bool>)
        {
            if (const auto* value = std::get_if<int64_t>(&it->second)) {
                return static_cast<T>(*value);
            }
            if (const auto* value = std::get_if<double>(&it->second)) {
                return static_cast<T>(*value);
            }
        }
        else if constexpr (std::is_floating_point_v<T>) {
            if (const auto* value = std::get_if<double>(&it->second)) {
                return static_cast<T>(*value);
            }
            if (const auto* value = std::get_if<int64_t>(&it->second)) {
                return static_cast<T>(*value);
            }
        }

        return fallback;
    }
};

struct AssetCompiler {
    SchemaID                input_schema;
    AssetType               output_type;
    std::vector<InputPort>  input_ports;   
    std::vector<ParamDecl>  parameters;
    CompileFn               compile;
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
    using CompilerTable =
        std::unordered_map<CompilerKey, AssetCompiler, CompilerKeyHash>;

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

    const CompilerTable& compilers() const { return table_; }

private:
    CompilerTable table_;
};

} // namespace wz::asset
