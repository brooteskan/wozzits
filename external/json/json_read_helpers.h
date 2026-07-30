#pragma once

// external/json/json_read_helpers.h
//
// Lightweight read-only accessors for wz::json::JSONValue trees.

#include <external/json/json_document.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>

namespace wz::json
{
    // Convert a JSON number to an integral type, REJECTING anything the target
    // cannot represent. Every JSON number arrives as a double, and
    // `static_cast<T>(d)` is undefined behaviour when the truncated value does not
    // fit T -- so `"slot": 1e30` or `"asset_graph_node_id": -1` in a file the
    // engine happily reads was UB, benign-looking on x86 (it wraps) and a trap
    // under UBSan. Rejecting turns that into a value the caller can handle the way
    // it already handles a missing or malformed member.
    //
    // Truncates toward zero, like the cast it replaces, so 2.7 -> 2.
    //
    // The bound is computed as a power of two rather than from numeric_limits::max()
    // on purpose: max() for a 64-bit type is not representable as a double and
    // rounds UP when converted, so the natural `d <= (double)max()` test would
    // ACCEPT 2^64 and hand the cast the one value that is still UB. 2^digits is
    // exact, and an integer-valued double below it is in range.
    template <typename T>
    inline std::optional<T> narrow_number(double value) noexcept
    {
        static_assert(std::is_integral_v<T>, "narrow_number targets integers");

        if (!std::isfinite(value)) {
            return std::nullopt;   // NaN / +-inf
        }
        const double truncated = std::trunc(value);
        constexpr int kValueBits = std::numeric_limits<T>::digits;
        const double limit = std::ldexp(1.0, kValueBits);   // 2^digits, exact
        if (truncated >= limit) {
            return std::nullopt;
        }
        if constexpr (std::is_signed_v<T>) {
            if (truncated < -limit) {
                return std::nullopt;
            }
        }
        else {
            if (truncated < 0.0) {
                return std::nullopt;
            }
        }
        return static_cast<T>(truncated);
    }

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

    // Read member `key` as an integral value, or nullopt when it is missing, not a
    // number, or out of range for T (see narrow_number).
    template <typename T>
    inline std::optional<T> read_integral(
        const JSONValue& obj,
        std::string_view key) noexcept
    {
        const auto* v = find_member(obj, key);
        if (!v || v->kind != JSONValueKind::Number) {
            return std::nullopt;
        }
        return narrow_number<T>(v->number_value);
    }

    // Guards the UPPER bound too, not just the negative side: the old body rejected
    // < 0 and then cast, so a value above UINT32_MAX was still UB. Every caller
    // already treats nullopt as "absent or unusable", so nothing had to change.
    inline std::optional<uint32_t> read_uint(
        const JSONValue& obj,
        std::string_view key) noexcept
    {
        return read_integral<uint32_t>(obj, key);
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
