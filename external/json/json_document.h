#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wz::json
{
    enum class JSONValueKind : uint8_t
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    struct JSONValue;

    using JSONValuePtr = std::unique_ptr<JSONValue>;

    struct JSONMember
    {
        std::string  key;
        JSONValuePtr value;
    };

    struct JSONValue
    {
        JSONValueKind kind = JSONValueKind::Null;

        bool bool_value = false;
        double number_value = 0.0;
        std::string string_value;

        std::vector<JSONValuePtr> array_values;
        std::vector<JSONMember>   object_members;
    };

    struct JSONDocument
    {
        JSONValuePtr root;
    };

} // namespace wz::json