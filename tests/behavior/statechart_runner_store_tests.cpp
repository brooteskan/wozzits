#include "behavior_test_support.h"

#include <engine/behavior/statechart_runner.h>

#include <string>

// A statechart_runner embeds a COMPILED chart (chart_ir) under its stable `chart` name. When the
// editor re-embeds an EDITED chart under that same name and the viewport restarts, the store must
// re-parse -- keying the cache on name alone made a restart keep running the STALE chart, because
// StatechartRunnerStore is a process-global singleton that outlives an AppContext restart. This
// pins the fix: load re-parses when the IR text differs.
namespace
{
    constexpr const char* kOneBinding =
        R"({"schema":"wozzits.statechart.ir.v0","name":"x",)"
        R"("bindings":[{"port":"a","find":"a","scope":"global"}],)"
        R"("agents":[],"pure":[],"regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","do":[],"transitions":[]}]})";

    constexpr const char* kTwoBindings =
        R"({"schema":"wozzits.statechart.ir.v0","name":"x",)"
        R"("bindings":[{"port":"a","find":"a","scope":"global"},{"port":"b","find":"b","scope":"global"}],)"
        R"("agents":[],"pure":[],"regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","do":[],"transitions":[]}]})";
}

TEST(StatechartRunnerStore, ReparsesWhenIrChangesUnderSameName)
{
    wz::engine::behavior::StatechartRunnerStore store;

    const auto* one = store.load("lesson0", kOneBinding);
    ASSERT_NE(one, nullptr);
    EXPECT_EQ(one->bindings.size(), 1u);

    // Same IR under the same name -> the cached parse is reused.
    EXPECT_EQ(store.load("lesson0", kOneBinding), one);

    // EDITED IR under the SAME name -> re-parsed, reflecting the new bindings. Before the fix this
    // returned the stale one-binding chart, so a restart never ran an edited chart.
    const auto* two = store.load("lesson0", kTwoBindings);
    ASSERT_NE(two, nullptr);
    EXPECT_EQ(two->bindings.size(), 2u);
}

// A failed load must hand back parse_chart's REASON. The store deliberately does not log (the
// caller reports), so a dropped error string meant the dispatch site could only say "bad IR" --
// and a runner whose chart fails is inert for the whole session while the process still exits 0.
// The reason is the entire difference between a fixable error and "my behaviour does nothing".
TEST(StatechartRunnerStore, FailedLoadReportsTheParseReason)
{
    wz::engine::behavior::StatechartRunnerStore store;

    // Structurally parseable JSON, but a transition pointing at a state that is not there --
    // exactly what deleting a state in the editor used to be able to persist.
    constexpr const char* kDanglingTarget =
        R"({"schema":"wozzits.statechart.ir.v0","name":"x",)"
        R"("bindings":[],"agents":[],"pure":[],)"
        R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","transitions":[)"
        R"({"trigger":{"kind":"after","seconds":1},"target":"GONE"}]}]})";

    std::string error = "untouched";
    EXPECT_EQ(store.load("broken", kDanglingTarget, &error), nullptr);
    EXPECT_NE(error.find("GONE"), std::string::npos)
        << "the parse reason was dropped; got: " << error;

    // A chart with no regions is the other reachable shape (deleting every state).
    constexpr const char* kNoRegions =
        R"({"schema":"wozzits.statechart.ir.v0","name":"x",)"
        R"("bindings":[],"agents":[],"pure":[],"regions":[],"states":[]})";

    std::string regions_error;
    EXPECT_EQ(store.load("empty", kNoRegions, &regions_error), nullptr);
    EXPECT_NE(regions_error.find("regions"), std::string::npos)
        << "the parse reason was dropped; got: " << regions_error;

    // Malformed JSON (an unconfigured chart_ir is the empty string) still reports a reason
    // rather than falling through to a bare verdict.
    std::string empty_error;
    EXPECT_EQ(store.load("blank", "", &empty_error), nullptr);
    EXPECT_FALSE(empty_error.empty());

    // The out-param stays optional: the cache-behaviour callers above pass nothing.
    EXPECT_EQ(store.load("broken", kDanglingTarget), nullptr);
}

// The schema id the editor stamps has to MEAN something. parse_chart used to read
// `name` and never look at `schema`, so a chart from a future editor would be parsed
// as v0 -- every key whose meaning the new revision changed read under the old
// meaning, running wrongly with no diagnostic. (The `reward` toward -> branch rename
// is the precedent: same silent-wrong-answer risk, defended by a hand-written special
// case because the version gate was not doing its job.)
TEST(StatechartRunnerStore, RefusesAChartWhoseSchemaIsNotThisBuilds)
{
    wz::engine::behavior::StatechartRunnerStore store;

    const auto with_schema = [](const char* schema) {
        return std::string(R"({"schema":")") + schema
            + R"(","name":"x","bindings":[],"agents":[],"pure":[],)"
              R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
              R"("states":[{"id":"s","transitions":[]}]})";
    };

    // The current id loads -- the gate must not reject what the editor writes today.
    std::string ok_error;
    EXPECT_NE(
        store.load(
            "current",
            with_schema("wozzits.statechart.ir.v0"),
            &ok_error),
        nullptr) << ok_error;

    // A NEWER schema is refused by name, so the reason points at the version rather
    // than at whatever downstream key first looked wrong.
    std::string newer_error;
    EXPECT_EQ(
        store.load("newer", with_schema("wozzits.statechart.ir.v1"), &newer_error),
        nullptr);
    EXPECT_NE(newer_error.find("wozzits.statechart.ir.v1"), std::string::npos)
        << newer_error;

    // A chart with NO schema is unrecognized too: everything the editor emits carries
    // the field, so one without it has unknown provenance, and guessing v0 is exactly
    // the behaviour being removed.
    constexpr const char* kNoSchema =
        R"({"name":"x","bindings":[],"agents":[],"pure":[],)"
        R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
        R"("states":[{"id":"s","transitions":[]}]})";
    std::string missing_error;
    EXPECT_EQ(store.load("bare", kNoSchema, &missing_error), nullptr);
    EXPECT_NE(missing_error.find("schema"), std::string::npos) << missing_error;
}

// An integral chart field that cannot hold the authored number is REPORTED, not
// wrapped. `static_cast<uint16_t>(-1.0)` is undefined behaviour, and on the usual
// target it quietly yields 65535 -- so a typo'd slot used to read a completely
// unrelated decision and the chart merely "behaved oddly". A chart is authored data;
// this is a fixable mistake and it should say so.
TEST(StatechartRunnerStore, RefusesAnOutOfRangeIntegralField)
{
    wz::engine::behavior::StatechartRunnerStore store;

    const auto chart_with = [](const char* slot) {
        return std::string(
                   R"({"schema":"wozzits.statechart.ir.v0","name":"x",)"
                   R"("bindings":[{"port":"a","find":"a","scope":"global"}],)"
                   R"("agents":[{"id":"m","owned":true,"host":"self"}],)"
                   R"("pure":[{"id":"z","op":"marginal","agent":"m","slot":)")
            + slot
            + R"(}],"regions":[{"id":"r","initial":"s","states":["s"]}],)"
              R"("states":[{"id":"s","transitions":[]}]})";
    };

    // In range: unchanged behaviour.
    std::string ok_error;
    EXPECT_NE(store.load("fine", chart_with("3"), &ok_error), nullptr) << ok_error;

    // Negative and enormous both refuse, naming the field.
    std::string negative_error;
    EXPECT_EQ(store.load("neg", chart_with("-1"), &negative_error), nullptr);
    EXPECT_NE(negative_error.find("slot"), std::string::npos) << negative_error;

    std::string huge_error;
    EXPECT_EQ(store.load("huge", chart_with("1e30"), &huge_error), nullptr);
    EXPECT_NE(huge_error.find("slot"), std::string::npos) << huge_error;

    // A commit trigger's `outcome` is an int8_t and takes the same route. Its "any"
    // sentinel is a NON-number ("any" or absent), which must still be accepted.
    const auto trigger_with_outcome = [](const char* outcome) {
        return std::string(
                   R"({"schema":"wozzits.statechart.ir.v0","name":"x",)"
                   R"("bindings":[],)"
                   R"("agents":[{"id":"m","owned":true,"host":"self"}],)"
                   R"("pure":[],)"
                   R"("regions":[{"id":"r","initial":"s","states":["s"]}],)"
                   R"("states":[{"id":"s","transitions":[{"trigger":)"
                   R"({"kind":"commit","agent":"m","slot":0,"outcome":)")
            + outcome + R"(},"target":"s"}]}]})";
    };

    std::string any_error;
    EXPECT_NE(
        store.load("any", trigger_with_outcome(R"("any")"), &any_error), nullptr)
        << any_error;

    std::string outcome_error;
    EXPECT_EQ(store.load("oob", trigger_with_outcome("1e30"), &outcome_error), nullptr);
    EXPECT_NE(outcome_error.find("outcome"), std::string::npos) << outcome_error;
}
