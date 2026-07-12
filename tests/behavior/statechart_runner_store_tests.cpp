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
