#include <gtest/gtest.h>
#include <string_view>

#include <external/toml/toml_parser.h>

namespace
{
    const wz::toml::TOMLValue* find_member(
        const wz::toml::TOMLValue& table,
        std::string_view key)
    {
        if (table.kind != wz::toml::TOMLValueKind::Table)
            return nullptr;

        for (const auto& member : table.table_members) {
            if (member.key == key)
                return member.value.get();
        }

        return nullptr;
    }
}

TEST(TOMLParser, ParsesSimpleScalars)
{
    const auto result = wz::toml::parse_toml_string(
        R"(
            name = "brick"
            enabled = true
            count = 42
            roughness = 0.8
        )"
    );

    ASSERT_TRUE(result.ok) << result.error.message;
    ASSERT_NE(result.document.root, nullptr);

    const auto& root = *result.document.root;
    ASSERT_EQ(root.kind, wz::toml::TOMLValueKind::Table);
    ASSERT_EQ(root.table_members.size(), 4u);

    const auto* name = find_member(root, "name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->kind, wz::toml::TOMLValueKind::String);
    EXPECT_EQ(name->string_value, "brick");

    const auto* enabled = find_member(root, "enabled");
    ASSERT_NE(enabled, nullptr);
    EXPECT_EQ(enabled->kind, wz::toml::TOMLValueKind::Bool);
    EXPECT_TRUE(enabled->bool_value);

    const auto* count = find_member(root, "count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->kind, wz::toml::TOMLValueKind::Integer);
    EXPECT_EQ(count->integer_value, 42);

    const auto* roughness = find_member(root, "roughness");
    ASSERT_NE(roughness, nullptr);
    EXPECT_EQ(roughness->kind, wz::toml::TOMLValueKind::Float);
    EXPECT_DOUBLE_EQ(roughness->float_value, 0.8);
}

TEST(TOMLParser, ParsesArray)
{
    const auto result = wz::toml::parse_toml_string(
        R"(
            colors = ["red", "green", "blue"]
        )"
    );

    ASSERT_TRUE(result.ok) << result.error.message;
    ASSERT_NE(result.document.root, nullptr);

    const auto& root = *result.document.root;
    ASSERT_EQ(root.kind, wz::toml::TOMLValueKind::Table);
    ASSERT_EQ(root.table_members.size(), 1u);

    const auto* colors = root.table_members[0].value.get();

    ASSERT_NE(colors, nullptr);
    ASSERT_EQ(colors->kind, wz::toml::TOMLValueKind::Array);
    ASSERT_EQ(colors->array_values.size(), 3u);

    EXPECT_EQ(colors->array_values[0]->kind, wz::toml::TOMLValueKind::String);
    EXPECT_EQ(colors->array_values[0]->string_value, "red");

    EXPECT_EQ(colors->array_values[1]->kind, wz::toml::TOMLValueKind::String);
    EXPECT_EQ(colors->array_values[1]->string_value, "green");

    EXPECT_EQ(colors->array_values[2]->kind, wz::toml::TOMLValueKind::String);
    EXPECT_EQ(colors->array_values[2]->string_value, "blue");
}

TEST(TOMLParser, ParsesNestedTable)
{
    const auto result = wz::toml::parse_toml_string(
        R"(
            [material]
            name = "brick"

            [material.textures]
            albedo = "brick_albedo.png"
        )"
    );

    ASSERT_TRUE(result.ok) << result.error.message;
    ASSERT_NE(result.document.root, nullptr);

    const auto& root = *result.document.root;
    ASSERT_EQ(root.kind, wz::toml::TOMLValueKind::Table);
    ASSERT_EQ(root.table_members.size(), 1u);

    EXPECT_EQ(root.table_members[0].key, "material");

    const auto* material = root.table_members[0].value.get();
    ASSERT_NE(material, nullptr);
    ASSERT_EQ(material->kind, wz::toml::TOMLValueKind::Table);

    ASSERT_EQ(material->table_members.size(), 2u);
    EXPECT_EQ(material->table_members[0].key, "name");
    EXPECT_EQ(material->table_members[1].key, "textures");

    const auto* textures = material->table_members[1].value.get();
    ASSERT_NE(textures, nullptr);
    ASSERT_EQ(textures->kind, wz::toml::TOMLValueKind::Table);

    ASSERT_EQ(textures->table_members.size(), 1u);
    EXPECT_EQ(textures->table_members[0].key, "albedo");
    ASSERT_NE(textures->table_members[0].value, nullptr);
    EXPECT_EQ(textures->table_members[0].value->string_value, "brick_albedo.png");
}

TEST(TOMLParser, RejectsInvalidTOML)
{
    const auto result = wz::toml::parse_toml_string(
        R"(
            name = "brick
        )"
    );

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.message, "");
}