#include <gtest/gtest.h>
#include <containers/buffer.h>
#include <algo/pipeline.h>
#include <algo/next.h>
#include <tuple>


TEST(Pipeline, MapBasic)
{
    std::vector<int> in = { 1, 2, 3 };

    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    auto op = wz::core::algo::pipeline::map(
        [](int x) { return x * 2; });

    op(in, out);

    ASSERT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 2);
    EXPECT_EQ(out.data()[1], 4);
    EXPECT_EQ(out.data()[2], 6);
}

TEST(Pipeline, FilterBasic)
{
    std::vector<int> in = { 1, 2, 3, 4, 5 };

    int storage[5] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 5);

    auto op = wz::core::algo::pipeline::filter(
        [](int x) { return x % 2 == 0; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 2);
    EXPECT_EQ(out.data()[1], 4);
}

TEST(Pipeline, MapFilter_FusedMatchesAlgo)
{
    std::vector<int> in = { 1, 2, 3, 4, 5, 6 };

    // baseline (algo layer)
    int tmp_storage[6] = {};
    auto tmp = wz::core::containers::Buffer<int>::wrap(tmp_storage, 6);

    int expected_storage[6] = {};
    auto expected = wz::core::containers::Buffer<int>::wrap(expected_storage, 6);

    wz::core::algo::next::transform(in, tmp,
        [](int x) { return x * 2; });

    wz::core::algo::next::filter(tmp, expected,
        [](int x) { return x % 3 == 0; });

    // pipeline (fused)
    int out_storage[6] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(out_storage, 6);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x * 2; }) |
        wz::core::algo::pipeline::filter([](int x) { return x % 3 == 0; });

    op(in, out);

    ASSERT_EQ(out.count(), expected.count());

    for (uint32_t i = 0; i < out.count(); ++i)
        EXPECT_EQ(out.data()[i], expected.data()[i]);
}

TEST(Pipeline, MultiStage_Fusion)
{
    std::vector<int> in = { 1, 2, 3, 4 };

    int storage[4] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 4);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x * 2; }) |
        wz::core::algo::pipeline::filter([](int x) { return x > 4; }) |
        wz::core::algo::pipeline::map([](int x) { return x + 1; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 7);  // (3*2=6 -> +1)
    EXPECT_EQ(out.data()[1], 9);  // (4*2=8 -> +1)
}

TEST(Pipeline, PreservesOrder)
{
    std::vector<int> in = { 5, 1, 4, 2 };

    int storage[4] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 4);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x; }) |
        wz::core::algo::pipeline::filter([](int x) { return x % 2 == 0; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 4);
    EXPECT_EQ(out.data()[1], 2);
}

TEST(Pipeline, TruncatesOnOverflow)
{
    std::vector<int> in = { 1, 2, 3, 4, 5 };

    int storage[2] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 2);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x * 10; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 10);
    EXPECT_EQ(out.data()[1], 20);
}

TEST(Pipeline, FilterSkipsCorrectly)
{
    std::vector<int> in = { 1, 2, 3 };

    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    auto op =
        wz::core::algo::pipeline::filter([](int) { return false; });

    op(in, out);

    EXPECT_EQ(out.count(), 0);
}

TEST(Pipeline, MapMap_Fusion)
{
    std::vector<int> in = { 1, 2, 3 };

    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x * 2; }) |
        wz::core::algo::pipeline::map([](int x) { return x + 1; });

    op(in, out);

    EXPECT_EQ(out.data()[0], 3);
    EXPECT_EQ(out.data()[1], 5);
    EXPECT_EQ(out.data()[2], 7);
}

TEST(Pipeline, MapFilterMap_MatchesAlgo)
{
    std::vector<int> in = { 1, 2, 3, 4, 5 };

    // baseline (algo)
    int tmp_storage[5] = {};
    auto tmp = wz::core::containers::Buffer<int>::wrap(tmp_storage, 5);

    int expected_storage[5] = {};
    auto expected = wz::core::containers::Buffer<int>::wrap(expected_storage, 5);

    wz::core::algo::next::transform(in, tmp,
        [](int x) { return x * 2; });

    wz::core::algo::next::filter(tmp, expected,
        [](int x) { return x > 4; });

    int final_storage[5] = {};
    auto final = wz::core::containers::Buffer<int>::wrap(final_storage, 5);

    wz::core::algo::next::transform(expected, final,
        [](int x) { return x + 1; });

    // pipeline
    int out_storage[5] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(out_storage, 5);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x * 2; }) |
        wz::core::algo::pipeline::filter([](int x) { return x > 4; }) |
        wz::core::algo::pipeline::map([](int x) { return x + 1; });

    op(in, out);

    ASSERT_EQ(out.count(), final.count());
    for (uint32_t i = 0; i < out.count(); ++i)
        EXPECT_EQ(out.data()[i], final.data()[i]);
}

TEST(Pipeline, MapFilterMap_Basic)
{
    std::vector<int> in = { 1, 2, 3, 4 };

    int storage[4] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 4);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x * 2; }) |
        wz::core::algo::pipeline::filter([](int x) { return x > 4; }) |
        wz::core::algo::pipeline::map([](int x) { return x + 1; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 7); // 3*2=6 → +1
    EXPECT_EQ(out.data()[1], 9); // 4*2=8 → +1
}

TEST(Pipeline, MapFilterMap_PreservesOrder)
{
    std::vector<int> in = { 5, 1, 4, 2 };

    int storage[4] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 4);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x; }) |
        wz::core::algo::pipeline::filter([](int x) { return x % 2 == 0; }) |
        wz::core::algo::pipeline::map([](int x) { return x * 10; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 40);
    EXPECT_EQ(out.data()[1], 20);
}

TEST(Pipeline, MapFilterMap_TruncatesOnOverflow)
{
    std::vector<int> in = { 1, 2, 3, 4, 5 };

    int storage[2] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 2);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x * 2; }) |
        wz::core::algo::pipeline::filter([](int x) { return x > 2; }) |
        wz::core::algo::pipeline::map([](int x) { return x + 1; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
}

TEST(Pipeline, MapFilterMap_AllFilteredOut)
{
    std::vector<int> in = { 1, 2, 3 };

    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    auto op =
        wz::core::algo::pipeline::map([](int x) { return x * 2; }) |
        wz::core::algo::pipeline::filter([](int) { return false; }) |
        wz::core::algo::pipeline::map([](int x) { return x + 1; });

    op(in, out);

    EXPECT_EQ(out.count(), 0);
}

