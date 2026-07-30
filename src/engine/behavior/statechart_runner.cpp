#include <engine/behavior/statechart_runner.h>

#include <engine/behavior/behavior_module_api.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace wz::engine::behavior
{
    namespace sc = wz::engine::behavior::statechart;

    StatechartRunnerStore& statechart_runner_store()
    {
        static StatechartRunnerStore instance;
        return instance;
    }

    const sc::Chart* StatechartRunnerStore::load(
        const std::string& name,
        const std::string& ir_text,
        std::string* error)
    {
        auto it = charts_.find(name);
        if (it != charts_.end() && it->second.source_ir == ir_text) {
            return &it->second.chart;   // same IR under this name -> reuse the cached parse
        }
        sc::Chart chart;
        std::string parse_error;
        if (!sc::parse_chart(ir_text, chart, parse_error)) {
            // Hand the reason back. It used to be collected into a local and dropped
            // on the floor, so every one of parse_chart's specific messages died here
            // and the author was told only that the IR was bad.
            if (error != nullptr) {
                *error = std::move(parse_error);
            }
            return nullptr;
        }
        // Insert or REPLACE: an edited chart re-embeds under the same name with new IR, so the
        // old parse must be overwritten (else a restart keeps running the stale chart). Safe
        // because load runs on self.start (fresh runtimes); prior runtimes were destroyed on the
        // previous scene teardown.
        auto& slot = charts_[name];
        slot.source_ir = ir_text;
        slot.chart = std::move(chart);
        return &slot.chart;
    }

    uint64_t StatechartRunnerStore::create(const sc::Chart* chart)
    {
        if (!chart) {
            return 0;
        }
        Runtime rt;
        rt.chart = chart;
        rt.active_state.resize(chart->regions.size());
        rt.time_in_state.assign(chart->regions.size(), 0.0f);
        for (size_t r = 0; r < chart->regions.size(); ++r) {
            rt.active_state[r] = chart->regions[r].initial;
        }
        rt.pure_out.assign(chart->pure.size(), 0.0);

        // Collect the distinct (agent, slot) pairs referenced by commit triggers.
        for (const sc::State& st : chart->states) {
            for (const sc::Transition& t : st.transitions) {
                if (t.trigger.kind != sc::TriggerKind::Commit) {
                    continue;
                }
                bool have = false;
                for (auto& cr : rt.commit_refs) {
                    if (cr.first == t.trigger.agent && cr.second == t.trigger.slot) {
                        have = true;
                        break;
                    }
                }
                if (!have) {
                    rt.commit_refs.emplace_back(t.trigger.agent, t.trigger.slot);
                }
            }
        }
        rt.last_committed.assign(rt.commit_refs.size(), static_cast<int8_t>(-2));

        const uint64_t h = next_++;
        runtimes_.emplace(h, std::move(rt));
        return h;
    }

    bool StatechartRunnerStore::destroy(uint64_t handle)
    {
        return runtimes_.erase(handle) > 0;
    }

    std::size_t StatechartRunnerStore::retain(const std::vector<uint64_t>& live)
    {
        std::size_t dropped = 0;
        for (auto it = runtimes_.begin(); it != runtimes_.end();) {
            if (std::find(live.begin(), live.end(), it->first) == live.end()) {
                it = runtimes_.erase(it);
                ++dropped;
            }
            else {
                ++it;
            }
        }
        return dropped;
    }

    void StatechartRunnerStore::tick(
        uint64_t handle,
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event)
    {
        auto it = runtimes_.find(handle);
        if (it == runtimes_.end() || !it->second.chart) {
            return;
        }
        Runtime& rt = it->second;
        const sc::Chart& c = *rt.chart;
        const WzBehaviorEntityId self = wz_self(event);
        const float dt = wz_delta_seconds(facts);

        // Resolve bound entities every frame (robust to scene rebuilds), like the
        // gallery plugins did with per-frame find_descendant.
        std::vector<WzBehaviorEntityId> ent(
            c.bindings.size(),
            static_cast<WzBehaviorEntityId>(WZ_INVALID_BEHAVIOR_ENTITY));
        for (size_t i = 0; i < c.bindings.size(); ++i) {
            const sc::Binding& b = c.bindings[i];
            WzBehaviorEntityId e =
                static_cast<WzBehaviorEntityId>(WZ_INVALID_BEHAVIOR_ENTITY);
            if (b.subtree) {
                wz_find_descendant_by_name(facts, self, b.find.c_str(), &e);
            }
            else {
                wz_find_entity_by_name(facts, b.find.c_str(), &e);
            }
            ent[i] = e;
        }
        std::vector<WzBehaviorEntityId> host(c.agents.size(), self);
        // Each agent may NAME which quantum_agent on its host node to read (empty ==
        // the first one). This makes a ref unambiguous when the node hosts several.
        std::vector<std::string> host_agent(c.agents.size());
        for (size_t i = 0; i < c.agents.size(); ++i) {
            const uint16_t hb = c.agents[i].host_binding;
            host[i] = (hb == sc::kSelfBinding)
                ? self
                : (hb < ent.size() ? ent[hb] : self);
            host_agent[i] = c.agents[i].agent_name;
        }

        auto decision = [&](uint16_t agent, uint16_t slot) -> WzAgentDecision {
            WzAgentDecision d{};
            d.committed = -1;
            d.marginal = 0.0f;
            if (agent < host.size()) {
                wz_agent_decision_at_named(
                    facts, host[agent], host_agent[agent].c_str(), slot, &d);
            }
            return d;
        };
        auto eref = [&](const sc::Ref& r) -> double {
            return r.kind == sc::Ref::Kind::Const
                ? r.constant
                : (r.op < rt.pure_out.size() ? rt.pure_out[r.op] : 0.0);
        };

        // 1. PURE PASS (topo order; no side effects).
        for (size_t i = 0; i < c.pure.size(); ++i) {
            const sc::PureOp& op = c.pure[i];
            double v = 0.0;
            using K = sc::OpKind;
            switch (op.op) {
            case K::Marginal:  v = decision(op.agent, op.slot).marginal; break;
            case K::Committed: v = decision(op.agent, op.slot).committed; break;
            case K::Memory:
                v = (op.agent < host.size())
                    ? wz_agent_memory(facts, host[op.agent], op.slot) : 0.0;
                break;
            case K::Add: v = eref(op.in0) + eref(op.in1); break;
            case K::Sub: v = eref(op.in0) - eref(op.in1); break;
            case K::Mul: v = eref(op.in0) * eref(op.in1); break;
            case K::Min: v = std::min(eref(op.in0), eref(op.in1)); break;
            case K::Max: v = std::max(eref(op.in0), eref(op.in1)); break;
            case K::MulAdd: v = eref(op.in0) * eref(op.in1) + eref(op.in2); break;
            case K::Clamp01: { double x = eref(op.in0); v = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); } break;
            case K::Eq: v = (eref(op.in0) == eref(op.in1)) ? 1.0 : 0.0; break;
            case K::Lt: v = (eref(op.in0) <  eref(op.in1)) ? 1.0 : 0.0; break;
            case K::Gt: v = (eref(op.in0) >  eref(op.in1)) ? 1.0 : 0.0; break;
            case K::And: v = (eref(op.in0) != 0.0 && eref(op.in1) != 0.0) ? 1.0 : 0.0; break;
            case K::Or:  v = (eref(op.in0) != 0.0 || eref(op.in1) != 0.0) ? 1.0 : 0.0; break;
            case K::Not: v = (eref(op.in0) == 0.0) ? 1.0 : 0.0; break;
            case K::Select: v = (eref(op.in0) != 0.0) ? eref(op.in1) : eref(op.in2); break;
            case K::Proximity: {
                WzVec3 me{}, them{};
                const WzBehaviorEntityId tgt = (op.slot < ent.size())
                    ? ent[op.slot]
                    : static_cast<WzBehaviorEntityId>(WZ_INVALID_BEHAVIOR_ENTITY);
                if (wz_read_world_position(facts, self, &me)
                    && wz_read_world_position(facts, tgt, &them)) {
                    const double dx = me.x - them.x;
                    const double dz = me.z - them.z;   // horizontal only
                    v = std::sqrt(dx * dx + dz * dz);
                }
                else {
                    v = 1.0e9;   // no target resolved -> effectively unobserved
                }
            } break;
            case K::ReadState: {
                // A behavior-published named scalar on self (v37). 0 when nothing
                // publishes it -- a guard then reads a well-defined "unset" value.
                double out = 0.0;
                wz_get_entity_scalar(facts, self, op.name.c_str(), &out);
                v = out;
            } break;
            }
            rt.pure_out[i] = v;
        }

        // Snapshot committed for the commit-refs (rising-edge detection).
        std::vector<int8_t> cur(rt.commit_refs.size(), static_cast<int8_t>(-1));
        for (size_t j = 0; j < rt.commit_refs.size(); ++j) {
            cur[j] = decision(rt.commit_refs[j].first, rt.commit_refs[j].second)
                .committed;
        }

        // Behavior-defined events delivered to self this frame -> their names, for
        // `event` triggers. A behavior emits (wz_emit_behavior_event) and a chart on
        // the target reacts here -- the piece commit/after/guard can't cover.
        std::vector<const char*> self_events;
        if (facts->behavior_events.read) {
            for (uint32_t i = 0; i < facts->behavior_events.count; ++i) {
                WzBehaviorEventEntry e{};
                if (facts->behavior_events.read(
                        facts->behavior_events.user, i, &e)
                    && e.target == self && e.name)
                {
                    self_events.push_back(e.name);
                }
            }
        }

        auto ref_index = [&](uint16_t agent, uint16_t slot) -> int {
            for (size_t j = 0; j < rt.commit_refs.size(); ++j) {
                if (rt.commit_refs[j].first == agent
                    && rt.commit_refs[j].second == slot) {
                    return static_cast<int>(j);
                }
            }
            return -1;
        };
        auto fires = [&](const sc::Trigger& t, size_t region) -> bool {
            switch (t.kind) {
            case sc::TriggerKind::Commit: {
                const int j = ref_index(t.agent, t.slot);
                if (j < 0) return false;
                const int8_t cc = cur[j];
                if (cc < 0) return false;                       // not committed
                if (rt.last_committed[j] >= 0) return false;    // not a rising edge
                return t.outcome < 0 || cc == t.outcome;        // -2 = any
            }
            case sc::TriggerKind::After:
                return rt.time_in_state[region] >= t.seconds;
            case sc::TriggerKind::Guard:
                return eref(t.cond) != 0.0;
            case sc::TriggerKind::Event:
                for (const char* n : self_events) {
                    if (t.event_name == n) {   // std::string == const char*
                        return true;
                    }
                }
                return false;
            }
            return false;
        };

        auto apply = [&](const sc::Effect& e) {
            const WzBehaviorEntityId tgt = (e.binding < ent.size())
                ? ent[e.binding]
                : static_cast<WzBehaviorEntityId>(WZ_INVALID_BEHAVIOR_ENTITY);
            const WzBehaviorEntityId ah = (e.agent < host.size())
                ? host[e.agent] : self;
            using EK = sc::EffectKind;
            switch (e.kind) {
            case EK::SetScale: {
                const float s = static_cast<float>(eref(e.value));
                wz_write_set_local_scale(facts, tgt, s, s, s);
            } break;
            case EK::SetVisible:
                wz_write_set_visible(facts, tgt, eref(e.value) != 0.0 ? 1u : 0u);
                break;
            case EK::SetGoal:
                wz_set_agent_goal(facts, ah, e.slot,
                    static_cast<float>(eref(e.value)));
                break;
            case EK::SetDecoherence:
                wz_set_agent_decoherence(facts, ah,
                    static_cast<float>(eref(e.value)));
                break;
            case EK::Rearm:
                wz_rearm_agent(facts, ah);
                break;
            case EK::Reward:
                wz_agent_reward(facts, ah, e.slot, e.flag,
                    static_cast<float>(eref(e.value)));
                break;
            case EK::MeasureAt: {
                // Chart-timed NON-COMMUTING measurement (v36): projectively measure
                // the named agent's decision `slot` along axis `value` (radians),
                // with back-action. The outcome latches on the agent (and the adapter
                // refreshes the module cache), so a Committed op on this slot reads it
                // back on the next evaluation. Named like the read.
                int8_t bit = 0;
                const char* name = (e.agent < host_agent.size())
                    ? host_agent[e.agent].c_str() : "";
                wz_measure_agent_in_basis(
                    facts, ah, name, e.slot,
                    static_cast<float>(eref(e.value)), &bit);
            } break;
            case EK::Call: {
                // A behavior-registered actuator: resolve it by name against the
                // live registry (through facts), marshal the chart-resolved args,
                // and call it with the SAME facts the runner is dispatching under.
                if (!facts->actuator_lookup) {
                    break;
                }
                WzBehaviorActuatorFn fn = nullptr;
                void* ud = nullptr;
                if (!facts->actuator_lookup(
                        facts->actuator_registry_user,
                        e.call.c_str(), &fn, &ud)
                    || !fn) {
                    break;   // unknown actuator -> no-op (a hand-authored typo)
                }
                constexpr size_t kMaxArgs = 8;
                WzActuatorArg argv[kMaxArgs]{};
                const size_t n = std::min(e.call_args.size(), kMaxArgs);
                for (size_t i = 0; i < n; ++i) {
                    const sc::CallArg& ca = e.call_args[i];
                    WzActuatorArg& a = argv[i];
                    switch (ca.kind) {
                    case sc::CallArg::Kind::Binding:
                        a.kind = WZ_ACTUATOR_ARG_ENTITY;
                        a.entity = (ca.binding < ent.size())
                            ? ent[ca.binding]
                            : static_cast<WzBehaviorEntityId>(
                                  WZ_INVALID_BEHAVIOR_ENTITY);
                        break;
                    case sc::CallArg::Kind::Agent:
                        a.kind = WZ_ACTUATOR_ARG_ENTITY;
                        a.entity = (ca.agent < host.size())
                            ? host[ca.agent] : self;
                        break;
                    case sc::CallArg::Kind::Scalar:
                    default:
                        a.kind = WZ_ACTUATOR_ARG_SCALAR;
                        a.scalar = eref(ca.scalar);
                        break;
                    }
                }
                fn(facts, self, argv, static_cast<uint32_t>(n), ud);
            } break;
            case EK::Reshape:
            case EK::Move:
            case EK::PlaySound:
                break;   // Seam 2 / 3
            }
        };
        auto run = [&](const std::vector<sc::Effect>& list) {
            for (const sc::Effect& e : list) {
                apply(e);
            }
        };

        // 2. CONTROL STEP (one transition per region per frame).
        for (size_t r = 0; r < c.regions.size(); ++r) {
            const sc::State& s = c.states[rt.active_state[r]];
            for (const sc::Transition& t : s.transitions) {
                if (fires(t.trigger, r)) {
                    run(s.exit);
                    run(t.actions);
                    rt.active_state[r] = t.target;
                    rt.time_in_state[r] = 0.0f;
                    run(c.states[t.target].entry);
                    break;
                }
            }
        }

        // 3. CONTINUOUS EFFECTS (v0: one active state/region -> apply directly;
        //    channel summation across regions lands in Seam 2).
        for (size_t r = 0; r < c.regions.size(); ++r) {
            run(c.states[rt.active_state[r]].doe);
        }

        // 4. ADVANCE.
        for (float& t : rt.time_in_state) {
            t += dt;
        }
        for (size_t j = 0; j < rt.commit_refs.size(); ++j) {
            rt.last_committed[j] = cur[j];
        }
    }

    // ── builtin module ───────────────────────────────────────────────────────

    namespace
    {
        std::string read_config_string(
            const WzBehaviorFrameFacts* facts, const char* key)
        {
            // get_config_string returns 0 when the buffer is too small, but it
            // STILL writes `required` (= length + 1). So probe for the size and do
            // NOT bail on the first call's return value -- a large value (e.g. a
            // 2KB embedded IR) always fails the probe and needs the sized read.
            char probe[256];
            uint32_t required = 0;
            wz_config_string(facts, key, probe, sizeof(probe), &required);
            if (required == 0) {
                return {};   // key absent
            }
            if (required <= sizeof(probe)) {
                return std::string(probe);   // fit (null-terminated in probe)
            }
            std::vector<char> big(required, '\0');   // required includes the null
            uint32_t got = 0;
            wz_config_string(
                facts, key, big.data(),
                static_cast<uint32_t>(big.size()), &got);
            return std::string(big.data());
        }

        void statechart_runner_on_init(
            const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
        {
            (void)wz_instance_state<StatechartRunnerState>(facts);
        }

        void statechart_runner_on_event(
            const WzBehaviorFrameFacts* facts,
            const WzBehaviorEvent* event,
            void*)
        {
            if (!facts || !event) {
                return;
            }
            StatechartRunnerState* s =
                wz_instance_state<StatechartRunnerState>(facts);
            if (!s) {
                return;
            }
            if (event->kind == WZ_EVENT_SELF_START) {
                if (s->handle != 0) {
                    return;   // already built (preserved across reloads)
                }
                std::string name = read_config_string(facts, "chart");
                std::string ir = read_config_string(facts, "chart_ir");
                if (name.empty()) {
                    name = "chart";
                }
                std::string load_error;
                const sc::Chart* chart =
                    statechart_runner_store().load(name, ir, &load_error);
                if (!chart) {
                    // ERROR, not info (v39): this runner is now inert for the whole
                    // session while the scene around it keeps running and the process
                    // exits 0. At INFO this was indistinguishable from the "loaded"
                    // line below, and a broken chart rode through two verification
                    // passes on the strength of a clean exit code.
                    //
                    // Carry the REASON, not just the verdict: parse_chart names the
                    // exact defect ("transition target 'X' is unknown", "chart has no
                    // regions", the reward 'toward'/'branch' port instruction), and
                    // that string is the whole difference between a fixable error and
                    // "my behaviour does nothing". An empty IR reports as a JSON parse
                    // failure, which is also the answer (a runner with no chart_ir).
                    wz_log_errorf(
                        facts,
                        "[statechart] '%s' failed to load: %s",
                        name.c_str(),
                        load_error.empty() ? "bad IR" : load_error.c_str());
                    return;
                }
                s->handle = statechart_runner_store().create(chart);
                wz_log_infof(
                    facts,
                    "[statechart] '%s' loaded: regions=%u states=%u pure=%u",
                    name.c_str(),
                    static_cast<unsigned>(chart->regions.size()),
                    static_cast<unsigned>(chart->states.size()),
                    static_cast<unsigned>(chart->pure.size()));
                return;
            }
            if (event->kind == WZ_EVENT_FRAME_UPDATE && s->handle != 0) {
                statechart_runner_store().tick(s->handle, facts, event);
            }
        }
    }

    uint8_t register_statechart_runner_behaviors(WzBehaviorPluginApi* api)
    {
        if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
            || !api->register_module_desc) {
            return 0;
        }

        static const char* channels[] = { "self.start", "frame.update" };
        static const WzBehaviorParamDesc params[] = {
            { "chart", "Chart name", WZ_BEHAVIOR_PARAM_STRING, 0.0, "" },
            { "chart_ir", "Chart IR (JSON)", WZ_BEHAVIOR_PARAM_STRING, 0.0, "" },
        };

        WzBehaviorModuleDesc desc{};
        desc.size = sizeof(desc);
        desc.module = "statechart_runner";
        desc.on_event = statechart_runner_on_event;
        desc.on_init = statechart_runner_on_init;
        desc.event_channels = channels;
        desc.event_channel_count = 2u;
        desc.module_user_data = nullptr;
        desc.params = params;
        desc.param_count = static_cast<uint32_t>(
            sizeof(params) / sizeof(params[0]));
        return api->register_module_desc(api->user, &desc);
    }
}
