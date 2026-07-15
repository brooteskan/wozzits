// Chart-authored state read (v37): a statechart pure-op `read_state` reads a
// behavior-PUBLISHED named scalar on self via facts.get_entity_scalar -- the pull twin
// of an `event` trigger. These pin the two moving parts without a live scene: the parse
// carries the scalar name, and the runner threads the published value into pure eval,
// so an effect (here a measure angle) driven by that value is observable.

#include <engine/behavior/statechart_ir.h>
#include <engine/behavior/statechart_runner.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    namespace sc = wz::engine::behavior::statechart;

    // Fake scalar board: returns `value` for a matching name, else misses. Keys
    // nothing on entity, so a hit proves the op read the name it authored.
    struct ScalarSource
    {
        std::string name;
        double value = 0.0;
        bool queried = false;
    };

    uint8_t fake_get_scalar(
        void* user, WzBehaviorEntityId /*entity*/, const char* name, double* out)
    {
        auto* src = static_cast<ScalarSource*>(user);
        if (!src || !name || !out) {
            return 0;
        }
        src->queried = true;
        if (src->name != name) {
            return 0;   // an unpublished name -> miss (leaves *out untouched)
        }
        *out = src->value;
        return 1;
    }

    // Captures the measure angle the runner passes, so a value that flowed through
    // read_state -> a measure_at `value` is observable end to end.
    struct ThetaCapture { bool invoked = false; float theta = -1.0f; };

    uint8_t capture_theta(
        void* user, WzBehaviorEntityId, const char*, uint32_t, float theta,
        int8_t* out_bit)
    {
        auto* cap = static_cast<ThetaCapture*>(user);
        if (!cap || !out_bit) {
            return 0;
        }
        cap->invoked = true;
        cap->theta = theta;
        *out_bit = 1;
        return 1;
    }

    // A chart that reads scalar `scalar_name` (op "hp") and measures agent "m" at an
    // angle EQUAL to that reading -- so the captured theta == the published value.
    std::string chart_reading(const std::string& scalar_name)
    {
        return
            R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
            R"("bindings":[],)"
            R"("agents":[{"id":"m","owned":false,"host":"self","agent":"mind"}],)"
            R"("pure":[{"id":"hp","op":"read_state","name":")" + scalar_name + R"("}],)"
            R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
            R"("states":[{"id":"s","do":[)"
            R"({"kind":"measure_at","agent":"m","slot":0,"value":{"op":"hp"}}],)"
            R"("transitions":[]}]})";
    }
}

TEST(StatechartReadState, ParsesReadStateOpWithName)
{
    sc::Chart chart;
    std::string error;
    ASSERT_TRUE(sc::parse_chart(chart_reading("damage"), chart, error)) << error;
    ASSERT_EQ(chart.pure.size(), 1u);
    EXPECT_EQ(chart.pure[0].op, sc::OpKind::ReadState);
    EXPECT_EQ(chart.pure[0].name, "damage");
}

TEST(StatechartReadState, RejectsReadStateWithoutName)
{
    sc::Chart chart;
    std::string error;
    const std::string bad =
        R"({"schema":"wozzits.statechart.ir.v0","name":"t","bindings":[],)"
        R"("agents":[],"pure":[{"id":"hp","op":"read_state"}],)"
        R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","transitions":[]}]})";
    EXPECT_FALSE(sc::parse_chart(bad, chart, error));   // a scalar read needs a name
}

TEST(StatechartReadState, RunnerThreadsPublishedValueIntoEval)
{
    wz::engine::behavior::StatechartRunnerStore store;
    const sc::Chart* chart = store.load("t", chart_reading("damage"));
    ASSERT_NE(chart, nullptr);
    const uint64_t handle = store.create(chart);
    ASSERT_NE(handle, 0u);

    ScalarSource src{ "damage", 42.0, false };
    ThetaCapture cap;
    WzBehaviorFrameFacts facts{};
    facts.entity_scalar_user = &src;
    facts.get_entity_scalar = fake_get_scalar;
    facts.cognition_reader_user = &cap;
    facts.measure_agent_in_basis = capture_theta;

    WzBehaviorEvent event{};
    event.kind = WZ_EVENT_FRAME_UPDATE;
    event.entity = 7u;
    store.tick(handle, &facts, &event);

    // read_state("damage") == 42 flowed through pure eval into the measure angle.
    EXPECT_TRUE(src.queried);
    EXPECT_TRUE(cap.invoked);
    EXPECT_NEAR(cap.theta, 42.0f, 1e-5f);
}

TEST(StatechartReadState, UnpublishedScalarReadsZero)
{
    wz::engine::behavior::StatechartRunnerStore store;
    const sc::Chart* chart = store.load("t", chart_reading("ammo"));
    ASSERT_NE(chart, nullptr);
    const uint64_t handle = store.create(chart);
    ASSERT_NE(handle, 0u);

    // The board publishes "damage", NOT "ammo" -> the read misses -> a well-defined 0.
    ScalarSource src{ "damage", 42.0, false };
    ThetaCapture cap;
    WzBehaviorFrameFacts facts{};
    facts.entity_scalar_user = &src;
    facts.get_entity_scalar = fake_get_scalar;
    facts.cognition_reader_user = &cap;
    facts.measure_agent_in_basis = capture_theta;

    WzBehaviorEvent event{};
    event.kind = WZ_EVENT_FRAME_UPDATE;
    event.entity = 7u;
    store.tick(handle, &facts, &event);

    EXPECT_TRUE(cap.invoked);
    EXPECT_NEAR(cap.theta, 0.0f, 1e-5f);
}
