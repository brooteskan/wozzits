#include <engine/behavior/statechart_ir.h>

#include <external/json/json_document.h>
#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <string_view>
#include <unordered_map>

namespace wz::engine::behavior::statechart
{
    using wz::json::find_member;
    using wz::json::JSONValue;
    using wz::json::JSONValueKind;

    int Chart::index_of_state(std::string_view id) const
    {
        for (size_t i = 0; i < states.size(); ++i)
            if (states[i].id == id) return static_cast<int>(i);
        return -1;
    }
    int Chart::index_of_agent(std::string_view id) const
    {
        for (size_t i = 0; i < agents.size(); ++i)
            if (agents[i].id == id) return static_cast<int>(i);
        return -1;
    }
    int Chart::index_of_binding(std::string_view port) const
    {
        for (size_t i = 0; i < bindings.size(); ++i)
            if (bindings[i].port == port) return static_cast<int>(i);
        return -1;
    }

    namespace
    {
        struct Parser
        {
            Chart& c;
            std::string& err;
            std::unordered_map<std::string, int> pure_i;   // built incrementally

            bool fail(std::string m) { err = std::move(m); return false; }

            static const JSONValue* arr(const JSONValue& o, const char* k)
            {
                const JSONValue* v = find_member(o, k);
                return (v && v->kind == JSONValueKind::Array) ? v : nullptr;
            }
            static std::string str(const JSONValue& o, const char* k)
            {
                const JSONValue* v = find_member(o, k);
                return (v && v->kind == JSONValueKind::String) ? v->string_value
                                                               : std::string{};
            }
            static double num(const JSONValue& o, const char* k, double d = 0.0)
            {
                const JSONValue* v = find_member(o, k);
                return (v && v->kind == JSONValueKind::Number) ? v->number_value : d;
            }

            bool ref(const JSONValue& o, Ref& r)
            {
                if (const JSONValue* cv = find_member(o, "const")) {
                    r.kind = Ref::Kind::Const;
                    if (cv->kind == JSONValueKind::Bool)
                        r.constant = cv->bool_value ? 1.0 : 0.0;
                    else if (cv->kind == JSONValueKind::Number)
                        r.constant = cv->number_value;
                    else return fail("const must be number or bool");
                    return true;
                }
                if (const JSONValue* ov = find_member(o, "op")) {
                    if (ov->kind != JSONValueKind::String)
                        return fail("op ref must be a string id");
                    auto it = pure_i.find(ov->string_value);
                    if (it == pure_i.end())
                        return fail("unknown pure op '" + ov->string_value + "'");
                    r.kind = Ref::Kind::Op;
                    r.op = static_cast<uint16_t>(it->second);
                    return true;
                }
                return fail("ref needs 'const' or 'op'");
            }

            bool op_kind(const std::string& s, OpKind& k)
            {
                static const std::pair<const char*, OpKind> t[] = {
                    {"marginal",OpKind::Marginal},{"committed",OpKind::Committed},
                    {"memory",OpKind::Memory},{"add",OpKind::Add},{"sub",OpKind::Sub},
                    {"mul",OpKind::Mul},{"min",OpKind::Min},{"max",OpKind::Max},
                    {"mul_add",OpKind::MulAdd},{"clamp01",OpKind::Clamp01},
                    {"eq",OpKind::Eq},{"lt",OpKind::Lt},{"gt",OpKind::Gt},
                    {"and",OpKind::And},{"or",OpKind::Or},{"not",OpKind::Not},
                    {"select",OpKind::Select},{"proximity",OpKind::Proximity},
                    {"read_state",OpKind::ReadState},
                };
                for (auto& e : t) if (s == e.first) { k = e.second; return true; }
                return fail("unknown pure op kind '" + s + "'");
            }

            bool pure_op(const JSONValue& o)
            {
                PureOp p{};
                std::string kind = str(o, "op");
                if (!op_kind(kind, p.op)) return false;

                if (p.op == OpKind::Marginal || p.op == OpKind::Committed
                    || p.op == OpKind::Memory) {
                    int a = c.index_of_agent(str(o, "agent"));
                    if (a < 0) return fail("read op names unknown agent");
                    p.agent = static_cast<uint16_t>(a);
                    p.slot = static_cast<uint16_t>(
                        num(o, p.op == OpKind::Memory ? "q" : "slot"));
                }
                else if (p.op == OpKind::Select) {
                    const JSONValue* cd = find_member(o, "cond");
                    const JSONValue* av = find_member(o, "a");
                    const JSONValue* bv = find_member(o, "b");
                    if (!cd || !av || !bv) return fail("select needs cond/a/b");
                    if (!ref(*cd, p.in0) || !ref(*av, p.in1) || !ref(*bv, p.in2))
                        return false;
                }
                else if (p.op == OpKind::Proximity) {
                    // self -> target horizontal distance; slot holds the target
                    // binding index (self is implicit).
                    int b = c.index_of_binding(str(o, "target"));
                    if (b < 0)
                        return fail("proximity names unknown target binding");
                    p.slot = static_cast<uint16_t>(b);
                }
                else if (p.op == OpKind::ReadState) {
                    // A behavior-published named scalar, read on self; `name` is the
                    // open-vocabulary scalar name (resolved at runtime via facts).
                    p.name = str(o, "name");
                    if (p.name.empty())
                        return fail("read_state needs a scalar 'name'");
                }
                else {
                    const JSONValue* ins = arr(o, "ins");
                    if (!ins) return fail("op '" + kind + "' needs 'ins'");
                    Ref* slots[3] = { &p.in0, &p.in1, &p.in2 };
                    for (size_t i = 0; i < ins->array_values.size() && i < 3; ++i)
                        if (!ref(*ins->array_values[i], *slots[i])) return false;
                }

                std::string id = str(o, "id");
                if (id.empty()) return fail("pure op missing id");
                pure_i[id] = static_cast<int>(c.pure.size());
                c.pure.push_back(p);
                return true;
            }

            bool effect(const JSONValue& o, Effect& e)
            {
                std::string k = str(o, "kind");
                auto agent = [&](Effect& ef) {
                    int a = c.index_of_agent(str(o, "agent"));
                    if (a < 0) return false;
                    ef.agent = static_cast<uint16_t>(a);
                    return true;
                };
                auto target = [&](Effect& ef) {
                    const JSONValue* t = find_member(o, "target");
                    std::string port = t ? [&]{
                        const JSONValue* b = find_member(*t, "bind");
                        return (b && b->kind == JSONValueKind::String) ? b->string_value : std::string{};
                    }() : std::string{};
                    int bi = c.index_of_binding(port);
                    if (bi < 0) return false;
                    ef.binding = static_cast<uint16_t>(bi);
                    return true;
                };
                auto value = [&](Effect& ef) {
                    const JSONValue* v = find_member(o, "value");
                    return v && ref(*v, ef.value);
                };

                if (k == "set_goal") {
                    e.kind = EffectKind::SetGoal;
                    if (!agent(e) || !value(e)) return fail("set_goal");
                    e.slot = static_cast<uint16_t>(num(o, "slot"));
                }
                else if (k == "measure_at") {
                    // Chart-timed non-commuting measurement: measure decision `slot`
                    // of the named agent along axis `value` (radians). Shape mirrors
                    // set_goal (agent + slot + a value Ref, here the angle).
                    e.kind = EffectKind::MeasureAt;
                    if (!agent(e) || !value(e)) return fail("measure_at");
                    e.slot = static_cast<uint16_t>(num(o, "slot"));
                }
                else if (k == "set_decoherence") {
                    e.kind = EffectKind::SetDecoherence;
                    if (!agent(e) || !value(e)) return fail("set_decoherence");
                }
                else if (k == "rearm") {
                    e.kind = EffectKind::Rearm;
                    if (!agent(e)) return fail("rearm");
                }
                else if (k == "reward") {
                    e.kind = EffectKind::Reward;
                    if (!agent(e)) return fail("reward");
                    e.slot = static_cast<uint16_t>(num(o, "q"));
                    // VALUE semantics (behavior ABI v38): `value` names the branch
                    // being reinforced -- true is the 1 branch, driving that fact's
                    // memory_preference toward -1. Matches reward_pair and
                    // committed().
                    //
                    // The key used to be `toward`, and it meant the OPPOSITE:
                    // toward == true selected the 0 branch. A chart carrying the old
                    // key would still parse and would learn BACKWARDS, silently, so
                    // this refuses it rather than guessing. Porting a chart is
                    // mechanical: rename the key and flip the boolean.
                    if (find_member(o, "toward")) {
                        return fail(
                            "reward: 'toward' was replaced by 'value' with the "
                            "OPPOSITE meaning (ABI v38) -- true now names the 1 "
                            "branch. Rename the key and flip the boolean.");
                    }
                    const JSONValue* tw = find_member(o, "value");
                    e.flag = (tw && tw->kind == JSONValueKind::Bool && tw->bool_value)
                        ? 1u : 0u;
                    const JSONValue* st = find_member(o, "strength");
                    if (st && !ref(*st, e.value)) return false;
                }
                else if (k == "set_scale") {
                    e.kind = EffectKind::SetScale;
                    if (!target(e) || !value(e)) return fail("set_scale");
                }
                else if (k == "set_visible") {
                    e.kind = EffectKind::SetVisible;
                    if (!target(e) || !value(e)) return fail("set_visible");
                }
                else if (k == "play_sound") {
                    e.kind = EffectKind::PlaySound;
                    if (!target(e)) return fail("play_sound");
                }
                else if (k == "call") {
                    // A behavior-registered actuator, called by name. Args resolve
                    // against the chart here (bind->binding index, agent->agent
                    // index, scalar->const/op Ref); the `fn` NAME resolves late, in
                    // the runner, against the live actuator registry.
                    e.kind = EffectKind::Call;
                    e.call = str(o, "fn");
                    if (e.call.empty()) return fail("call needs 'fn'");
                    if (const JSONValue* a = arr(o, "args")) {
                        for (auto& av : a->array_values) {
                            CallArg ca{};
                            if (const JSONValue* b = find_member(*av, "bind")) {
                                if (b->kind != JSONValueKind::String)
                                    return fail("call arg 'bind' must be a string");
                                int bi = c.index_of_binding(b->string_value);
                                if (bi < 0)
                                    return fail("call arg binds unknown port '"
                                        + b->string_value + "'");
                                ca.kind = CallArg::Kind::Binding;
                                ca.binding = static_cast<uint16_t>(bi);
                            }
                            else if (const JSONValue* ag =
                                         find_member(*av, "agent")) {
                                if (ag->kind != JSONValueKind::String)
                                    return fail("call arg 'agent' must be a string");
                                int ai = c.index_of_agent(ag->string_value);
                                if (ai < 0)
                                    return fail("call arg names unknown agent '"
                                        + ag->string_value + "'");
                                ca.kind = CallArg::Kind::Agent;
                                ca.agent = static_cast<uint16_t>(ai);
                            }
                            else {
                                ca.kind = CallArg::Kind::Scalar;
                                if (!ref(*av, ca.scalar)) return false;
                            }
                            e.call_args.push_back(ca);
                        }
                    }
                }
                else {
                    return fail("unknown effect kind '" + k + "'");
                }
                return true;
            }

            bool effect_list(const JSONValue& o, const char* key,
                std::vector<Effect>& out)
            {
                const JSONValue* a = arr(o, key);
                if (!a) return true;
                for (auto& ev : a->array_values) {
                    Effect e{};
                    if (!effect(*ev, e)) return false;
                    out.push_back(e);
                }
                return true;
            }

            bool trigger(const JSONValue& o, Trigger& t)
            {
                std::string k = str(o, "kind");
                if (k == "commit") {
                    t.kind = TriggerKind::Commit;
                    int a = c.index_of_agent(str(o, "agent"));
                    if (a < 0) return fail("commit names unknown agent");
                    t.agent = static_cast<uint16_t>(a);
                    t.slot = static_cast<uint16_t>(num(o, "slot"));
                    const JSONValue* ov = find_member(o, "outcome");
                    t.outcome = (ov && ov->kind == JSONValueKind::Number)
                        ? static_cast<int8_t>(ov->number_value) : -2;  // "any"
                }
                else if (k == "after") {
                    t.kind = TriggerKind::After;
                    t.seconds = static_cast<float>(num(o, "seconds"));
                }
                else if (k == "guard") {
                    t.kind = TriggerKind::Guard;
                    const JSONValue* cv = find_member(o, "cond");
                    if (!cv || !ref(*cv, t.cond)) return fail("guard needs cond");
                }
                else if (k == "event") {
                    t.kind = TriggerKind::Event;
                    t.event_name = str(o, "name");   // a behavior-emitted event name
                }
                else return fail("unknown trigger kind '" + k + "'");
                return true;
            }

            bool run()
            {
                const JSONValue& root = *doc.root;
                if (root.kind != JSONValueKind::Object)
                    return fail("chart root must be an object");
                c.name = str(root, "name");

                if (const JSONValue* bs = arr(root, "bindings"))
                    for (auto& b : bs->array_values) {
                        Binding bd;
                        bd.port = str(*b, "port");
                        bd.find = str(*b, "find");
                        bd.subtree = str(*b, "scope") != "global";
                        c.bindings.push_back(std::move(bd));
                    }

                if (const JSONValue* as = arr(root, "agents"))
                    for (auto& a : as->array_values) {
                        AgentDecl ad;
                        ad.id = str(*a, "id");
                        const JSONValue* ow = find_member(*a, "owned");
                        ad.owned = !(ow && ow->kind == JSONValueKind::Bool)
                            || ow->bool_value;
                        std::string host = str(*a, "host");
                        if (host.empty() || host == "self")
                            ad.host_binding = kSelfBinding;
                        else {
                            int bi = c.index_of_binding(host);
                            if (bi < 0) return fail("agent host '" + host
                                + "' is not a binding port");
                            ad.host_binding = static_cast<uint16_t>(bi);
                        }
                        // Optional: name WHICH quantum_agent on the host to read (a REF
                        // that disambiguates a node with several). Empty = first-match.
                        ad.agent_name = str(*a, "agent");
                        c.agents.push_back(std::move(ad));
                    }

                if (const JSONValue* ps = arr(root, "pure"))
                    for (auto& p : ps->array_values)
                        if (!pure_op(*p)) return false;

                // States: collect ids first (transitions target states by id),
                // then parse bodies.
                const JSONValue* ss = arr(root, "states");
                if (ss)
                    for (auto& s : ss->array_values)
                        c.states.push_back(State{ str(*s, "id"), {}, {}, {}, {} });
                if (ss) {
                    size_t idx = 0;
                    for (auto& s : ss->array_values) {
                        State& st = c.states[idx++];
                        if (!effect_list(*s, "do", st.doe)) return false;
                        if (!effect_list(*s, "entry", st.entry)) return false;
                        if (!effect_list(*s, "exit", st.exit)) return false;
                        if (const JSONValue* ts = arr(*s, "transitions"))
                            for (auto& t : ts->array_values) {
                                Transition tr;
                                const JSONValue* trg = find_member(*t, "trigger");
                                if (!trg || !trigger(*trg, tr.trigger))
                                    return fail("bad transition trigger");
                                if (!effect_list(*t, "actions", tr.actions))
                                    return false;
                                int tgt = c.index_of_state(str(*t, "target"));
                                if (tgt < 0) return fail("transition target '"
                                    + str(*t, "target") + "' is unknown");
                                tr.target = static_cast<uint16_t>(tgt);
                                st.transitions.push_back(std::move(tr));
                            }
                    }
                }

                if (const JSONValue* rs = arr(root, "regions"))
                    for (auto& r : rs->array_values) {
                        Region rg;
                        rg.id = str(*r, "id");
                        int init = c.index_of_state(str(*r, "initial"));
                        if (init < 0) return fail("region initial state unknown");
                        rg.initial = static_cast<uint16_t>(init);
                        if (const JSONValue* sl = arr(*r, "states"))
                            for (auto& sid : sl->array_values) {
                                int si = c.index_of_state(sid->string_value);
                                if (si < 0) return fail("region names unknown state");
                                rg.states.push_back(static_cast<uint16_t>(si));
                            }
                        c.regions.push_back(std::move(rg));
                    }

                if (c.regions.empty()) return fail("chart has no regions");
                return true;
            }

            const wz::json::JSONDocument& doc;
        };
    }

    bool parse_chart(const std::string& json_text, Chart& out, std::string& error)
    {
        wz::json::JSONParseResult res = wz::json::parse_json_string(json_text);
        if (!res.ok || !res.document.root) {
            error = "JSON parse failed: " + res.error.message;
            return false;
        }
        Parser p{ out, error, {}, res.document };
        return p.run();
    }
}
