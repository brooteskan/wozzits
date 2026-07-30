#include <gtest/gtest.h>

// A3-T2 / A3-T4 (issue #77 / umbrella #320): the malformed-document corpus this
// issue originally asked for, driven through the REAL parse entry points.
//
// The framing that makes these cases non-obvious: yyjson refuses to parse
// `1e309`, `NaN` and `Infinity`, so the DOM the parsers see is guaranteed free
// of non-finite doubles. Every case below is therefore a document made of
// perfectly legal JSON numbers -- the damage is done by what the parsers do on
// the way from double to the field's actual type. `1e308` is finite until it is
// a float; `1.7` is a fine number until it is a node id.
//
// Several cases assert a NEGATIVE that is easy to lose in a refactor (a round
// trip is a fixed point, node order matters, unknown root members are ignored).
// Those were green when written; pinning them is the point.

#include <engine/assets/scene/asset_graph_json.h>
#include <engine/assets/scene/scene_compilers.h>
#include <engine/assets/scene/scene_instance.h>
#include <engine/assets/scene/scene_json_export.h>

#include <external/json/json_parser.h>
#include <external/json/json_writer.h>

#include <asset/draft.h>
#include <logging/logger.h>

#include <optional>
#include <string>

namespace
{
    using namespace wz::engine::assets;

    wz::Logger& quiet_logger()
    {
        static wz::Logger logger;   // no sink installed: parse errors go nowhere
        return logger;
    }

    std::optional<SceneAssetData> parse_scene(const std::string& text)
    {
        const auto parsed = wz::json::parse_json_string(text);
        if (!parsed.ok || !parsed.document.root) {
            return std::nullopt;
        }
        return internal::parse_scene_data_from_json(
            parsed.document, quiet_logger());
    }

    std::string scene_with_nodes(const std::string& nodes)
    {
        return R"({"schema":"wozzits.scene.v0","name":"hostile","nodes":)"
            + nodes + "}";
    }

    bool load_graph(const std::string& text, wz::asset::AssetGraphDraft& draft,
        std::string& error)
    {
        const auto parsed = wz::json::parse_json_string(text);
        if (!parsed.ok || !parsed.document.root) {
            error = "json: " + parsed.error.message;
            return false;
        }
        return load_asset_graph_draft_from_v2_json(
            *parsed.document.root, draft, error);
    }

    // ── the JSON layer's own guarantees ──────────────────────────────────

    TEST(SceneJsonHostileDocument, NonFiniteNumbersCannotEnterTheDom)
    {
        for (const char* text : {
                 R"({"n": 1e309})", R"({"n": -1e309})", R"({"n": 1e400})",
                 R"({"n": NaN})", R"({"n": Infinity})" })
        {
            EXPECT_FALSE(wz::json::parse_json_string(text).ok)
                << "a non-finite number reached the DOM: " << text;
        }
        // ...but the largest finite double IS legal input, which is what makes
        // the float narrowing below the real ingress.
        EXPECT_TRUE(wz::json::parse_json_string(R"({"n": 1e308})").ok);
    }

    TEST(SceneJsonHostileDocument, DuplicateKeysAndDeepNestingAreRejected)
    {
        EXPECT_FALSE(
            wz::json::parse_json_string(R"({"a":1,"a":2})").ok);

        std::string deep;
        for (int i = 0; i < 400; ++i) deep += "[";
        for (int i = 0; i < 400; ++i) deep += "]";
        EXPECT_FALSE(wz::json::parse_json_string(deep).ok);
    }

    // ── scene: numeric hostility ─────────────────────────────────────────

    TEST(SceneJsonHostileDocument, TransformThatCannotNarrowToFloatIsRejected)
    {
        // Was accepted, yielding translation = [inf, 0, 0] all the way into the
        // world matrices.
        EXPECT_FALSE(parse_scene(scene_with_nodes(
            R"([{"id":"a","transform":{"translation":[1e308,0,0]}}])")));
        EXPECT_FALSE(parse_scene(scene_with_nodes(
            R"([{"id":"a","transform":{"rotation_quat":[1e308,0,0,1]}}])")));
        EXPECT_FALSE(parse_scene(scene_with_nodes(
            R"([{"id":"a","transform":{"scale":[1,1,-1e308]}}])")));

        const auto ok = parse_scene(scene_with_nodes(
            R"([{"id":"a","transform":{"translation":[1e37,0,0]}}])"));
        ASSERT_TRUE(ok) << "a value that fits a float must still load";
        EXPECT_FLOAT_EQ(ok->nodes[0].local.translation[0], 1e37f);
    }

    TEST(SceneJsonHostileDocument, MalformedTransformFailsInsteadOfDefaulting)
    {
        // Silently substituting identity loses the author's placement with no
        // diagnostic -- the node simply appears at the origin.
        EXPECT_FALSE(parse_scene(scene_with_nodes(
            R"([{"id":"a","transform":{"translation":[1,2]}}])")));
        EXPECT_FALSE(parse_scene(scene_with_nodes(
            R"([{"id":"a","transform":{"translation":["1","2","3"]}}])")));
        EXPECT_FALSE(parse_scene(scene_with_nodes(
            R"([{"id":"a","transform":{"rotation_quat":[0,0,0]}}])")));

        // An ABSENT member keeps its default: a partial transform block is
        // legitimate authoring, not a malformed document.
        const auto partial = parse_scene(scene_with_nodes(
            R"([{"id":"a","transform":{"translation":[1,2,3]}}])"));
        ASSERT_TRUE(partial);
        EXPECT_FLOAT_EQ(partial->nodes[0].local.scale[0], 1.0f);
        EXPECT_FLOAT_EQ(partial->nodes[0].local.rotation_quat[3], 1.0f);
    }

    TEST(SceneJsonHostileDocument, GraphNodeIdMustBeAnExactPositiveInteger)
    {
        // 1.7 truncating to 1 is the sharp case: not a crash, not obviously
        // corrupt -- a silent rebinding to an asset the author never named.
        for (const char* id : { "1e300", "1.7", "-1", "0" }) {
            const std::string text = scene_with_nodes(
                std::string(R"([{"id":"a","renderable":)")
                + R"({"asset_graph_node_id":)" + id + "}}]");
            EXPECT_FALSE(parse_scene(text))
                << "unusable renderable node id accepted: " << id;
        }

        const auto ok = parse_scene(scene_with_nodes(
            R"([{"id":"a","renderable":{"asset_graph_node_id":7}}])"));
        ASSERT_TRUE(ok);
        ASSERT_TRUE(ok->nodes[0].renderable_asset_node_id.has_value());
        EXPECT_EQ(*ok->nodes[0].renderable_asset_node_id, 7u);
    }

    TEST(SceneJsonHostileDocument, RenderOrderOutOfRangeKeepsTheDefaultLayer)
    {
        // Both 1e300 and -1e300 used to land on INT_MIN -- the frontmost layer,
        // indistinguishably. Out of range now reads as "not authored".
        for (const char* order : { "1e300", "-1e300" }) {
            const auto scene = parse_scene(scene_with_nodes(
                std::string(R"([{"id":"a","render_order":)") + order + "}]"));
            ASSERT_TRUE(scene) << order;
            EXPECT_EQ(scene->nodes[0].render_order, 0) << order;
        }

        const auto ok =
            parse_scene(scene_with_nodes(R"([{"id":"a","render_order":-3}])"));
        ASSERT_TRUE(ok);
        EXPECT_EQ(ok->nodes[0].render_order, -3);

        // A fractional value truncates toward zero rather than being refused --
        // read_integral's documented contract, and what the old raw cast did for
        // in-range input. Pinned so the guard is not mistaken for exactness.
        const auto fractional =
            parse_scene(scene_with_nodes(R"([{"id":"a","render_order":2.5}])"));
        ASSERT_TRUE(fractional);
        EXPECT_EQ(fractional->nodes[0].render_order, 2);
    }

    // ── scene: structural hostility ──────────────────────────────────────

    TEST(SceneJsonHostileDocument, TopologyHostilityIsCaughtAtInstantiate)
    {
        struct Case
        {
            const char* nodes;
            SceneInstantiateError expected;
        };
        const Case cases[] = {
            { R"([{"id":"a"},{"id":"a"}])",
              SceneInstantiateError::DuplicateNodeId },
            { R"([{"id":"a","parent":"a"}])",
              SceneInstantiateError::ParentCycle },
            { R"([{"id":"a","parent":"ghost"}])",
              SceneInstantiateError::ParentNotFound },
            { R"([{"id":"a","parent":"b"},{"id":"b","parent":"a"}])",
              SceneInstantiateError::PolytreeBuildFailed },
        };
        for (const Case& c : cases) {
            const auto scene = parse_scene(scene_with_nodes(c.nodes));
            ASSERT_TRUE(scene) << c.nodes;
            SceneInstantiateContext ctx{};
            EXPECT_EQ(instantiate_scene(*scene, ctx).error, c.expected)
                << c.nodes;
        }

        // A parent named before its child is fine: parents are resolved in a
        // second pass, so forward references are legal.
        const auto forward = parse_scene(scene_with_nodes(
            R"([{"id":"child","parent":"parent"},{"id":"parent"}])"));
        ASSERT_TRUE(forward);
        SceneInstantiateContext ctx{};
        EXPECT_EQ(instantiate_scene(*forward, ctx).error,
            SceneInstantiateError::None);
    }

    TEST(SceneJsonHostileDocument, StructurallyUnusableDocumentsAreRejected)
    {
        EXPECT_FALSE(parse_scene(scene_with_nodes(R"([{"name":"no id"}])")));
        EXPECT_FALSE(parse_scene(scene_with_nodes(R"([42])")));
        EXPECT_FALSE(parse_scene(
            R"({"schema":"wozzits.scene.v0","nodes":{}})"));
        EXPECT_FALSE(parse_scene(
            R"({"schema":"wozzits.scene.v0"})"));
    }

    // ── scene: version / dialect skew ────────────────────────────────────

    TEST(SceneJsonHostileDocument, SchemaSkewFailsClosedButUnknownMembersDont)
    {
        EXPECT_FALSE(parse_scene(R"({"nodes":[{"id":"a"}]})"))
            << "a document with no schema must not be read as v0";
        EXPECT_FALSE(parse_scene(
            R"({"schema":"wozzits.scene.v1","nodes":[{"id":"a"}]})"))
            << "a future schema must not be read under v0 key meanings";

        // Forward compatibility in the other direction: members this parser does
        // not know are ignored, so a newer writer's document still loads.
        EXPECT_TRUE(parse_scene(
            R"({"schema":"wozzits.scene.v0","nodes":[{"id":"a",)"
            R"("not_a_real_component":{"x":1}}],"future_root":7})"));
    }

    // ── scene: round-trip properties (green when written; pinned) ────────

    TEST(SceneJsonHostileDocument, ExportThenParseIsAFixedPoint)
    {
        const auto scene = parse_scene(scene_with_nodes(
            R"([{"id":"a","name":"A","visible":true,"active":false,)"
            R"("render_order":3,"transform":{"translation":[1,2,3],)"
            R"("rotation_quat":[0,0,0,1],"scale":[2,2,2]},)"
            R"("render_to_texture":{"target_asset_node_id":5,)"
            R"("include_descendants":true}},{"id":"b","parent":"a"}])"));
        ASSERT_TRUE(scene);

        const std::string once =
            wz::json::serialize_json(export_scene_to_json_document(*scene));
        const auto reparsed = wz::json::parse_json_string(once);
        ASSERT_TRUE(reparsed.ok) << "export produced unparseable JSON";
        const auto scene2 = internal::parse_scene_data_from_json(
            reparsed.document, quiet_logger());
        ASSERT_TRUE(scene2) << "export produced a document its own parser rejects";

        EXPECT_EQ(once,
            wz::json::serialize_json(export_scene_to_json_document(*scene2)));
    }

    // ── asset graph v2 ───────────────────────────────────────────────────

    TEST(SceneJsonHostileDocument, GraphSchemaGateRefusesAWrongDialect)
    {
        wz::asset::AssetGraphDraft draft;
        std::string error;

        EXPECT_FALSE(load_graph(
            R"({"schema":"wozzits.scene_editor.assets.graph.v1",)"
            R"("nodes":[{"node_id":1,"type":1,"schema":"0x1"}]})",
            draft, error));
        EXPECT_NE(error.find("schema"), std::string::npos)
            << "the diagnostic must name the schema, not a downstream symptom: "
            << error;

        // Asymmetric on purpose: a MISSING schema still loads, so a document
        // built inline (tests, tools) does not need the ceremony.
        wz::asset::AssetGraphDraft no_schema;
        EXPECT_TRUE(load_graph(
            R"({"nodes":[{"node_id":1,"type":1,"schema":"0x1"}]})",
            no_schema, error)) << error;
    }

    TEST(SceneJsonHostileDocument, GraphNodeIdsMustBeExactPositiveIntegers)
    {
        for (const char* id : { "1e300", "1.5", "-1", "0" }) {
            wz::asset::AssetGraphDraft draft;
            std::string error;
            const std::string text =
                std::string(R"({"nodes":[{"node_id":)") + id
                + R"(,"type":1,"schema":"0x1"}]})";
            EXPECT_FALSE(load_graph(text, draft, error)) << id;
            EXPECT_EQ(error, "invalid node id") << id;
        }
    }

    TEST(SceneJsonHostileDocument, GraphStructuralHostilityIsRejectedOrDeferred)
    {
        wz::asset::AssetGraphDraft draft;
        std::string error;

        EXPECT_FALSE(load_graph(
            R"({"nodes":[{"node_id":7,"type":1,"schema":"0x1"},)"
            R"({"node_id":7,"type":2,"schema":"0x2"}]})", draft, error));
        EXPECT_FALSE(load_graph(
            R"({"nodes":[{"node_id":1,"type":1,"schema":"0x1",)"
            R"("deps":[{"from_node_id":99}]}]})", draft, error));

        // A self-edge loads: this loader is deliberately topology-agnostic and
        // the cycle contract lives in validate_asset_graph_draft. Pinned so the
        // division of labour stays explicit.
        wz::asset::AssetGraphDraft self_edge;
        EXPECT_TRUE(load_graph(
            R"({"nodes":[{"node_id":1,"type":1,"schema":"0x1",)"
            R"("deps":[{"from_node_id":1}]}]})", self_edge, error)) << error;
        EXPECT_EQ(self_edge.edges.size(), 1u);
    }
}
