#pragma once

#include <external/json/json_document.h>

#include <cstdint>
#include <string>

namespace wz::json
{
    struct JSONWriteOptions
    {
        bool pretty = true;
        uint32_t indent_spaces = 2;
    };

    std::string serialize_json(
        const JSONValue& value,
        const JSONWriteOptions& options = {});

    std::string serialize_json(
        const JSONDocument& document,
        const JSONWriteOptions& options = {});

} // namespace wz::json
