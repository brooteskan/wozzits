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
    //
    // The contract is COMPLETENESS over the authored data: every authored member
    // of SceneNodeAsset (presence and meaningful fields) must reach the mix, so
    // two scenes that differ anywhere an author can reach never hash the same.
    // Bridge products are the one deliberate exception -- the AssetKeys
    // re-resolved on every bind (renderable_asset, geometry_asset,
    // render_program_asset, scene_source, render_to_texture.target) are excluded
    // because their authored anchors, the asset-graph node ids, are mixed
    // instead. Adding an authored component without adding it here is a bug:
    // see the sensitivity matrix in tests/asset_scene/scene_fingerprint_tests.
    uint64_t scene_asset_fingerprint(const SceneAssetData& scene);
    std::string scene_asset_fingerprint_string(const SceneAssetData& scene);
}
