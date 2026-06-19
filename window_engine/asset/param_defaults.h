#pragma once

// wz/asset/param_defaults.h
//
// Default-value helpers for compiler ParamDecl / ParamBlock.
//
// UI-agnostic: authoring code (and editor parameter widgets) use these to seed a
// node's parameters from its compiler schema and to keep a stored value's type
// in sync with its declaration. Pure functions over compiler.h types — no
// engine, filesystem, or GUI dependencies.
//
// Moved out of the imgui scene editor as part of #183 (default_param_value /
// param_value_matches_decl / ensure_candidate_parameters).

#include "compiler.h"

#include <array>
#include <string>
#include <variant>

namespace wz::asset {

    // The declared default for a parameter, as a ParamValue of the matching
    // alternative. Float3/Color broadcast default_num to all three components;
    // String/FilePath use default_str.
    [[nodiscard]] inline ParamValue default_param_value(const ParamDecl& decl)
    {
        switch (decl.type) {
        case ParamType::Bool:
            return decl.default_num != 0.0;
        case ParamType::Int:
        case ParamType::Enum:
            return static_cast<int64_t>(decl.default_num);
        case ParamType::Float:
            return decl.default_num;
        case ParamType::Float3:
        case ParamType::Color:
            return std::array<float, 3>{
                static_cast<float>(decl.default_num),
                static_cast<float>(decl.default_num),
                static_cast<float>(decl.default_num),
            };
        case ParamType::String:
        case ParamType::FilePath:
            return std::string(decl.default_str);
        }
        return std::string{};
    }

    // True when value holds the variant alternative implied by decl.type.
    [[nodiscard]] inline bool param_value_matches_decl(
        const ParamValue& value,
        const ParamDecl& decl)
    {
        switch (decl.type) {
        case ParamType::Bool:
            return std::holds_alternative<bool>(value);
        case ParamType::Int:
        case ParamType::Enum:
            return std::holds_alternative<int64_t>(value);
        case ParamType::Float:
            return std::holds_alternative<double>(value);
        case ParamType::Float3:
        case ParamType::Color:
            return std::holds_alternative<std::array<float, 3>>(value);
        case ParamType::String:
        case ParamType::FilePath:
            return std::holds_alternative<std::string>(value);
        }
        return false;
    }

    // Fill any parameter missing from the block with its declared default, and
    // repair values whose stored type no longer matches the declaration.
    // Existing, correctly typed values are left untouched.
    inline void ensure_param_block_defaults(
        ParamBlock& params,
        const AssetCompiler& compiler)
    {
        for (const ParamDecl& decl : compiler.parameters) {
            const std::string name(decl.name);
            const auto it = params.values.find(name);
            if (it == params.values.end()) {
                params.values.emplace(name, default_param_value(decl));
            }
            else if (!param_value_matches_decl(it->second, decl)) {
                it->second = default_param_value(decl);
            }
        }
    }

} // namespace wz::asset
