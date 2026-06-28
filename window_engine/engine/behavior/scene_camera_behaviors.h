#pragma once

// engine/behavior/scene_camera_behaviors.h

#include <engine/behavior/behavior_plugin_abi.h>

namespace wz::engine::behavior
{
    // Built-in "scene setup" module. Bind it to a single node (e.g. the scene
    // root) and subscribe it to "scene.loaded". On that event it looks up the
    // node named by its `camera_name` config (first match), and asks the host
    // to make that node the active camera (WZ_BEHAVIOR_COMMAND_SET_ACTIVE_CAMERA).
    // Keeping camera selection on one root node avoids multiple nodes fighting
    // to set the camera on load.
    inline constexpr const char* kSceneCameraModule = "scene_camera";

    // Config key (string): the name of the camera node to activate.
    inline constexpr const char* kSceneCameraNameConfigKey = "camera_name";

    uint8_t register_scene_camera_behaviors(WzBehaviorPluginApi* api);
}
