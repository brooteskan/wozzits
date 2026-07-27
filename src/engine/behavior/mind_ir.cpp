#include <engine/behavior/mind_ir.h>

#include <engine/behavior/quantum_agent_behaviors.h>
#include <external/json/json_document.h>
#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <utility>
#include <vector>

namespace wz::engine::behavior
{
    using wz::json::find_member;
    using wz::json::JSONValue;
    using wz::json::JSONValueKind;

    namespace
    {
        const JSONValue* array_of(const JSONValue& o, const char* k)
        {
            const JSONValue* v = find_member(o, k);
            return (v && v->kind == JSONValueKind::Array) ? v : nullptr;
        }
        const JSONValue* object_of(const JSONValue& o, const char* k)
        {
            const JSONValue* v = find_member(o, k);
            return (v && v->kind == JSONValueKind::Object) ? v : nullptr;
        }
        double num(const JSONValue& o, const char* k, double d)
        {
            const JSONValue* v = find_member(o, k);
            return (v && v->kind == JSONValueKind::Number) ? v->number_value : d;
        }
        // A non-negative index from a Number field, or -1 when absent / not a number.
        long index_of(const JSONValue& o, const char* k)
        {
            const JSONValue* v = find_member(o, k);
            return (v && v->kind == JSONValueKind::Number)
                ? static_cast<long>(v->number_value)
                : -1;
        }
    }

    bool parse_mind(
        const std::string& json_text,
        cognition::AgentSpec& out,
        std::string& error)
    {
        auto fail = [&](std::string m) { error = std::move(m); return false; };

        wz::json::JSONParseResult res = wz::json::parse_json_string(json_text);
        if (!res.ok || !res.document.root) {
            return fail("JSON parse failed: " + res.error.message);
        }
        const JSONValue& root = *res.document.root;
        if (root.kind != JSONValueKind::Object) {
            return fail("mind root must be an object");
        }

        cognition::AgentSpec spec;

        // Optional multi-disposition layout: agent i owns dispositions[i] qubits,
        // so an agent can hold a real >2-way choice rather than one bit. Absent
        // (the v0 shape) means one qubit per agent and `qubits` is the count.
        cognition::AgentLayout layout;
        uint32_t n = 0;
        const JSONValue* dispositions = array_of(root, "dispositions");
        if (dispositions) {
            std::vector<uint32_t> per_agent;
            for (const auto& d : dispositions->array_values) {
                if (d->kind != JSONValueKind::Number || d->number_value < 1.0) {
                    return fail("mind `dispositions` entries must be >= 1");
                }
                per_agent.push_back(static_cast<uint32_t>(d->number_value));
            }
            if (per_agent.empty()) {
                return fail("mind `dispositions` must not be empty");
            }
            layout = cognition::make_agent_layout(per_agent);
            n = layout.total_qubits;
            // An explicit `qubits` alongside a layout must agree -- the two would
            // otherwise silently address different qubits.
            const long declared = index_of(root, "qubits");
            if (declared >= 0 && static_cast<uint32_t>(declared) != n) {
                return fail(
                    "mind `qubits` disagrees with the sum of `dispositions`");
            }
            spec.dispositions_per_agent = std::move(per_agent);
        } else {
            const long qubits = index_of(root, "qubits");
            if (qubits < 1) {
                return fail("mind needs a positive `qubits` count");
            }
            n = static_cast<uint32_t>(qubits);
        }
        spec.agent_count = n;

        // Resolve one endpoint of a goal or bond. Two addressing forms: the flat
        // qubit `"q"` (or `"a"`/`"b"`), and -- only with a layout -- the pair
        // {"<prefix>agent", "<prefix>disposition"}. Returns -1 on anything out of
        // range so the caller can fail with its own message.
        const auto resolve = [&](const JSONValue& o, const char* flat_key,
                                 const char* agent_key,
                                 const char* disposition_key) -> long {
            const long agent = index_of(o, agent_key);
            if (agent >= 0) {
                if (!dispositions) {
                    return -1;   // (agent, disposition) needs a declared layout
                }
                const long d = index_of(o, disposition_key);
                const uint32_t q = cognition::qubit_of(
                    layout, static_cast<uint32_t>(agent),
                    static_cast<uint32_t>(d < 0 ? 0 : d));
                return q >= n ? -1 : static_cast<long>(q);
            }
            const long q = index_of(o, flat_key);
            return (q < 0 || static_cast<uint32_t>(q) >= n) ? -1 : q;
        };

        // Per-qubit longitudinal goal bias (a node field).
        if (const JSONValue* goals = array_of(root, "goals")) {
            for (const auto& g : goals->array_values) {
                const long q = resolve(*g, "q", "agent", "disposition");
                if (q < 0) {
                    return fail("mind goal names an out-of-range qubit");
                }
                spec.goals.push_back(cognition::Goal{
                    .agent = static_cast<uint32_t>(q),
                    .field = num(*g, "field", 0.0),
                });
            }
        }

        // Pairwise couplings (the graph edges) between two distinct qubits.
        if (const JSONValue* bonds = array_of(root, "bonds")) {
            for (const auto& b : bonds->array_values) {
                const long a =
                    resolve(*b, "a", "a_agent", "a_disposition");
                const long bb =
                    resolve(*b, "b", "b_agent", "b_disposition");
                if (a < 0 || bb < 0) {
                    return fail("mind bond names an out-of-range qubit");
                }
                if (a == bb) {
                    return fail("mind bond couples a qubit to itself");
                }
                spec.bonds.push_back(cognition::ExactBond{
                    .a = static_cast<uint32_t>(a),
                    .b = static_cast<uint32_t>(bb),
                    .j = num(*b, "j", 0.0),
                });
            }
        }

        // Optional per-agent exclusivity: makes that agent's dispositions a
        // mutually-exclusive choice (exactly one active). Shorter than
        // `dispositions` is fine -- the rest simply have none.
        if (const JSONValue* one_hot = array_of(root, "one_hot")) {
            if (!dispositions) {
                return fail("mind `one_hot` needs a `dispositions` layout");
            }
            if (one_hot->array_values.size()
                > spec.dispositions_per_agent.size()) {
                return fail("mind `one_hot` has more entries than agents");
            }
            for (const auto& s : one_hot->array_values) {
                spec.one_hot_strength.push_back(
                    s->kind == JSONValueKind::Number ? s->number_value : 0.0);
            }
        }

        // Backend selector + optional learning register.
        const long chi = index_of(root, "chi");
        spec.chi = chi < 0 ? 0u : static_cast<uint32_t>(chi);
        const long memory = index_of(root, "memory");
        spec.memory_qubits = memory < 0 ? 0u : static_cast<uint32_t>(memory);

        // Anneal clock -- sensible defaults where a field is absent. gamma_end takes
        // the shared quantum_agent authoring default so the two front ends (scalar
        // config / mind IR) cannot drift; see the note on kQuantumAgentDefaultGammaEnd
        // for why the residual field is non-zero and how it interacts with confidence.
        spec.clock.gamma_start = 2.0;
        spec.clock.gamma_end = kQuantumAgentDefaultGammaEnd;
        spec.clock.anneal_seconds = 4.0;
        spec.clock.relax_rate = 1.0;
        if (const JSONValue* clock = object_of(root, "clock")) {
            spec.clock.gamma_start = num(*clock, "gamma_start", spec.clock.gamma_start);
            spec.clock.gamma_end = num(*clock, "gamma_end", spec.clock.gamma_end);
            spec.clock.anneal_seconds =
                num(*clock, "anneal_seconds", spec.clock.anneal_seconds);
            spec.clock.relax_rate = num(*clock, "relax_rate", spec.clock.relax_rate);
        }

        // Commit policy.
        spec.commit.confidence = 0.8;
        spec.commit.decoherence_rate = 0.0;
        if (const JSONValue* commit = object_of(root, "commit")) {
            spec.commit.confidence = num(*commit, "confidence", spec.commit.confidence);
            spec.commit.decoherence_rate =
                num(*commit, "decoherence", spec.commit.decoherence_rate);
        }

        out = std::move(spec);
        return true;
    }
}
