// tests/asset/scalar_field_set_tests.cpp
//
// Unit tests for ScalarFieldSet — named co-registered scalar field collection.

#include <engine/assets/scalar_field/scalar_field_set.h>

#include <gtest/gtest.h>

using namespace wz::engine::assets;

namespace
{
    // Helper: build a small valid ScalarFieldData with given dimensions
    // and uniform value.
    ScalarFieldData make_field(uint32_t w, uint32_t h, float fill = 0.0f)
    {
        ScalarFieldData f;
        f.width  = w;
        f.height = h;
        f.depth  = 1;
        f.values.resize(static_cast<size_t>(w) * h, fill);
        f.min_value = fill;
        f.max_value = fill;
        return f;
    }
}

// ── Basic insertion and lookup ─────────────────────────────────────

TEST(ScalarFieldSet, AddAndGet)
{
    ScalarFieldSet set;
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.size(), 0u);

    auto field = make_field(4, 4, 1.0f);
    EXPECT_TRUE(set.add("height", field));
    EXPECT_FALSE(set.empty());
    EXPECT_EQ(set.size(), 1u);

    const ScalarFieldData* got = set.get("height");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->width, 4u);
    EXPECT_EQ(got->height, 4u);
}

TEST(ScalarFieldSet, GetMissing_ReturnsNull)
{
    ScalarFieldSet set;
    set.add("height", make_field(4, 4));
    EXPECT_EQ(set.get("missing"), nullptr);
}

TEST(ScalarFieldSet, Contains)
{
    ScalarFieldSet set;
    set.add("height", make_field(4, 4));
    EXPECT_TRUE(set.contains("height"));
    EXPECT_FALSE(set.contains("rockiness"));
}

// ── Dimension establishment and enforcement ────────────────────────

TEST(ScalarFieldSet, FirstFieldEstablishesDimensions)
{
    ScalarFieldSet set;
    set.add("height", make_field(8, 6));
    EXPECT_EQ(set.width(), 8u);
    EXPECT_EQ(set.height(), 6u);
    EXPECT_EQ(set.depth(), 1u);
}

TEST(ScalarFieldSet, MatchingDimensionsAccepted)
{
    ScalarFieldSet set;
    EXPECT_TRUE(set.add("height",    make_field(4, 4, 0.0f)));
    EXPECT_TRUE(set.add("rockiness", make_field(4, 4, 0.5f)));
    EXPECT_TRUE(set.add("soil",      make_field(4, 4, 0.8f)));
    EXPECT_EQ(set.size(), 3u);
}

TEST(ScalarFieldSet, MismatchedWidth_Rejected)
{
    ScalarFieldSet set;
    set.add("height", make_field(4, 4));
    EXPECT_FALSE(set.add("bad", make_field(8, 4)));
    EXPECT_EQ(set.size(), 1u);
    EXPECT_FALSE(set.contains("bad"));
}

TEST(ScalarFieldSet, MismatchedHeight_Rejected)
{
    ScalarFieldSet set;
    set.add("height", make_field(4, 4));
    EXPECT_FALSE(set.add("bad", make_field(4, 8)));
    EXPECT_EQ(set.size(), 1u);
}

// ── Rejection cases ────────────────────────────────────────────────

TEST(ScalarFieldSet, EmptyName_Rejected)
{
    ScalarFieldSet set;
    EXPECT_FALSE(set.add("", make_field(4, 4)));
    EXPECT_TRUE(set.empty());
}

TEST(ScalarFieldSet, InvalidField_Rejected)
{
    ScalarFieldSet set;
    ScalarFieldData bad;
    bad.width = 4;
    bad.height = 4;
    bad.depth = 1;
    // values is empty → valid() returns false
    EXPECT_FALSE(set.add("bad", bad));
}

TEST(ScalarFieldSet, DuplicateName_Rejected)
{
    ScalarFieldSet set;
    set.add("height", make_field(4, 4, 0.0f));
    EXPECT_FALSE(set.add("height", make_field(4, 4, 1.0f)));
    EXPECT_EQ(set.size(), 1u);
    // Original value preserved.
    EXPECT_FLOAT_EQ(set.get("height")->min_value, 0.0f);
}

// ── Clear ──────────────────────────────────────────────────────────

TEST(ScalarFieldSet, Clear_ResetsEverything)
{
    ScalarFieldSet set;
    set.add("height", make_field(4, 4));
    set.add("rock",   make_field(4, 4));
    EXPECT_EQ(set.size(), 2u);

    set.clear();
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.width(), 0u);
    EXPECT_EQ(set.height(), 0u);

    // After clear, new dimensions can be established.
    EXPECT_TRUE(set.add("new", make_field(8, 8)));
    EXPECT_EQ(set.width(), 8u);
}

// ── Iteration ──────────────────────────────────────────────────────

TEST(ScalarFieldSet, Iteration_CoversAllEntries)
{
    ScalarFieldSet set;
    set.add("a", make_field(3, 3, 1.0f));
    set.add("b", make_field(3, 3, 2.0f));
    set.add("c", make_field(3, 3, 3.0f));

    size_t count = 0;
    for (const auto& [name, field] : set)
    {
        EXPECT_TRUE(name == "a" || name == "b" || name == "c");
        ++count;
    }
    EXPECT_EQ(count, 3u);
}
