#include "behavior_test_support.h"

// Declared params for behavior MODULES (previously only function behaviors could
// advertise their config): a module ships a WzBehaviorParamDesc table on its
// WzBehaviorModuleDesc, the size-gated adapter parses it, and the registry carries
// it -- the schema the editor needs to render knobs with defaults.

namespace
{
    void params_mod_on_event(
        const WzBehaviorFrameFacts*, const WzBehaviorEvent*, void*)
    {
    }

    uint8_t register_params_module(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc)
        {
            return 0;
        }
        static const char* channels[] = { "self.start" };
        static const WzBehaviorParamDesc params[] = {
            { "speed", "Speed", WZ_BEHAVIOR_PARAM_FLOAT, 6.0, nullptr },
            { "mode", "Mode", WZ_BEHAVIOR_PARAM_STRING, 0.0, "idle" },
        };
        WzBehaviorModuleDesc desc{};
        desc.size = sizeof(desc);
        desc.module = "params_mod";
        desc.on_event = params_mod_on_event;
        desc.event_channels = channels;
        desc.event_channel_count = 1u;
        desc.params = params;
        desc.param_count = 2u;
        return api->register_module_desc(api->user, &desc);
    }
}

// A module's declared param table survives the ABI round-trip into the registry.
TEST(BehaviorModuleParams, RegisteredModuleCarriesDeclaredParams)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_params_module));

    const auto handle = registry.find_module("params_mod");
    ASSERT_TRUE(handle.has_value());
    const auto* mod = registry.get_module(*handle);
    ASSERT_NE(mod, nullptr);

    ASSERT_EQ(mod->params.size(), 2u);
    EXPECT_EQ(mod->params[0].key, "speed");
    EXPECT_EQ(mod->params[0].type, BehaviorParamType::Float);
    EXPECT_EQ(mod->params[0].default_number, 6.0);
    EXPECT_EQ(mod->params[1].key, "mode");
    EXPECT_EQ(mod->params[1].type, BehaviorParamType::String);
    EXPECT_EQ(mod->params[1].default_string, "idle");
}

// The built-in quantum_agent now advertises its knobs (goal + friends).
TEST(BehaviorModuleParams, BuiltinQuantumAgentDeclaresItsKnobs)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    wz::Logger logger{};
    register_builtin_behaviors(registry, plugins, logger);

    const auto handle = registry.find_module("quantum_agent");
    ASSERT_TRUE(handle.has_value());
    const auto* mod = registry.get_module(*handle);
    ASSERT_NE(mod, nullptr);
    EXPECT_FALSE(mod->params.empty());

    bool has_goal = false;
    for (const auto& p : mod->params) {
        if (p.key == "goal") {
            has_goal = true;
            EXPECT_EQ(p.type, BehaviorParamType::Float);
        }
    }
    EXPECT_TRUE(has_goal);

    // terrain_align advertises its rate too.
    const auto align = registry.find_module("terrain_align");
    ASSERT_TRUE(align.has_value());
    EXPECT_FALSE(registry.get_module(*align)->params.empty());
}
