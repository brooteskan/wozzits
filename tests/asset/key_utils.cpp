#include <gtest/gtest.h>

#include <asset/key_utils.h>

#include <vector>

namespace
{
    wz::asset::AssetKey make_key(uint64_t content_lo)
    {
        wz::asset::AssetKey key{};
        key.content_hash = { content_lo, 0x42 };
        return key;
    }
}

TEST(AssetKeyUtils, EmptyRecognizesDefaultConstructedKeys)
{
    EXPECT_TRUE(wz::asset::empty(wz::asset::AssetKey{}));
    EXPECT_FALSE(wz::asset::empty(make_key(0x100)));
}

TEST(AssetKeyUtils, ContainsKeyFindsExactMatches)
{
    const std::vector<wz::asset::AssetKey> keys{
        make_key(0x1),
        make_key(0x2),
        make_key(0x3),
    };

    EXPECT_FALSE(wz::asset::contains_key(keys, make_key(0x4)));
    EXPECT_TRUE(wz::asset::contains_key(keys, make_key(0x2)));
}

TEST(AssetKeyUtils, AppendUniqueKeyReportsWhetherItAppended)
{
    std::vector<wz::asset::AssetKey> keys{ make_key(0x1) };

    EXPECT_TRUE(wz::asset::append_unique_key(keys, make_key(0x2)));
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys.back(), make_key(0x2));

    EXPECT_FALSE(wz::asset::append_unique_key(keys, make_key(0x2)));
    EXPECT_EQ(keys.size(), 2u);
}

TEST(AssetKeyUtils, AppendUniqueNonEmptyKeySkipsEmptyAndDuplicates)
{
    std::vector<wz::asset::AssetKey> keys{};

    EXPECT_FALSE(wz::asset::append_unique_non_empty_key(
        keys,
        wz::asset::AssetKey{}));
    EXPECT_TRUE(wz::asset::append_unique_non_empty_key(keys, make_key(0x1)));
    EXPECT_FALSE(wz::asset::append_unique_non_empty_key(keys, make_key(0x1)));

    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys.front(), make_key(0x1));
}
