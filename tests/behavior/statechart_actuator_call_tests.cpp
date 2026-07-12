// Seam 1 of the open actuator vocabulary (v33): a statechart `call` effect invokes
// a behavior-REGISTERED actuator by name, with its arguments resolved against the
// chart (bindings -> entities, consts/ops -> scalars) and handed over as
// WzActuatorArg. These tests pin the three moving parts without the app/editor:
//   * parse       -- statechart_ir.cpp accepts `call` and rejects bad args
//   * registry    -- BehaviorRegistry stores + finds actuators by name
//   * dispatch    -- StatechartRunnerStore::tick marshals args + calls through
//                    facts.actuator_lookup with the acting entity as `self`
// The runner reaches the registry only through facts, so a hand-built facts with a
// fake lookup fully exercises the dispatch path.

#include <engine/behavior/behavior_registry.h>
#include <engine/behavior/statechart_ir.h>
#include <engine/behavior/statechart_runner.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    namespace sc = wz::engine::behavior::statechart;
    using wz::engine::behavior::BehaviorActuatorRegistration;
    using wz::engine::behavior::BehaviorRegistry;

    // Records what an actuator invocation received, so a test can assert on the
    // resolved args. Carried to the fake actuator via user_data (no globals).
    struct CapturedCall
    {
        bool invoked = false;
        std::string looked_up_name;
        WzBehaviorEntityId self = 0;
        std::vector<WzActuatorArg> args;
    };

    void fake_actuator(
        const WzBehaviorFrameFacts*,
        WzBehaviorEntityId self,
        const WzActuatorArg* args,
        uint32_t arg_count,
        void* user_data)
    {
        auto* cap = static_cast<CapturedCall*>(user_data);
        cap->invoked = true;
        cap->self = self;
        cap->args.assign(args, args + arg_count);
    }

    // facts.actuator_lookup: resolve only "test_actuator", passing the capture
    // through as the actuator's user_data.
    uint8_t fake_lookup(
        void* user,
        const char* name,
        WzBehaviorActuatorFn* out_fn,
        void** out_user_data)
    {
        auto* cap = static_cast<CapturedCall*>(user);
        cap->looked_up_name = name ? name : "";
        if (name && std::string(name) == "test_actuator") {
            if (out_fn) {
                *out_fn = fake_actuator;
            }
            if (out_user_data) {
                *out_user_data = user;   // capture reaches the actuator as user_data
            }
            return 1;
        }
        return 0;
    }

    // A stand-in global name lookup: "prey" resolves to entity 42.
    uint8_t find_prey(void* /*user*/, const char* name, WzBehaviorEntityId* out)
    {
        if (name && std::string(name) == "prey") {
            if (out) {
                *out = 42u;
            }
            return 1;
        }
        return 0;
    }

    void noop_actuator(
        const WzBehaviorFrameFacts*, WzBehaviorEntityId,
        const WzActuatorArg*, uint32_t, void*)
    {
    }

    // A chart whose initial state's `do` calls test_actuator(bind:prey, const:3.5)
    // every tick. The binding is global-scope so it resolves via find_entity_by_name.
    constexpr const char* kCallChart =
        R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
        R"("bindings":[{"port":"prey","find":"prey","scope":"global"}],)"
        R"("agents":[],"pure":[],)"
        R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","do":[)"
        R"({"kind":"call","fn":"test_actuator",)"
        R"("args":[{"bind":"prey"},{"const":3.5}]}],"transitions":[]}]})";
}

TEST(StatechartActuatorCall, DispatchResolvesBindingAndScalarArgs)
{
    wz::engine::behavior::StatechartRunnerStore store;
    const sc::Chart* chart = store.load("t", kCallChart);
    ASSERT_NE(chart, nullptr);
    const uint64_t handle = store.create(chart);
    ASSERT_NE(handle, 0u);

    CapturedCall capture;

    WzFrameTiming timing{};
    timing.delta_seconds = 0.016f;

    WzBehaviorFrameFacts facts{};
    facts.timing = &timing;
    facts.find_entity_by_name = find_prey;
    facts.actuator_registry_user = &capture;
    facts.actuator_lookup = fake_lookup;

    WzBehaviorEvent event{};
    event.kind = WZ_EVENT_FRAME_UPDATE;
    event.entity = 7u;   // the acting entity (self)

    store.tick(handle, &facts, &event);

    EXPECT_TRUE(capture.invoked);
    EXPECT_EQ(capture.looked_up_name, "test_actuator");
    EXPECT_EQ(capture.self, 7u);
    ASSERT_EQ(capture.args.size(), 2u);
    // arg 0: the bound "prey" -> entity 42.
    EXPECT_EQ(capture.args[0].kind, WZ_ACTUATOR_ARG_ENTITY);
    EXPECT_EQ(capture.args[0].entity, 42u);
    // arg 1: the const 3.5 -> a scalar.
    EXPECT_EQ(capture.args[1].kind, WZ_ACTUATOR_ARG_SCALAR);
    EXPECT_DOUBLE_EQ(capture.args[1].scalar, 3.5);
}

TEST(StatechartActuatorCall, UnknownActuatorNameIsANoOp)
{
    // The lookup only knows "test_actuator"; a chart calling something else must
    // simply not invoke anything (a hand-authored typo should not crash the tick).
    constexpr const char* ir =
        R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
        R"("bindings":[],"agents":[],"pure":[],)"
        R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","do":[)"
        R"({"kind":"call","fn":"does_not_exist","args":[]}],"transitions":[]}]})";

    wz::engine::behavior::StatechartRunnerStore store;
    const sc::Chart* chart = store.load("t", ir);
    ASSERT_NE(chart, nullptr);
    const uint64_t handle = store.create(chart);

    CapturedCall capture;
    WzBehaviorFrameFacts facts{};
    facts.actuator_registry_user = &capture;
    facts.actuator_lookup = fake_lookup;
    WzBehaviorEvent event{};
    event.kind = WZ_EVENT_FRAME_UPDATE;
    event.entity = 1u;

    store.tick(handle, &facts, &event);   // must not crash
    EXPECT_FALSE(capture.invoked);
    EXPECT_EQ(capture.looked_up_name, "does_not_exist");
}

TEST(StatechartActuatorCall, ParseRejectsCallWithoutFn)
{
    constexpr const char* ir =
        R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
        R"("bindings":[],"agents":[],"pure":[],)"
        R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","do":[{"kind":"call","args":[]}],"transitions":[]}]})";

    sc::Chart chart;
    std::string error;
    EXPECT_FALSE(sc::parse_chart(ir, chart, error));
    EXPECT_NE(error.find("call needs 'fn'"), std::string::npos);
}

TEST(StatechartActuatorCall, ParseRejectsCallArgBindingToUnknownPort)
{
    constexpr const char* ir =
        R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
        R"("bindings":[],"agents":[],"pure":[],)"
        R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","do":[)"
        R"({"kind":"call","fn":"move_toward","args":[{"bind":"nope"}]}],)"
        R"("transitions":[]}]})";

    sc::Chart chart;
    std::string error;
    EXPECT_FALSE(sc::parse_chart(ir, chart, error));
    EXPECT_NE(error.find("unknown port"), std::string::npos);
}

TEST(BehaviorActuatorRegistry, RegistersFindsAndReplacesByName)
{
    BehaviorRegistry registry;

    EXPECT_TRUE(registry.register_actuator(BehaviorActuatorRegistration{
        .name = "move_toward",
        .label = "Move toward",
        .params = {},
        .fn = noop_actuator,
        .user_data = nullptr,
    }));

    // A null fn or empty name is rejected.
    EXPECT_FALSE(registry.register_actuator(BehaviorActuatorRegistration{
        .name = "bad",
        .fn = nullptr,
    }));
    EXPECT_FALSE(registry.register_actuator(BehaviorActuatorRegistration{
        .name = "",
        .fn = noop_actuator,
    }));

    const auto* found = registry.find_actuator("move_toward");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->fn, noop_actuator);
    EXPECT_EQ(registry.find_actuator("missing"), nullptr);
    EXPECT_EQ(registry.actuators().size(), 1u);

    // Last writer wins for the same name (a reloaded plugin re-registers).
    EXPECT_TRUE(registry.register_actuator(BehaviorActuatorRegistration{
        .name = "move_toward",
        .label = "Move toward (v2)",
        .fn = noop_actuator,
    }));
    EXPECT_EQ(registry.actuators().size(), 1u);
    EXPECT_EQ(registry.find_actuator("move_toward")->label, "Move toward (v2)");

    registry.clear();
    EXPECT_EQ(registry.find_actuator("move_toward"), nullptr);
    EXPECT_TRUE(registry.actuators().empty());
}
