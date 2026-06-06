#pragma once

#include <file/filesystem.h>

namespace wz::engine::assets
{
    struct EngineAssetCacheSettings
    {
        wz::fs::Path root;
        bool enabled = true;
    };
}
