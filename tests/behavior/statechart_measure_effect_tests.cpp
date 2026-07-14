// Chart-authored non-commuting measurement (seam D): a statechart effect
// `measure_at` projectively measures a NAMED agent's decision along a chosen axis,
// with back-action, via the v36 measure_agent_in_basis ABI. These pin the two
// moving parts without a cognition host: the parse carries agent + slot + angle,
// and the runner threads them into facts.measure_agent_in_basis -- a fake reader
// captures the call, so which agent/slot/angle the effect drove is observable.

#include <engine/behavior/statechart_ir.h>
#include <engine/behavior/statechart_runner.h>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace
{
    namespace sc = wz::engine::behavior::statechart;

    struct MeasureCapture
    {
        bool invoked = false;
        std::string name;
        uint32_t slot = 0xFFFFFFFFu;
        float theta = -1.0f;
        int8_t give = 1;  // the outcome the fake returns (|1>)
    };

    // Fake measure: records the (name, slot, theta) the runner passed and returns a
    // fixed outcome. Keys nothing on entity, so the captured values prove the effect
    // drove the measurement it named -- not some default.
    uint8_t fake_measure(
        void* user, WzBehaviorEntityId /*entity*/, const char* agent_name,
        uint32_t agent_index, float theta, int8_t* out_bit)
    {
        auto* cap = static_cast<MeasureCapture*>(user);
        if (!cap || !out_bit) {
            return 0;
        }
        cap->invoked = true;
        cap->name = agent_name ? agent_name : "";
        cap->slot = agent_index;
        cap->theta = theta;
        *out_bit = cap->give;
        return 1;
    }

    // A chart with one agent ref "m" (named quantum_agent "mind") and a state whose
    // `do` measures decision `slot` of that agent at `angle` radians.
    std::string chart_measuring(uint32_t slot, double angle)
    {
        return
            R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
            R"("bindings":[],)"
            R"("agents":[{"id":"m","owned":false,"host":"self","agent":"mind"}],)"
            R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
            R"("states":[{"id":"s","do":[)"
            R"({"kind":"measure_at","agent":"m","slot":)" + std::to_string(slot) +
            R"(,"value":{"const":)" + std::to_string(angle) + R"(}}],)"
            R"("transitions":[]}]})";
    }
}

TEST(StatechartMeasureEffect, ParsesMeasureAtEffect)
{
    sc::Chart chart;
    std::string error;
    ASSERT_TRUE(sc::parse_chart(chart_measuring(1u, 0.7853981633974483), chart, error))
        << error;
    ASSERT_EQ(chart.agents.size(), 1u);
    EXPECT_EQ(chart.agents[0].agent_name, "mind");
}

TEST(StatechartMeasureEffect, RunnerMeasuresNamedAgentAtAngle)
{
    wz::engine::behavior::StatechartRunnerStore store;
    const double angle = 0.7853981633974483;  // pi/4
    const sc::Chart* chart = store.load("t", chart_measuring(1u, angle));
    ASSERT_NE(chart, nullptr);
    const uint64_t handle = store.create(chart);
    ASSERT_NE(handle, 0u);

    MeasureCapture cap;
    WzBehaviorFrameFacts facts{};
    facts.cognition_reader_user = &cap;
    facts.measure_agent_in_basis = fake_measure;

    WzBehaviorEvent event{};
    event.kind = WZ_EVENT_FRAME_UPDATE;
    event.entity = 7u;
    store.tick(handle, &facts, &event);

    // The effect drove a measurement of the agent it NAMED, at the slot + angle it
    // authored -- the whole chart -> ABI thread.
    EXPECT_TRUE(cap.invoked);
    EXPECT_EQ(cap.name, "mind");
    EXPECT_EQ(cap.slot, 1u);
    EXPECT_NEAR(cap.theta, static_cast<float>(angle), 1e-5f);
}
