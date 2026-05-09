#pragma once

// engine/assets/json/json.h

#include <asset/types.h>

#include <external/json/json_document.h>

#include <vector>

namespace wz::engine::assets
{
    struct JSONData
    {
        wz::json::JSONDocument document;
    };

    class JSONTable
    {
    public:
        JSONTable();

        wz::asset::ResourceHandle add(JSONData data);

        const JSONData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            JSONData data;
        };

        std::vector<Slot> slots_;
    };

} // namespace wz::engine::assets