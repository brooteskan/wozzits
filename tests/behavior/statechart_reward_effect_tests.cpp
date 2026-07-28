// The chart-authored `reward` effect's branch flag. Behavior ABI v38 flipped
// reward_agent's third argument to VALUE semantics -- true names the 1 branch --
// so it stops inverting reward_agent_pair. The chart IR key changed with it:
// `toward` (true = the 0 branch) became `value` (true = the 1 branch).
//
// This is exactly the kind of change that cannot be allowed to pass silently: a
// chart carrying the old key would still be perfectly valid JSON, would parse,
// and would learn BACKWARDS with nothing to show for it. The parse refuses it.

#include <engine/behavior/statechart_ir.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    namespace sc = wz::engine::behavior::statechart;

    std::string chart_with_reward_action(const std::string& action)
    {
        return
            R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
            R"("bindings":[],)"
            R"("agents":[{"id":"a","owned":false,"host":"self","agent":"mind"}],)"
            R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
            R"("states":[{"id":"s","entry":[)"
            + action +
            R"(],"transitions":[]}]})";
    }
}

// `value: true` names the 1 branch, and reaches the runner as flag 1.
TEST(StatechartRewardEffect, ValueTrueSelectsTheOneBranch)
{
    sc::Chart chart;
    std::string error;
    ASSERT_TRUE(sc::parse_chart(
        chart_with_reward_action(
            R"({"kind":"reward","agent":"a","q":2,"value":true,)"
            R"("strength":{"const":0.5}})"),
        chart, error)) << error;

    ASSERT_EQ(chart.states.size(), 1u);
    ASSERT_EQ(chart.states[0].entry.size(), 1u);
    const sc::Effect& e = chart.states[0].entry[0];
    EXPECT_EQ(e.kind, sc::EffectKind::Reward);
    EXPECT_EQ(e.slot, 2u);
    EXPECT_EQ(e.flag, 1u);
}

// `value: false` names the 0 branch, and an absent `value` defaults to it.
TEST(StatechartRewardEffect, ValueFalseAndAbsentSelectTheZeroBranch)
{
    for (const char* action : {
             R"({"kind":"reward","agent":"a","q":0,"value":false,)"
             R"("strength":{"const":0.5}})",
             R"({"kind":"reward","agent":"a","q":0,"strength":{"const":0.5}})" }) {
        sc::Chart chart;
        std::string error;
        ASSERT_TRUE(sc::parse_chart(chart_with_reward_action(action), chart, error))
            << error << "  [" << action << "]";
        ASSERT_EQ(chart.states[0].entry.size(), 1u);
        EXPECT_EQ(chart.states[0].entry[0].flag, 0u) << action;
    }
}

// The legacy `toward` key is REFUSED, not silently reinterpreted. It meant the
// opposite, so accepting it would flip what every authored chart learned -- a
// failure with no symptom until someone noticed the agent preferring the branch
// that never paid off. The message has to say what to do about it.
TEST(StatechartRewardEffect, LegacyTowardKeyIsRefusedWithAnActionableMessage)
{
    for (const char* legacy : {
             R"({"kind":"reward","agent":"a","q":0,"toward":true,)"
             R"("strength":{"const":0.5}})",
             R"({"kind":"reward","agent":"a","q":0,"toward":false,)"
             R"("strength":{"const":0.5}})" }) {
        sc::Chart chart;
        std::string error;
        EXPECT_FALSE(sc::parse_chart(chart_with_reward_action(legacy), chart, error))
            << "a chart carrying the pre-v38 key must not parse: " << legacy;
        EXPECT_NE(error.find("toward"), std::string::npos) << error;
        EXPECT_NE(error.find("value"), std::string::npos) << error;
    }
}
