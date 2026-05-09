#pragma once

// engine/assets/toml/toml.h

#include <asset/types.h>

#include <external/toml/toml_document.h>

#include <vector>

namespace wz::engine::assets
{
    struct TOMLData
    {
        wz::toml::TOMLDocument document;
    };

    class TOMLTable
    {
    public:
        TOMLTable();

        wz::asset::ResourceHandle add(TOMLData data);

        const TOMLData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            TOMLData data;
        };

        std::vector<Slot> slots_;
    };

} // namespace wz::engine::assets