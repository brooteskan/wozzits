#include <gtest/gtest.h>

#include <external/json/json_parser.h>
#include <external/json/json_writer.h>

#include <memory>
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

TEST(JSONWriter, SerializesDocumentAndParsesRoundTrip)
{
    wz::json::JSONDocument document{};
    auto root = std::make_unique<wz::json::JSONValue>();
    root->kind = wz::json::JSONValueKind::Object;

    auto name = std::make_unique<wz::json::JSONValue>();
    name->kind = wz::json::JSONValueKind::String;
    name->string_value = "brick";

    auto roughness = std::make_unique<wz::json::JSONValue>();
    roughness->kind = wz::json::JSONValueKind::Number;
    roughness->number_value = 0.8;

    root->object_members.push_back(wz::json::JSONMember{
        .key = "name",
        .value = std::move(name),
    });
    root->object_members.push_back(wz::json::JSONMember{
        .key = "roughness",
        .value = std::move(roughness),
    });
    document.root = std::move(root);

    const std::string text = wz::json::serialize_json(document);
    const auto parsed = wz::json::parse_json_string(text);

    ASSERT_TRUE(parsed.ok) << parsed.error.message;
    ASSERT_NE(parsed.document.root, nullptr);
    ASSERT_EQ(parsed.document.root->kind, wz::json::JSONValueKind::Object);
    ASSERT_EQ(parsed.document.root->object_members.size(), 2u);
    EXPECT_EQ(parsed.document.root->object_members[0].key, "name");
    EXPECT_EQ(parsed.document.root->object_members[1].key, "roughness");
}

namespace
{
    std::string serialize_number(double value)
    {
        wz::json::JSONValue json{};
        json.kind = wz::json::JSONValueKind::Number;
        json.number_value = value;
        return wz::json::serialize_json(json, { .pretty = false });
    }
}

// #284: the writer used max_digits10, so 0.1 came back out as
// 0.10000000000000001 and merely loading + saving a document rewrote every
// float in it. Numbers must serialize as the SHORTEST text that still parses
// back to the identical double.
TEST(JSONWriter, WritesShortestRoundTrippingNumbers)
{
    EXPECT_EQ(serialize_number(0.1), "0.1");
    EXPECT_EQ(serialize_number(947.385236966102), "947.385236966102");
    EXPECT_EQ(serialize_number(2.0), "2");
    EXPECT_EQ(serialize_number(-0.75), "-0.75");

    // An integral value prints as an integer even when scientific notation
    // would be SHORTER. Shortest-only rendered these authored int params --
    // a triangle budget, a texture extent -- as "2e+05" and "1e+06", which
    // round-trips but is not what a person editing the file expects to read.
    EXPECT_EQ(serialize_number(200000.0), "200000");
    EXPECT_EQ(serialize_number(1000000.0), "1000000");
    EXPECT_EQ(serialize_number(-512.0), "-512");
}

TEST(JSONWriter, NumberSerializationIsAFixedPoint)
{
    // Values a project's asset graph actually carries: authored dials, a
    // computed placement, an integral count, and both magnitude extremes.
    const double values[] = {
        0.1, 0.35, -0.62, 947.385236966102, 1.0, 0.0,
        1.0e30, 1.5e-7, 3.4028234663852886e38,
    };

    for (const double value : values) {
        const std::string once = serialize_number(value);

        const auto parsed = wz::json::parse_json_string(once);
        ASSERT_TRUE(parsed.ok) << once << ": " << parsed.error.message;
        ASSERT_NE(parsed.document.root, nullptr) << once;
        ASSERT_EQ(parsed.document.root->kind, wz::json::JSONValueKind::Number)
            << once;

        // Exact, not near: a lossy write would show up here first.
        EXPECT_DOUBLE_EQ(parsed.document.root->number_value, value) << once;

        // ...and writing what we just read reproduces the same text, so
        // re-saving an unchanged document is byte-identical.
        EXPECT_EQ(serialize_number(parsed.document.root->number_value), once);
    }
}

// Negative zero writes as "0" -- it takes the integral path, and that is what
// a round trip produces anyway since the parser returns +0. Pinned so the
// normalisation is a stated property rather than a surprise, and so the round
// trip is a fixed point from the very first write.
TEST(JSONWriter, NegativeZeroWritesAsZero)
{
    EXPECT_EQ(serialize_number(-0.0), "0");

    const auto parsed = wz::json::parse_json_string("-0");
    ASSERT_TRUE(parsed.ok) << parsed.error.message;
    ASSERT_NE(parsed.document.root, nullptr);
    EXPECT_EQ(serialize_number(parsed.document.root->number_value), "0");
}

TEST(JSONWriter, EscapesStrings)
{
    wz::json::JSONValue value{};
    value.kind = wz::json::JSONValueKind::String;
    value.string_value = "line\nquote\"slash\\";

    const auto text = wz::json::serialize_json(value, {
        .pretty = false,
    });

    EXPECT_EQ(text, R"("line\nquote\"slash\\")");

    const auto parsed = wz::json::parse_json_string(text);
    ASSERT_TRUE(parsed.ok) << parsed.error.message;
    ASSERT_NE(parsed.document.root, nullptr);
    EXPECT_EQ(parsed.document.root->string_value, value.string_value);
}
