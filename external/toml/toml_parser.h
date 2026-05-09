#pragma once

// external/toml/toml_parser.h
//
// Wozzits-owned TOML parser API.
// This header must not include toml++.
// toml++ stays private to toml_parser.cpp.

#include <external/toml/toml_document.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wz::toml
{
    struct TOMLParseError
    {
        std::string message;

        // 1-based when available from the parser; 0 means unknown.
        uint32_t line = 0;

        // 1-based when available from the parser; 0 means unknown.
        uint32_t column = 0;
    };

    struct TOMLParseResult
    {
        TOMLDocument   document;
        TOMLParseError error;
        bool           ok = false;
    };

    TOMLParseResult parse_toml_bytes(
        const uint8_t* data,
        size_t         size
    );

    TOMLParseResult parse_toml_string(
        const std::string& text
    );

    TOMLParseResult parse_toml_bytes(
        const std::vector<uint8_t>& bytes
    );

} // namespace wz::toml