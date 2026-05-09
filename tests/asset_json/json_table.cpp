#include <gtest/gtest.h>

#include <engine/assets/json/json.h>
#include <engine/assets/type_extensions.h>

#include <external/json/json_parser.h>

namespace
{
    wz::engine::assets::JSONData make_json_data(std::string_view text)
    {
        const auto parsed = wz::json::parse_json_string(std::string(text));
        EXPECT_TRUE(parsed.ok);

        wz::engine::assets::JSONData data;
        data.document = std::move(
            const_cast<wz::json::JSONParseResult&>(parsed).document
        );
        return data;
    }
}

TEST(JSONTable, AddReturnsValidHandle)
{
    wz::engine::assets::JSONTable table;

    auto parsed = wz::json::parse_json_string(R"({ "name": "brick" })");
    ASSERT_TRUE(parsed.ok);

    wz::engine::assets::JSONData data;
    data.document = std::move(parsed.document);

    const wz::asset::ResourceHandle handle =
        table.add(std::move(data));

    EXPECT_TRUE(handle.valid());
    EXPECT_NE(handle.id, 0u);
    EXPECT_EQ(handle.epoch, 1u);
    EXPECT_EQ(handle.type, wz::engine::assets::kAssetTypeJSONDocument);

    const wz::engine::assets::JSONData* stored = table.get(handle);

    ASSERT_NE(stored, nullptr);
    ASSERT_NE(stored->document.root, nullptr);
    EXPECT_EQ(stored->document.root->kind, wz::json::JSONValueKind::Object);
}

TEST(JSONTable, GetRejectsDefaultInvalidHandle)
{
    wz::engine::assets::JSONTable table;

    const wz::asset::ResourceHandle handle{};

    EXPECT_FALSE(handle.valid());
    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(JSONTable, GetRejectsWrongType)
{
    wz::engine::assets::JSONTable table;

    auto parsed = wz::json::parse_json_string(R"({ "name": "brick" })");
    ASSERT_TRUE(parsed.ok);

    wz::engine::assets::JSONData data;
    data.document = std::move(parsed.document);

    wz::asset::ResourceHandle handle =
        table.add(std::move(data));

    ASSERT_TRUE(handle.valid());

    handle.type = wz::engine::assets::kAssetTypeCSVTable;

    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(JSONTable, GetRejectsOutOfRangeId)
{
    wz::engine::assets::JSONTable table;

    wz::asset::ResourceHandle handle{
        .id = 999,
        .epoch = 1,
        .type = wz::engine::assets::kAssetTypeJSONDocument,
    };

    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(JSONTable, GetRejectsWrongEpoch)
{
    wz::engine::assets::JSONTable table;

    auto parsed = wz::json::parse_json_string(R"({ "name": "brick" })");
    ASSERT_TRUE(parsed.ok);

    wz::engine::assets::JSONData data;
    data.document = std::move(parsed.document);

    wz::asset::ResourceHandle handle =
        table.add(std::move(data));

    ASSERT_TRUE(handle.valid());

    handle.epoch = 999;

    EXPECT_EQ(table.get(handle), nullptr);
}

TEST(JSONTable, DestroyInvalidatesOldHandle)
{
    wz::engine::assets::JSONTable table;

    auto parsed = wz::json::parse_json_string(R"({ "name": "brick" })");
    ASSERT_TRUE(parsed.ok);

    wz::engine::assets::JSONData data;
    data.document = std::move(parsed.document);

    const wz::asset::ResourceHandle handle =
        table.add(std::move(data));

    ASSERT_TRUE(handle.valid());
    ASSERT_NE(table.get(handle), nullptr);

    table.destroy();

    EXPECT_EQ(table.get(handle), nullptr);
}