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

            // Read an integral chart field, FAILING the parse when the number will not
            // fit. static_cast<uint16_t>(1e30) is undefined behaviour, and a chart is
            // authored data: a typo'd `"slot": -1` silently becoming slot 65535 reads
            // an unrelated decision and the chart just behaves oddly. An absent or
            // non-number member keeps `dflt`, matching num() above -- only a present,
            // out-of-range number is an error.
            template <typename T>
            bool integral(const JSONValue& o, const char* k, T& out, T dflt = T{})
            {
                const JSONValue* v = find_member(o, k);
                if (!v || v->kind != JSONValueKind::Number) {
                    out = dflt;
                    return true;
                }
                const auto narrowed = wz::json::narrow_number<T>(v->number_value);
                if (!narrowed) {
                    return fail(
                        std::string("'") + k + "' is out of range for this field");
                }
                out = *narrowed;
                return true;
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
                    if (!integral(
                            o,
                            p.op == OpKind::Memory ? "q" : "slot",
                            p.slot))
                    {
                        return false;
                    }
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
                // A duplicate would not merely shadow: refs resolved BEFORE it keep
                // pointing at the earlier op while later refs get this one, so one id
                // carries two values in the same chart. Refuse it -- the editor
                // cannot even serialize a duplicate (TopoSortPure throws).
                if (pure_i.contains(id))
                    return fail("duplicate pure op id '" + id + "'");
                pure_i[id] = static_cast<int>(c.pure.size());
                c.pure.push_back(p);
                return true;
            }

            bool effect(const JSONValue& o, Effect& e)
            {
                std::string k = str(o, "kind");
                // These three set a SPECIFIC parse reason on failure, so the
                // per-kind call sites below can `return false` and preserve it
                // rather than clobbering it with the bare effect-kind word. D2-H4
                // did this for trigger(); A3-H7 (#77 visit 2) finishes effect().
                auto agent = [&](Effect& ef) {
                    const std::string name = str(o, "agent");
                    int a = c.index_of_agent(name);
                    if (a < 0) return fail(k + ": unknown agent '" + name + "'");
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
                    if (bi < 0) return fail(k + ": unknown target binding '" + port + "'");
                    ef.binding = static_cast<uint16_t>(bi);
                    return true;
                };
                auto value = [&](Effect& ef) {
                    const JSONValue* v = find_member(o, "value");
                    if (!v) return fail(k + ": missing `value`");
                    return ref(*v, ef.value);   // ref() sets a specific reason
                };

                if (k == "set_goal") {
                    e.kind = EffectKind::SetGoal;
                    if (!agent(e) || !value(e)) return false;
                    if (!integral(o, "slot", e.slot)) return false;
                }
                else if (k == "measure_at") {
                    // Chart-timed non-commuting measurement: measure decision `slot`
                    // of the named agent along axis `value` (radians). Shape mirrors
                    // set_goal (agent + slot + a value Ref, here the angle).
                    e.kind = EffectKind::MeasureAt;
                    if (!agent(e) || !value(e)) return false;
                    if (!integral(o, "slot", e.slot)) return false;
                }
                else if (k == "set_decoherence") {
                    e.kind = EffectKind::SetDecoherence;
                    if (!agent(e) || !value(e)) return false;
                }
                else if (k == "rearm") {
                    e.kind = EffectKind::Rearm;
                    if (!agent(e)) return false;
                }
                else if (k == "reward") {
                    e.kind = EffectKind::Reward;
                    if (!agent(e)) return false;
                    if (!integral(o, "q", e.slot)) return false;
                    // `branch` names the branch being reinforced -- true is the 1
                    // branch, driving that fact's memory_preference toward -1. That
                    // is the VALUE convention the whole learning API uses (behavior
                    // ABI v38), matching reward_pair and committed().
                    //
                    // NOT spelled `value`: every other effect here uses `value` for
                    // its AMOUNT (see the `value` helper above -- set_goal,
                    // set_scale, set_visible). One key meaning two things in one
                    // schema is the footgun this convention change existed to
                    // remove; `reward` keeps `strength` for its amount.
                    //
                    // The key used to be `toward`, and it meant the OPPOSITE:
                    // toward == true selected the 0 branch. A chart carrying the old
                    // key would still parse and would learn BACKWARDS, silently, so
                    // this refuses it rather than guessing. Porting a chart is
                    // mechanical: rename the key and flip the boolean.
                    if (find_member(o, "toward")) {
                        return fail(
                            "reward: 'toward' was replaced by 'branch' with the "
                            "OPPOSITE meaning (ABI v38) -- true now names the 1 "
                            "branch. Rename the key and flip the boolean.");
                    }
                    const JSONValue* tw = find_member(o, "branch");
                    e.flag = (tw && tw->kind == JSONValueKind::Bool && tw->bool_value)
                        ? 1u : 0u;
                    const JSONValue* st = find_member(o, "strength");
                    if (st && !ref(*st, e.value)) return false;
                }
                else if (k == "set_scale") {
                    e.kind = EffectKind::SetScale;
                    if (!target(e) || !value(e)) return false;
                }
                else if (k == "set_visible") {
                    e.kind = EffectKind::SetVisible;
                    if (!target(e) || !value(e)) return false;
                }
                else if (k == "play_sound") {
                    e.kind = EffectKind::PlaySound;
                    if (!target(e)) return false;
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
                    if (!integral(o, "slot", t.slot)) return false;
                    // -2 == "any", which is also what the writer's `"outcome": "any"`
                    // string lands on (not a Number, so the default stands).
                    if (!integral<int8_t>(o, "outcome", t.outcome, -2)) return false;
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

                // Gate on the schema id BEFORE reading anything else, exactly as the
                // scene reader does. Without this the version the editor stamps was
                // decorative: a `...ir.v1` chart would be parsed as v0, so every key
                // whose meaning the new revision changed would be read under the old
                // meaning and the chart would run -- wrongly, with no diagnostic. That
                // is strictly worse than refusing to run it. Absent counts as
                // unrecognized: everything the editor writes carries the field, so a
                // chart without one has an unknown provenance rather than a known-old
                // one, and guessing is the behaviour being removed.
                const std::string schema = str(root, "schema");
                if (schema.empty())
                    return fail(
                        "chart is missing its 'schema' field (expected '"
                        + std::string(kChartSchema) + "')");
                if (schema != kChartSchema)
                    return fail(
                        "unrecognized chart schema '" + schema + "' -- this build "
                        "reads '" + std::string(kChartSchema) + "'. A chart from a "
                        "newer editor needs a newer engine; it is refused rather "
                        "than read under the old meaning of its keys.");

                c.name = str(root, "name");

                // Declaration NAMES must be present and unique. Both rules exist to
                // stop a name lookup succeeding by accident:
                //
                //  * EMPTY is the dangerous one. Every lookup here goes through
                //    str(), which yields "" for an absent member, and index_of_*
                //    matches by value -- so a single declaration with an empty name
                //    turns into a WILDCARD that any missing field silently binds to.
                //    A transition with no "target" would resolve to a state with no
                //    "id" instead of failing; an effect with no "agent" would drive
                //    whichever agent forgot its id.
                //  * DUPLICATE resolves to the first match, so the later declaration
                //    is unreachable and which one you get depends on authoring order.
                //    For pure ops it is worse than unreachable: refs already resolved
                //    keep pointing at the earlier op while later refs get the newer
                //    one, so one id means two different values in one chart.
                //
                // The editor refuses all of these too (StatechartJson.Inspect), so
                // this is the engine catching up rather than adding a rule -- a chart
                // the editor would not emit should not be one the engine silently
                // runs differently.
                if (const JSONValue* bs = arr(root, "bindings"))
                    for (auto& b : bs->array_values) {
                        Binding bd;
                        bd.port = str(*b, "port");
                        if (bd.port.empty())
                            return fail("binding is missing its 'port' name");
                        if (c.index_of_binding(bd.port) >= 0)
                            return fail(
                                "duplicate binding port '" + bd.port + "'");
                        bd.find = str(*b, "find");
                        bd.subtree = str(*b, "scope") != "global";
                        c.bindings.push_back(std::move(bd));
                    }

                if (const JSONValue* as = arr(root, "agents"))
                    for (auto& a : as->array_values) {
                        AgentDecl ad;
                        ad.id = str(*a, "id");
                        if (ad.id.empty())
                            return fail("agent is missing its 'id'");
                        if (c.index_of_agent(ad.id) >= 0)
                            return fail("duplicate agent id '" + ad.id + "'");
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
                    for (auto& s : ss->array_values) {
                        std::string sid = str(*s, "id");
                        if (sid.empty())
                            return fail("state is missing its 'id'");
                        if (c.index_of_state(sid) >= 0)
                            return fail("duplicate state id '" + sid + "'");
                        c.states.push_back(State{ std::move(sid), {}, {}, {}, {} });
                    }
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
                                if (!trg)
                                    return fail(
                                        "transition is missing its 'trigger'");
                                // trigger() already reported exactly what was wrong;
                                // the old `|| !trigger(...)` collapsed both cases into
                                // one fail() that CLOBBERED that message with a
                                // generic "bad transition trigger" -- throwing away
                                // the reason at the last step before the caller.
                                if (!trigger(*trg, tr.trigger)) return false;
                                if (!effect_list(*t, "actions", tr.actions))
                                    return false;
                                // Now that no state can carry an empty id, an absent
                                // "target" can no longer alias onto one -- but say so
                                // directly rather than reporting it as target '' is
                                // unknown.
                                const std::string target = str(*t, "target");
                                if (target.empty())
                                    return fail(
                                        "transition on state '" + st.id
                                        + "' is missing its 'target'");
                                int tgt = c.index_of_state(target);
                                if (tgt < 0) return fail("transition target '"
                                    + target + "' is unknown");
                                tr.target = static_cast<uint16_t>(tgt);
                                st.transitions.push_back(std::move(tr));
                            }
                    }
                }

                if (const JSONValue* rs = arr(root, "regions"))
                    for (auto& r : rs->array_values) {
                        Region rg;
                        rg.id = str(*r, "id");
                        const std::string initial = str(*r, "initial");
                        if (initial.empty())
                            return fail(
                                "region '" + rg.id + "' is missing its 'initial' "
                                "state");
                        int init = c.index_of_state(initial);
                        if (init < 0)
                            return fail(
                                "region '" + rg.id + "' initial state '" + initial
                                + "' is unknown");
                        rg.initial = static_cast<uint16_t>(init);
                        if (const JSONValue* sl = arr(*r, "states"))
                            for (auto& sid : sl->array_values) {
                                // A non-string entry yields "" here, which no state
                                // can match any more, so it reports as unknown
                                // rather than silently binding to one.
                                int si = c.index_of_state(sid->string_value);
                                if (si < 0)
                                    return fail(
                                        "region '" + rg.id + "' names unknown state '"
                                        + sid->string_value + "'");
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
