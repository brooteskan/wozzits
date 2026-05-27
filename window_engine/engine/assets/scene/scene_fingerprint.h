#pragma once

#include <engine/assets/scene/scene_asset_data.h>

#include <cstdint>
#include <string>

namespace wz::engine::assets
{
    // Identity convention for logs:
    // - scene_hash identifies authored SceneAssetData content and should stay
    //   stable for the same authored scene shape across runtime owners.
    // - pointer values in lifecycle logs identify process-local runtime
    //   owners/storage and are only meaningful within one run.
    uint64_t scene_asset_fingerprint(const SceneAssetData& scene);
    std::string scene_asset_fingerprint_string(const SceneAssetData& scene);
}
