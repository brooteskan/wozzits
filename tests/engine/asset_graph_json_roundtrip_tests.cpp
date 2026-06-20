#include <gtest/gtest.h>

#include <engine/assets/scene/asset_graph_json.h>

#include <asset/compiler.h>
#include <asset/draft.h>

#include <external/json/json_parser.h>

#include <any>
#include <string>

namespace
{
    wz::asset::AssetGraphDraft load_draft(const std::string& json, bool& ok)
    {
        const wz::json::JSONParseResult parsed =
            wz::json::parse_json_string(json);
        wz::asset::AssetGraphDraft draft;
        std::string error;
        ok = parsed.ok && parsed.document.root
            && wz::engine::assets::load_asset_graph_draft_from_v2_json(
                *parsed.document.root, draft, error);
        return draft;
    }

    const wz::asset::ParamBlock* find_params(const wz::asset::AssetGraphDraft& d)
    {
        for (const auto& node : d.nodes) {
            if (const auto* params =
                    std::any_cast<wz::asset::ParamBlock>(&node.node.meta))
            {
                return params;
            }
        }
        return nullptr;
    }

    // Read v2 JSON -> draft -> write it back -> read again; the topology and the
    // node data (schema/type, typed params, the edge) must survive the writer.
    TEST(AssetGraphJsonRoundtrip, NodesEdgesAndParamsSurviveWriteThenRead)
    {
        const std::string source = R"({
          "schema": "wozzits.scene_editor.assets.graph.v2",
          "nodes": [
            {
              "node_id": 1,
              "type": 5,
              "schema": "0x00000000000000aa",
              "params": [
                { "name": "tau",   "type": "float",  "value": 0.5 },
                { "name": "label", "type": "string", "value": "base" },
                { "name": "count", "type": "int",    "value": 3 },
                { "name": "flag",  "type": "bool",   "value": true }
              ]
            },
            {
              "node_id": 2,
              "type": 6,
              "schema": "0x00000000000000bb",
              "deps": [ { "from_node_id": 1, "to_input_port": 0 } ]
            }
          ]
        })";

        bool ok_first = false;
        const wz::asset::AssetGraphDraft first = load_draft(source, ok_first);
        ASSERT_TRUE(ok_first);
        ASSERT_EQ(first.nodes.size(), 2u);
        ASSERT_EQ(first.edges.size(), 1u);

        const std::string written =
            wz::engine::assets::save_asset_graph_draft_to_v2_json(first);

        bool ok_second = false;
        const wz::asset::AssetGraphDraft second = load_draft(written, ok_second);
        ASSERT_TRUE(ok_second);

        EXPECT_EQ(second.nodes.size(), first.nodes.size());
        ASSERT_EQ(second.edges.size(), 1u);
        EXPECT_EQ(second.edges[0].to_input_port, 0u);

        const wz::asset::ParamBlock* params = find_params(second);
        ASSERT_NE(params, nullptr);
        EXPECT_NEAR(params->get<double>("tau", 0.0), 0.5, 1e-6);
        EXPECT_EQ(params->get<std::string>("label", std::string{}), "base");
        EXPECT_EQ(params->get<int64_t>("count", 0), 3);
        EXPECT_TRUE(params->get<bool>("flag", false));
    }
}
