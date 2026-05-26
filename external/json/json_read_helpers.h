#pragma once

// external/json/json_read_helpers.h
//
// Lightweight read-only accessors for wz::json::JSONValue trees.

#include <external/json/json_document.h>

#include <cstdint>
#include <optional>
#include <string_view>

namespace wz::json
{
    inline const JSONValue* find_member(
        const JSONValue& obj,
        std::string_view key) noexcept
    {
        if (obj.kind != JSONValueKind::Object) return nullptr;
        for (const auto& m : obj.object_members) {
            if (m.key == key && m.value)
                return m.value.get();
        }
        return nullptr;
    }

    inline std::optional<std::string_view> read_string(
        const JSONValue& obj,
        std::string_view key) noexcept
    {
        const auto* v = find_member(obj, key);
        if (!v || v->kind != JSONValueKind::String)
            return std::nullopt;
        return std::string_view{ v->string_value };
    }

    inline std::optional<double> read_number(
        const JSONValue& obj,
        std::string_view key) noexcept
    {
        const auto* v = find_member(obj, key);
        if (!v || v->kind != JSONValueKind::Number)
            return std::nullopt;
        return v->number_value;
    }

    inline std::optional<bool> read_bool(
        const JSONValue& obj,
        std::string_view key) noexcept
    {
        const auto* v = find_member(obj, key);
        if (!v || v->kind != JSONValueKind::Bool)
            return std::nullopt;
        return v->bool_value;
    }

    inline std::optional<uint32_t> read_uint(
        const JSONValue& obj,
        std::string_view key) noexcept
    {
        const auto* v = find_member(obj, key);
        if (!v || v->kind != JSONValueKind::Number)
            return std::nullopt;
        if (v->number_value < 0.0)
            return std::nullopt;
        return static_cast<uint32_t>(v->number_value);
    }

    inline bool read_float3(
        const JSONValue& obj,
        std::string_view key,
        float out[3]) noexcept
    {
        const auto* v = find_member(obj, key);
        if (!v || v->kind != JSONValueKind::Array)
            return false;
        if (v->array_values.size() != 3)
            return false;
        for (int i = 0; i < 3; ++i) {
            if (v->array_values[i]->kind != JSONValueKind::Number)
                return false;
            out[i] = static_cast<float>(v->array_values[i]->number_value);
        }
        return true;
    }

    inline bool read_float4(
        const JSONValue& obj,
        std::string_view key,
        float out[4]) noexcept
    {
        const auto* v = find_member(obj, key);
        if (!v || v->kind != JSONValueKind::Array)
            return false;
        if (v->array_values.size() != 4)
            return false;
        for (int i = 0; i < 4; ++i) {
            if (v->array_values[i]->kind != JSONValueKind::Number)
                return false;
            out[i] = static_cast<float>(v->array_values[i]->number_value);
        }
        return true;
    }

} // namespace wz::json
