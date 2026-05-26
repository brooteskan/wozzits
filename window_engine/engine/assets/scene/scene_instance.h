#pragma once

// engine/assets/scene/scene_instance.h

#include <engine/assets/scene/scene_asset_data.h>

#include <scene/scene_graph.h>
#include <scene/compile/compiled_scene.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace wz::engine::assets
{
    struct SceneInstance
    {
        wz::scene::SceneStorage storage{};

        std::vector<wz::scene::RenderableDescriptor> renderables;
        std::vector<wz::scene::LightRecord> lights;

        wz::scene::ViewData default_view{};

        std::vector<std::string> runtime_to_authored;
        std::unordered_map<std::string, wz::core::graph::NodeHandle> authored_to_runtime;
    };

    enum class SceneInstantiateError
    {
        None = 0,
        DuplicateNodeId,
        ParentNotFound,
        ParentCycle,
        PolytreeBuildFailed,
    };

    struct SceneInstantiateResult
    {
        SceneInstance instance{};
        SceneInstantiateError error = SceneInstantiateError::None;
        std::string error_detail;

        bool ok() const noexcept { return error == SceneInstantiateError::None; }
    };

    SceneInstantiateResult instantiate_scene(const SceneAssetData& scene);

} // namespace wz::engine::assets
