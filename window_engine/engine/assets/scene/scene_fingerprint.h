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
    //
    // The fingerprint describes authored source identity only. It must not
    // include SceneInstance addresses, runtime storage pointers, app owner
    // pointers, GPU handles, or other process-local runtime identity.
    uint64_t scene_asset_fingerprint(const SceneAssetData& scene);
    std::string scene_asset_fingerprint_string(const SceneAssetData& scene);
}
