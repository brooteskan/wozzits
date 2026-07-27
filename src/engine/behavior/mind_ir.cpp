#include <engine/behavior/mind_ir.h>

#include <engine/behavior/quantum_agent_behaviors.h>
#include <external/json/json_document.h>
#include <external/json/json_parser.h>
#include <external/json/json_read_helpers.h>

#include <utility>

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

        const long qubits = index_of(root, "qubits");
        if (qubits < 1) {
            return fail("mind needs a positive `qubits` count");
        }
        const auto n = static_cast<uint32_t>(qubits);

        cognition::AgentSpec spec;
        spec.agent_count = n;

        // Per-qubit longitudinal goal bias (a node field).
        if (const JSONValue* goals = array_of(root, "goals")) {
            for (const auto& g : goals->array_values) {
                const long q = index_of(*g, "q");
                if (q < 0 || static_cast<uint32_t>(q) >= n) {
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
                const long a = index_of(*b, "a");
                const long bb = index_of(*b, "b");
                if (a < 0 || bb < 0
                    || static_cast<uint32_t>(a) >= n
                    || static_cast<uint32_t>(bb) >= n) {
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
