// Behavior-defined events (v34): a behavior emits a NAMED event at a node, and a
// statechart on it reacts via an `event` transition. These pin the RUNNER half -- the
// IR `event` trigger + the runner matching this frame's delivered events targeting self
// -- by fabricating a behavior_events view (the dispatcher plumbing that fills it real
// is tested separately). The chart's target state runs a recorded actuator, so a fired
// transition is observable.

#include <engine/behavior/statechart_ir.h>
#include <engine/behavior/statechart_runner.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    namespace sc = wz::engine::behavior::statechart;

    struct Recorder { bool invoked = false; };

    void rec_actuator(
        const WzBehaviorFrameFacts*, WzBehaviorEntityId,
        const WzActuatorArg*, uint32_t, void* user_data)
    {
        static_cast<Recorder*>(user_data)->invoked = true;
    }

    uint8_t rec_lookup(
        void* user, const char* name,
        WzBehaviorActuatorFn* out_fn, void** out_user_data)
    {
        if (name && std::string(name) == "test_actuator") {
            if (out_fn) {
                *out_fn = rec_actuator;
            }
            if (out_user_data) {
                *out_user_data = user;
            }
            return 1;
        }
        return 0;
    }

    // A behavior_events view over a plain vector.
    struct EventList { std::vector<WzBehaviorEventEntry> entries; };

    uint8_t read_event(void* user, uint32_t index, WzBehaviorEventEntry* out)
    {
        auto* list = static_cast<EventList*>(user);
        if (index >= list->entries.size()) {
            return 0;
        }
        *out = list->entries[index];
        return 1;
    }

    // idle --(event "died")--> dead; dead's `do` calls test_actuator, so a fired
    // transition (dead entered, its continuous effect applied) is observable.
    constexpr const char* kEventChart =
        R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
        R"("bindings":[],"agents":[],"pure":[],)"
        R"("regions":[{"id":"r","initial":"idle","states":["idle","dead"]}],)"
        R"("states":[)"
        R"({"id":"idle","do":[],"transitions":[)"
        R"({"trigger":{"kind":"event","name":"died"},"target":"dead"}]},)"
        R"({"id":"dead","do":[{"kind":"call","fn":"test_actuator","args":[]}],)"
        R"("transitions":[]}]})";

    void tick_with(
        EventList& events, Recorder& rec, WzBehaviorEntityId self)
    {
        wz::engine::behavior::StatechartRunnerStore store;
        const sc::Chart* chart = store.load("t", kEventChart);
        ASSERT_NE(chart, nullptr);
        const uint64_t handle = store.create(chart);
        ASSERT_NE(handle, 0u);

        WzBehaviorFrameFacts facts{};
        facts.actuator_registry_user = &rec;
        facts.actuator_lookup = rec_lookup;
        facts.behavior_events = WzBehaviorEventView{
            .user = &events,
            .count = static_cast<uint32_t>(events.entries.size()),
            .read = read_event,
        };

        WzBehaviorEvent ev{};
        ev.kind = WZ_EVENT_FRAME_UPDATE;
        ev.entity = self;
        store.tick(handle, &facts, &ev);
    }
}

TEST(StatechartEventTrigger, FiresOnMatchingBehaviorEvent)
{
    EventList events;
    events.entries.push_back(WzBehaviorEventEntry{ 7u, "died" });
    Recorder rec;
    tick_with(events, rec, /*self=*/7u);
    EXPECT_TRUE(rec.invoked);   // idle --event:died--> dead, dead's do ran
}

TEST(StatechartEventTrigger, DoesNotFireWithoutTheEvent)
{
    EventList events;   // empty
    Recorder rec;
    tick_with(events, rec, /*self=*/7u);
    EXPECT_FALSE(rec.invoked);
}

TEST(StatechartEventTrigger, IgnoresEventTargetedAtAnotherNode)
{
    EventList events;
    events.entries.push_back(WzBehaviorEventEntry{ 99u, "died" });   // not self
    Recorder rec;
    tick_with(events, rec, /*self=*/7u);
    EXPECT_FALSE(rec.invoked);
}

TEST(StatechartEventTrigger, IgnoresADifferentEventName)
{
    EventList events;
    events.entries.push_back(WzBehaviorEventEntry{ 7u, "hit" });   // not "died"
    Recorder rec;
    tick_with(events, rec, /*self=*/7u);
    EXPECT_FALSE(rec.invoked);
}
