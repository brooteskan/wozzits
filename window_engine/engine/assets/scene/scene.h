#pragma once

// engine/assets/scene/scene.h

#include <asset/types.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <vector>

namespace wz::engine::assets
{
    class SceneAssetTable
    {
    public:
        SceneAssetTable();

        wz::asset::ResourceHandle add(SceneAssetData data);

        const SceneAssetData* get(wz::asset::ResourceHandle handle) const;

        void destroy();

    private:
        struct Slot
        {
            uint32_t epoch = 0;
            bool occupied = false;
            SceneAssetData data;
        };

        std::vector<Slot> slots_;
    };

} // namespace wz::engine::assets
