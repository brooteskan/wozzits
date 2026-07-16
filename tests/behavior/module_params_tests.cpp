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

// ─── Declared defaults reach the RUNTIME, not just the inspector ─────
//
// A declared default used to travel one way only (plugin -> catalog -> the editor's
// field), so a param the user never edited read as absent at runtime -- nothing but
// overrides is ever written to the scene file. Every module therefore had to repeat
// its default in C++, and the two copies could silently disagree: the inspector
// confidently displayed a value the plugin had never heard of. wz_config_* now falls
// back to the declared default, so the plugin declares it once.

namespace
{
    // Reads one param of each declared type, plus two reads that must STAY misses.
    struct ConfigDefaultProbe
    {
        uint32_t calls = 0;

        uint8_t mode_read = 0;
        char mode[32]{};
        uint32_t mode_required = 0;

        uint8_t speed_read = 0;
        double speed = -1.0;

        uint8_t eager_read = 0;
        uint8_t eager = 0;

        // An undeclared key must miss and leave the out-param alone -- that is what
        // lets a caller pre-seed its own default and keep it.
        uint8_t undeclared_read = 1;
        double undeclared_preseed = 42.0;

        // "mode" is declared STRING; reading it as a number must NOT hand back the
        // default. The declared type is the schema.
        uint8_t wrong_type_read = 1;
        double wrong_type_preseed = 99.0;
    };

    ConfigDefaultProbe* g_config_default_probe = nullptr;

    void config_default_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<ConfigDefaultProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        if (!wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
            return;
        }

        ++probe->calls;
        probe->mode_read = wz_config_string(
            facts,
            "mode",
            probe->mode,
            sizeof(probe->mode),
            &probe->mode_required);
        probe->speed_read = wz_config_number(facts, "speed", &probe->speed);
        probe->eager_read = wz_config_bool(facts, "eager", &probe->eager);
        probe->undeclared_read =
            wz_config_number(facts, "nope", &probe->undeclared_preseed);
        probe->wrong_type_read =
            wz_config_number(facts, "mode", &probe->wrong_type_preseed);
    }

    uint8_t register_config_default_pack(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc || !g_config_default_probe)
        {
            return 0;
        }
        static const char* channels[] = { "frame.update" };
        static const WzBehaviorParamDesc params[] = {
            { "speed", "Speed", WZ_BEHAVIOR_PARAM_FLOAT, 6.0, nullptr },
            { "mode", "Mode", WZ_BEHAVIOR_PARAM_STRING, 0.0, "idle" },
            // A BOOL's default rides in default_number (there is no default_bool).
            { "eager", "Eager", WZ_BEHAVIOR_PARAM_BOOL, 1.0, nullptr },
        };
        WzBehaviorModuleDesc desc{};
        desc.size = sizeof(desc);
        desc.module = "config_default_mod";
        desc.on_event = config_default_handler;
        desc.event_channels = channels;
        desc.event_channel_count = 1u;
        desc.params = params;
        desc.param_count = 3u;
        desc.module_user_data = g_config_default_probe;
        return api->register_module_desc(api->user, &desc);
    }

    // Declares NOTHING but reads the same keys: the regression guard for every
    // module that pre-seeds its own default and never declared a param table.
    struct NoParamsProbe
    {
        uint32_t calls = 0;
        uint8_t speed_read = 1;
        double speed_preseed = 3.5;
        uint8_t mode_read = 1;
        char mode[32] = "preseeded";
    };

    NoParamsProbe* g_no_params_probe = nullptr;

    void no_params_handler(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void* user)
    {
        auto* probe = static_cast<NoParamsProbe*>(user);
        ASSERT_NE(probe, nullptr);
        ASSERT_NE(facts, nullptr);
        if (!wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
            return;
        }

        ++probe->calls;
        uint32_t required = 0;
        probe->speed_read =
            wz_config_number(facts, "speed", &probe->speed_preseed);
        probe->mode_read = wz_config_string(
            facts, "mode", probe->mode, sizeof(probe->mode), &required);
    }

    uint8_t register_no_params_pack(WzBehaviorPluginApi* api)
    {
        if (!api || !api->register_module || !g_no_params_probe) {
            return 0;
        }
        return api->register_module(
            api->user, "no_params_mod", no_params_handler, g_no_params_probe);
    }
}

// With NO authored config -- the normal case -- each declared default is what the
// module reads.
TEST(BehaviorModuleParams, DeclaredDefaultsAreReadableAtRuntime)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ConfigDefaultProbe probe{};
    g_config_default_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_config_default_pack));

    SceneInstance scene = scene_with_behavior(4u, "config_default_mod", "");
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.speed_read, 1u);
    EXPECT_DOUBLE_EQ(probe.speed, 6.0);
    EXPECT_EQ(probe.mode_read, 1u);
    EXPECT_STREQ(probe.mode, "idle");
    EXPECT_EQ(probe.mode_required, 5u) << "\"idle\" + NUL";
    EXPECT_EQ(probe.eager_read, 1u);
    EXPECT_EQ(probe.eager, 1u) << "a BOOL default rides in default_number";

    EXPECT_EQ(probe.undeclared_read, 0u)
        << "an undeclared key must still miss";
    EXPECT_DOUBLE_EQ(probe.undeclared_preseed, 42.0)
        << "a miss must leave the out-param untouched, so a caller's pre-seeded "
           "default survives";

    EXPECT_EQ(probe.wrong_type_read, 0u)
        << "a declared STRING must never satisfy a number read";
    EXPECT_DOUBLE_EQ(probe.wrong_type_preseed, 99.0);

    g_config_default_probe = nullptr;
}

// An authored value beats the declared default -- including when it is the falsy
// one, which a "default wins if the value looks empty" bug would get wrong.
TEST(BehaviorModuleParams, AuthoredConfigOverridesTheDeclaredDefault)
{
    using wz::engine::assets::SceneBehaviorConfigValue;
    using Kind = wz::engine::assets::SceneBehaviorConfigValueKind;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    ConfigDefaultProbe probe{};
    g_config_default_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_config_default_pack));

    SceneInstance scene = scene_with_behavior(4u, "config_default_mod", "");
    scene.behaviors[0].component.config = {
        SceneBehaviorConfigValue{
            .key = "speed", .kind = Kind::Number, .number_value = 2.5 },
        SceneBehaviorConfigValue{
            .key = "mode", .kind = Kind::String, .string_value = "hunt" },
        SceneBehaviorConfigValue{
            .key = "eager", .kind = Kind::Bool, .bool_value = false },
    };
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 1u);
    EXPECT_DOUBLE_EQ(probe.speed, 2.5);
    EXPECT_STREQ(probe.mode, "hunt");
    EXPECT_EQ(probe.eager_read, 1u);
    EXPECT_EQ(probe.eager, 0u)
        << "an authored false must beat a declared default of true";

    g_config_default_probe = nullptr;
}

// The regression guard: a module that declares no params is untouched by the
// fallback, so every module that pre-seeds its own C++ default keeps it.
TEST(BehaviorModuleParams, ModuleDeclaringNoParamsLeavesPreSeedsAlone)
{
    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    NoParamsProbe probe{};
    g_no_params_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry, register_no_params_pack));

    SceneInstance scene = scene_with_behavior(4u, "no_params_mod", "");
    wz::engine::FrameStorage frame_storage{};
    BehaviorFrameContext context{
        .frame_storage = &frame_storage,
        .scene = &scene,
        .commands = &frame_storage.behavior_commands,
    };

    dispatch_behaviors(scene, registry, context);

    ASSERT_EQ(probe.calls, 1u);
    EXPECT_EQ(probe.speed_read, 0u);
    EXPECT_DOUBLE_EQ(probe.speed_preseed, 3.5);
    EXPECT_EQ(probe.mode_read, 0u);
    EXPECT_STREQ(probe.mode, "preseeded");

    g_no_params_probe = nullptr;
}
