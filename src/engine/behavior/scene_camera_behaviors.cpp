#include <engine/behavior/scene_camera_behaviors.h>

#include <string>

namespace wz::engine::behavior
{
    namespace
    {
        // On WZ_EVENT_SCENE_LOADED: resolve the configured camera node by name
        // and emit a SET_ACTIVE_CAMERA command for it. The host (WozzitsApp_v1)
        // reads that node's transform + SceneCameraAsset and points the runtime
        // camera at it. The behavior does no math and reads no camera params --
        // it only names the node.
        void scene_camera_on_event(
            const WzBehaviorFrameFacts* facts,
            const WzBehaviorEvent* event,
            void*)
        {
            if (!facts || !event
                || event->kind != WZ_EVENT_SCENE_LOADED)
            {
                return;
            }
            if (!facts->get_config_string || !facts->find_entity_by_name
                || !facts->write_command)
            {
                return;
            }

            char name[256] = {};
            uint32_t required = 0;
            if (!facts->get_config_string(
                    facts->behavior_config_user,
                    kSceneCameraNameConfigKey,
                    name,
                    static_cast<uint32_t>(sizeof(name)),
                    &required))
            {
                if (facts->log_info) {
                    facts->log_info(
                        facts->log_user,
                        "[behavior/scene_camera] no 'camera_name' config; "
                        "leaving the free-fly camera active");
                }
                return;
            }

            WzBehaviorEntityId camera = WZ_INVALID_BEHAVIOR_ENTITY;
            if (!facts->find_entity_by_name(
                    facts->scene_query_user, name, &camera))
            {
                if (facts->log_info) {
                    std::string msg =
                        "[behavior/scene_camera] camera node '";
                    msg += name;
                    msg += "' not found; leaving the free-fly camera active";
                    facts->log_info(facts->log_user, msg.c_str());
                }
                return;
            }

            const WzBehaviorCommand command{
                .entity = camera,
                .kind = WZ_BEHAVIOR_COMMAND_SET_ACTIVE_CAMERA,
                .values = { 0.0f, 0.0f, 0.0f, 0.0f },
            };
            facts->write_command(facts->command_writer_user, &command);
        }
    }

    uint8_t register_scene_camera_behaviors(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc)
        {
            return 0;
        }

        const char* channels[] = { "scene.loaded" };
        WzBehaviorModuleDesc desc{};
        desc.size = sizeof(desc);
        desc.module = kSceneCameraModule;
        desc.on_event = scene_camera_on_event;
        desc.on_init = nullptr;
        desc.event_channels = channels;
        desc.event_channel_count = 1u;
        desc.module_user_data = nullptr;
        return api->register_module_desc(api->user, &desc);
    }
}
