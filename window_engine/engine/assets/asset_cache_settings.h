#pragma once

#include <file/filesystem.h>

namespace wz::engine::assets
{
    struct EngineAssetCacheSettings
    {
        wz::fs::Path root;
        bool enabled = true;
        // Sealed = a shipped bundle running from a read-only baked cache with the
        // heavy authoring sources stripped (issue #334). A cacheable asset that is
        // ABSENT from the cache is then a FATAL miss naming the key, rather than a
        // silent recompile from a source that isn't there (which would surface
        // later as a confusing DependencyFailed on a downstream node). Only ever
        // set by the standalone bundle's wozzits_app.json; the editor leaves it
        // false so live authoring still recompiles from source on a miss.
        bool sealed = false;
    };
}
