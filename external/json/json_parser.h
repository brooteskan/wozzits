#pragma once

#include <external/json/json_document.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::json
{
    struct JSONParseError
    {
        std::string message;
        uint64_t    position = 0;
    };

    struct JSONParseResult
    {
        JSONDocument   document;
        JSONParseError error;
        bool           ok = false;
    };

    JSONParseResult parse_json_bytes(
        const uint8_t* data,
        size_t         size
    );

    JSONParseResult parse_json_string(
        const std::string& text
    );

    JSONParseResult parse_json_bytes(
        const std::vector<uint8_t>& bytes
    );

} // namespace wz::json