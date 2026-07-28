#include "scene_asset_module_test_support.h"

#include <engine/assets/scene/scene_json_export.h>

#include <external/json/json_parser.h>
#include <external/json/json_writer.h>

#include <string>

namespace
{
    using wz::engine::assets::set_scene_document_behavior_config;

    wz::json::JSONDocument parse(const std::string& text)
    {
        wz::json::JSONParseResult parsed = wz::json::parse_json_string(text);
        EXPECT_TRUE(parsed.ok) << parsed.error.message;
        return std::move(parsed.document);
    }

    // A scenelet with one statechart_runner bound to chart "patrol", plus a
    // second runner on another chart and an unrelated module, so the tests can
    // prove the match is narrow.
    const char* kScenelet = R"({
      "schema": "wozzits.scene.v0",
      "name": "tank",
      "nodes": [
        {
          "id": "root",
          "name": "tank",
          "behaviors": [
            {
              "module": "statechart_runner",
              "config": { "chart": "patrol", "chart_ir": "OLD" }
            },
            {
              "module": "statechart_runner",
              "config": { "chart": "guard", "chart_ir": "GUARD_IR" }
            },
            {
              "module": "spinner",
              "config": { "chart": "patrol", "chart_ir": "NOT_A_RUNNER" }
            }
          ]
        }
      ],
      "defaults": {}
    })";

    // The legacy node shape: a single "behavior" object rather than an array.
    const char* kLegacyScenelet = R"({
      "schema": "wozzits.scene.v0",
      "name": "old",
      "nodes": [
        {
          "id": "root",
          "behavior": {
            "module": "statechart_runner",
            "config": { "chart": "patrol", "chart_ir": "OLD" }
          }
        }
      ]
    })";

    // Read nodes[0].behaviors[i].config[key].
    std::string config_at(
        const wz::json::JSONDocument& doc, size_t behavior_index,
        const std::string& key)
    {
        const auto& nodes = doc.root->object_members;
        for (const auto& m : nodes) {
            if (m.key != "nodes") {
                continue;
            }
            const auto& node = *m.value->array_values.at(0);
            for (const auto& nm : node.object_members) {
                if (nm.key != "behaviors") {
                    continue;
                }
                const auto& behavior =
                    *nm.value->array_values.at(behavior_index);
                for (const auto& bm : behavior.object_members) {
                    if (bm.key != "config") {
                        continue;
                    }
                    for (const auto& cm : bm.value->object_members) {
                        if (cm.key == key) {
                            return cm.value->string_value;
                        }
                    }
                }
            }
        }
        return "<missing>";
    }
}

TEST(SceneDocumentBehaviorConfig, UpdatesOnlyTheMatchingBinding)
{
    wz::json::JSONDocument doc = parse(kScenelet);

    const uint32_t changed = set_scene_document_behavior_config(
        doc, "statechart_runner", "chart", "patrol", "chart_ir", "FRESH");

    EXPECT_EQ(changed, 1u);
    EXPECT_EQ(config_at(doc, 0, "chart_ir"), "FRESH");

    // A different chart on the same module, and the same chart on a different
    // module, are both left alone.
    EXPECT_EQ(config_at(doc, 1, "chart_ir"), "GUARD_IR");
    EXPECT_EQ(config_at(doc, 2, "chart_ir"), "NOT_A_RUNNER");
}

// What keeps a scenelet that is ALSO the open scene from being rewritten: the
// in-scene refresh already pushed the IR, so the file re-write must be a no-op.
TEST(SceneDocumentBehaviorConfig, IsIdempotent)
{
    wz::json::JSONDocument doc = parse(kScenelet);

    EXPECT_EQ(
        set_scene_document_behavior_config(
            doc, "statechart_runner", "chart", "patrol", "chart_ir", "FRESH"),
        1u);

    // Applying the SAME value again reports no change, so the caller skips the
    // write entirely.
    EXPECT_EQ(
        set_scene_document_behavior_config(
            doc, "statechart_runner", "chart", "patrol", "chart_ir", "FRESH"),
        0u);
}

TEST(SceneDocumentBehaviorConfig, HandlesTheLegacySingularBehaviorShape)
{
    wz::json::JSONDocument doc = parse(kLegacyScenelet);

    EXPECT_EQ(
        set_scene_document_behavior_config(
            doc, "statechart_runner", "chart", "patrol", "chart_ir", "FRESH"),
        1u);
}

TEST(SceneDocumentBehaviorConfig, AddsTheConfigKeyWhenAbsent)
{
    // A runner that has never been compiled carries no chart_ir at all.
    wz::json::JSONDocument doc = parse(R"({
      "nodes": [
        {
          "id": "root",
          "behaviors": [
            { "module": "statechart_runner", "config": { "chart": "patrol" } }
          ]
        }
      ]
    })");

    EXPECT_EQ(
        set_scene_document_behavior_config(
            doc, "statechart_runner", "chart", "patrol", "chart_ir", "FRESH"),
        1u);
    EXPECT_EQ(config_at(doc, 0, "chart_ir"), "FRESH");
}

TEST(SceneDocumentBehaviorConfig, ReportsNoChangeWhenNothingMatches)
{
    wz::json::JSONDocument doc = parse(kScenelet);

    // Unknown chart name.
    EXPECT_EQ(
        set_scene_document_behavior_config(
            doc, "statechart_runner", "chart", "nope", "chart_ir", "FRESH"),
        0u);
    // Unknown module.
    EXPECT_EQ(
        set_scene_document_behavior_config(
            doc, "quantum_agent", "chart", "patrol", "chart_ir", "FRESH"),
        0u);
}

// The whole reason this is a surgical upsert rather than a nodes-array
// re-emit: everything the exporter does not model must survive verbatim.
TEST(SceneDocumentBehaviorConfig, PreservesUnrelatedDocumentContent)
{
    wz::json::JSONDocument doc = parse(kScenelet);

    set_scene_document_behavior_config(
        doc, "statechart_runner", "chart", "patrol", "chart_ir", "FRESH");

    const std::string text = wz::json::serialize_json(doc);
    EXPECT_NE(text.find("wozzits.scene.v0"), std::string::npos);
    EXPECT_NE(text.find("\"defaults\""), std::string::npos);
    EXPECT_NE(text.find("\"spinner\""), std::string::npos);
    EXPECT_NE(text.find("GUARD_IR"), std::string::npos);
}

TEST(SceneDocumentBehaviorConfig, IgnoresDocumentsWithoutNodes)
{
    wz::json::JSONDocument empty = parse("{}");
    EXPECT_EQ(
        set_scene_document_behavior_config(
            empty, "statechart_runner", "chart", "patrol", "chart_ir", "X"),
        0u);

    wz::json::JSONDocument scalar = parse("42");
    EXPECT_EQ(
        set_scene_document_behavior_config(
            scalar, "statechart_runner", "chart", "patrol", "chart_ir", "X"),
        0u);
}
