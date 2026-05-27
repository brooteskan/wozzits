#pragma once

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_fingerprint.h>
#include <engine/assets/scene/scene_instance.h>

#include <cstdint>
#include <string>

namespace wz::engine::assets
{
    // Lower-level construction result shared by editor preview and future
    // benchmark adapters. This is not an app owner; callers decide where the
    // snapshot, runtime instance, frame storage, input policy, and metrics live.
    struct SceneAssetRuntimeBuild
    {
        SceneAssetData snapshot{};
        SceneInstance instance{};
        uint64_t scene_hash = 0;
        std::string scene_hash_text;
        std::string status;
        bool valid = false;
    };

    SceneAssetRuntimeBuild build_scene_runtime_from_asset_snapshot(
        const SceneAssetData& authored,
        const SceneInstantiateContext& context = {});
}
