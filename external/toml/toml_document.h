#pragma once

// external/toml/toml_document.h
//
// Wozzits-owned TOML document model.
//
// This header must not include toml++.
// toml++ stays private to toml_parser.cpp.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wz::toml
{
    enum class TOMLValueKind : uint8_t
    {
        None,
        Bool,
        Integer,
        Float,
        String,
        Array,
        Table,
    };

    struct TOMLValue;

    using TOMLValuePtr = std::unique_ptr<TOMLValue>;

    struct TOMLMember
    {
        std::string  key;
        TOMLValuePtr value;
    };

    struct TOMLValue
    {
        TOMLValueKind kind = TOMLValueKind::None;

        bool bool_value = false;

        int64_t integer_value = 0;
        double  float_value = 0.0;

        std::string string_value;

        // TOML arrays are ordered.
        std::vector<TOMLValuePtr> array_values;

        // TOML tables are ordered here by parser traversal/insertion order.
        // This preserves useful source/document order for diagnostics and tools.
        std::vector<TOMLMember> table_members;
    };

    struct TOMLDocument
    {
        // Root should normally be a Table.
        TOMLValuePtr root;
    };

} // namespace wz::toml