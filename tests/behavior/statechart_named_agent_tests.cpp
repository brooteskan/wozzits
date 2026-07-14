// Explicit agent references (v35): a statechart REF (an `owned:false` agent) may
// NAME which quantum_agent on its host node it reads, instead of the engine
// silently grabbing the first quantum_agent found. These pin the two moving parts
// without a scene/cognition host:
//   * parse  -- statechart_ir.cpp carries `"agent":"<label>"` into AgentDecl.agent_name
//   * runner -- the tick threads each agent's target name into the NAMED decision
//               read (facts.get_agent_decision_at_named), so two agents on the same
//               node resolve to DIFFERENT dispositions by name alone.
// The runner reaches the reader only through facts, so a fake get_agent_decision_at_
// named that answers by name fully exercises the threading. The read value is handed
// to a recorded actuator, so which agent was read is observable.

#include <engine/behavior/statechart_ir.h>
#include <engine/behavior/statechart_runner.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    namespace sc = wz::engine::behavior::statechart;

    // A fake named reader: agent "beta" is disposed 0.75, "alpha" 0.25, anything
    // else 0. Keys ONLY on the name, so a captured value proves which name the
    // runner passed -- i.e. that the ref read the agent it named, not the first.
    uint8_t named_decision(
        void* /*user*/, WzBehaviorEntityId /*entity*/, const char* agent_name,
        uint32_t /*agent_index*/, WzAgentDecision* out)
    {
        if (!out) {
            return 0;
        }
        const std::string name = agent_name ? agent_name : "";
        out->committed = -1;
        out->marginal = (name == "beta") ? 0.75f
            : (name == "alpha") ? 0.25f
            : 0.0f;
        return 1;
    }

    struct CapturedScalar { bool invoked = false; double value = -1.0; };

    void capture_actuator(
        const WzBehaviorFrameFacts*, WzBehaviorEntityId,
        const WzActuatorArg* args, uint32_t arg_count, void* user_data)
    {
        auto* cap = static_cast<CapturedScalar*>(user_data);
        cap->invoked = true;
        if (arg_count >= 1 && args[0].kind == WZ_ACTUATOR_ARG_SCALAR) {
            cap->value = args[0].scalar;
        }
    }

    uint8_t capture_lookup(
        void* user, const char* name,
        WzBehaviorActuatorFn* out_fn, void** out_user_data)
    {
        if (name && std::string(name) == "reader") {
            if (out_fn) *out_fn = capture_actuator;
            if (out_user_data) *out_user_data = user;
            return 1;
        }
        return 0;
    }

    // Two co-located quantum_agent REFS (both host:self) named alpha + beta. A pure
    // op reads the marginal of the agent whose id == `read_agent`, and the state's
    // `do` hands that scalar to the "reader" actuator -- so the captured value is
    // exactly the marginal of the NAMED agent.
    std::string chart_reading(const std::string& read_agent)
    {
        return
            R"({"schema":"wozzits.statechart.ir.v0","name":"t",)"
            R"("bindings":[],)"
            R"("agents":[)"
            R"({"id":"alpha","owned":false,"host":"self","agent":"alpha"},)"
            R"({"id":"beta","owned":false,"host":"self","agent":"beta"}],)"
            R"("pure":[{"id":"m","op":"marginal","agent":")" + read_agent +
            R"(","slot":0}],)"
            R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
            R"("states":[{"id":"s","do":[)"
            R"({"kind":"call","fn":"reader","args":[{"op":"m"}]}],)"
            R"("transitions":[]}]})";
    }

    double captured_marginal_for(const std::string& read_agent)
    {
        wz::engine::behavior::StatechartRunnerStore store;
        const sc::Chart* chart = store.load(read_agent, chart_reading(read_agent));
        EXPECT_NE(chart, nullptr);
        const uint64_t handle = store.create(chart);
        EXPECT_NE(handle, 0u);

        CapturedScalar cap;
        WzBehaviorFrameFacts facts{};
        facts.get_agent_decision_at_named = named_decision;
        facts.actuator_registry_user = &cap;
        facts.actuator_lookup = capture_lookup;

        WzBehaviorEvent event{};
        event.kind = WZ_EVENT_FRAME_UPDATE;
        event.entity = 7u;
        store.tick(handle, &facts, &event);

        EXPECT_TRUE(cap.invoked);
        return cap.value;
    }
}

TEST(StatechartNamedAgent, ParseCarriesTheTargetName)
{
    sc::Chart chart;
    std::string error;
    ASSERT_TRUE(sc::parse_chart(chart_reading("beta"), chart, error)) << error;
    ASSERT_EQ(chart.agents.size(), 2u);
    EXPECT_EQ(chart.agents[0].agent_name, "alpha");
    EXPECT_FALSE(chart.agents[0].owned);
    EXPECT_EQ(chart.agents[1].agent_name, "beta");
}

TEST(StatechartNamedAgent, RunnerReadsTheAgentTheRefNames)
{
    // Same chart shape; only which agent the read op names differs. The reader
    // answers by name, so the captured marginal proves the runner threaded the
    // named agent through -- NOT the first-found one.
    EXPECT_DOUBLE_EQ(captured_marginal_for("beta"), 0.75);
    EXPECT_DOUBLE_EQ(captured_marginal_for("alpha"), 0.25);
}
