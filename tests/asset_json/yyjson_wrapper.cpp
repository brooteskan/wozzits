#include <gtest/gtest.h>

#include <external/json/json_parser.h>

#include <string>

TEST(JSONParser, ParsesNull)
{
    const auto result = wz::json::parse_json_string("null");

    ASSERT_TRUE(result.ok);
    ASSERT_NE(result.document.root, nullptr);
    EXPECT_EQ(result.document.root->kind, wz::json::JSONValueKind::Null);
}

TEST(JSONParser, ParsesObjectWithStringAndNumber)
{
    const auto result = wz::json::parse_json_string(
        R"({
            "name": "brick",
            "roughness": 0.8
        })"
    );

    ASSERT_TRUE(result.ok);
    ASSERT_NE(result.document.root, nullptr);

    const auto& root = *result.document.root;
    ASSERT_EQ(root.kind, wz::json::JSONValueKind::Object);
    ASSERT_EQ(root.object_members.size(), 2u);

    EXPECT_EQ(root.object_members[0].key, "name");
    ASSERT_NE(root.object_members[0].value, nullptr);
    EXPECT_EQ(root.object_members[0].value->kind,
        wz::json::JSONValueKind::String);
    EXPECT_EQ(root.object_members[0].value->string_value, "brick");

    EXPECT_EQ(root.object_members[1].key, "roughness");
    ASSERT_NE(root.object_members[1].value, nullptr);
    EXPECT_EQ(root.object_members[1].value->kind,
        wz::json::JSONValueKind::Number);
    EXPECT_DOUBLE_EQ(root.object_members[1].value->number_value, 0.8);
}

TEST(JSONParser, ParsesArray)
{
    const auto result = wz::json::parse_json_string(
        R"([true, false, null, 42])"
    );

    ASSERT_TRUE(result.ok);
    ASSERT_NE(result.document.root, nullptr);

    const auto& root = *result.document.root;
    ASSERT_EQ(root.kind, wz::json::JSONValueKind::Array);
    ASSERT_EQ(root.array_values.size(), 4u);

    ASSERT_NE(root.array_values[0], nullptr);
    EXPECT_EQ(root.array_values[0]->kind, wz::json::JSONValueKind::Bool);
    EXPECT_TRUE(root.array_values[0]->bool_value);

    ASSERT_NE(root.array_values[1], nullptr);
    EXPECT_EQ(root.array_values[1]->kind, wz::json::JSONValueKind::Bool);
    EXPECT_FALSE(root.array_values[1]->bool_value);

    ASSERT_NE(root.array_values[2], nullptr);
    EXPECT_EQ(root.array_values[2]->kind, wz::json::JSONValueKind::Null);

    ASSERT_NE(root.array_values[3], nullptr);
    EXPECT_EQ(root.array_values[3]->kind, wz::json::JSONValueKind::Number);
    EXPECT_DOUBLE_EQ(root.array_values[3]->number_value, 42.0);
}

TEST(JSONParser, PreservesObjectMemberOrder)
{
    const auto result = wz::json::parse_json_string(
        R"({
            "a": 1,
            "b": 2,
            "c": 3
        })"
    );

    ASSERT_TRUE(result.ok);
    ASSERT_NE(result.document.root, nullptr);

    const auto& root = *result.document.root;
    ASSERT_EQ(root.kind, wz::json::JSONValueKind::Object);
    ASSERT_EQ(root.object_members.size(), 3u);

    EXPECT_EQ(root.object_members[0].key, "a");
    EXPECT_EQ(root.object_members[1].key, "b");
    EXPECT_EQ(root.object_members[2].key, "c");
}

TEST(JSONParser, RejectsInvalidJSON)
{
    const auto result = wz::json::parse_json_string(
        R"({ "name": "brick", )"
    );

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.message, "");
}

TEST(JSONParser, RejectsDuplicateObjectKeys)
{
    const auto result = wz::json::parse_json_string(
        R"({
            "name": "brick",
            "name": "stone"
        })"
    );

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.message, "");
}