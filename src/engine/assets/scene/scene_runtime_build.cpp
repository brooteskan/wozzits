#include <engine/assets/scene/scene_runtime_build.h>

#include <scene/scene_graph.h>

namespace wz::engine::assets
{
    SceneAssetRuntimeBuild build_scene_runtime_from_asset_snapshot(
        const SceneAssetData& authored,
        const SceneInstantiateContext& context)
    {
        SceneAssetRuntimeBuild build{};
        build.snapshot = authored;
        build.scene_hash = scene_asset_fingerprint(build.snapshot);
        build.scene_hash_text = scene_asset_fingerprint_string(build.snapshot);

        auto result = instantiate_scene(build.snapshot, context);
        if (!result.ok()) {
            build.status = "instantiate failed: " + result.error_detail;
            build.valid = false;
            return build;
        }

        build.instance = std::move(result.instance);
        wz::scene::propagate_all(build.instance.storage.polytree);
        build.status = "runtime scene ready: " + build.snapshot.name
            + " scene_hash=" + build.scene_hash_text;
        build.valid = true;
        return build;
    }
}
