#include <gtest/gtest.h>
#include <containers/buffer.h>
#include <algo/next.h>

TEST(AlgoNext, Transform_Basic)
{
    std::vector<int> in = { 1, 2, 3 };
    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    wz::core::algo::next::transform(in, out,
        [](int x) { return x * 2; });

    ASSERT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 2);
    EXPECT_EQ(out.data()[1], 4);
    EXPECT_EQ(out.data()[2], 6);
}

TEST(AlgoNext, Filter_Basic)
{
    std::vector<int> in = { 1, 2, 3, 4, 5, 6 };
    int storage[6] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 6);

    wz::core::algo::next::filter(in, out,
        [](int x) { return x % 2 == 0; });

    ASSERT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 2);
    EXPECT_EQ(out.data()[1], 4);
    EXPECT_EQ(out.data()[2], 6);
}

TEST(AlgoNext, Reduce_Basic)
{
    std::vector<int> in = { 1, 2, 3, 4 };

    int sum = wz::core::algo::next::reduce(in, 0,
        [](int acc, int v) { return acc + v; });

    EXPECT_EQ(sum, 10);
}

TEST(AlgoNext, MapThenFilter_Baseline)
{
    std::vector<int> in = { 1, 2, 3, 4, 5, 6 };

    int storage[6] = {};
    auto tmp = wz::core::containers::Buffer<int>::wrap(storage, 6);

    int out_storage[6] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(out_storage, 6);

    wz::core::algo::next::transform(in, tmp,
        [](int x) { return x * 2; });

    wz::core::algo::next::filter(tmp, out,
        [](int x) { return x % 3 == 0; });

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 6);
    EXPECT_EQ(out.data()[1], 12);
}

TEST(AlgoNext, MapFilter_FusedMatchesBaseline)
{
    std::vector<int> in = { 1, 2, 3, 4, 5, 6 };

    int storage[6] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage,6);

    auto op =
        wz::core::algo::next::map([](int x) { return x * 2; }) |
        wz::core::algo::next::filter([](int x) { return x % 3 == 0; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 6);
    EXPECT_EQ(out.data()[1], 12);
}

TEST(AlgoNext, MapFilter_PreservesOrder)
{
    std::vector<int> in = { 3, 1, 6, 2 };
    int storage[6] = {};

    auto out = wz::core::containers::Buffer<int>::wrap(storage, 6);

    auto op =
        wz::core::algo::next::map([](int x) { return x * 10; }) |
        wz::core::algo::next::filter([](int x) { return x >= 20; });

    op(in, out);

    ASSERT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 30);
    EXPECT_EQ(out.data()[1], 60);
}

TEST(AlgoNext, TransformAndReduce_Consistency)
{
    std::vector<int> in = { 1, 2, 3 };
    int storage[3] = {};

    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    wz::core::algo::next::transform(in, out,
        [](int x) { return x + 1; });

    int sum = wz::core::algo::next::reduce(out, 0,
        [](int acc, int v) { return acc + v; });

    EXPECT_EQ(sum, 9); // (2 + 3 + 4)
}

TEST(AlgoNext, Transform_TruncatesOnOverflow)
{
    std::vector<int> in = { 1, 2, 3, 4, 5 };

    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    wz::core::algo::next::transform(in, out,
        [](int x) { return x * 10; });

    EXPECT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 10);
    EXPECT_EQ(out.data()[1], 20);
    EXPECT_EQ(out.data()[2], 30);
}

TEST(AlgoNext, Transform_FunctionPointer)
{
    std::vector<int> in = { 1, 2, 3 };
    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    auto func = [](int x) { return x + 1; };

    wz::core::algo::next::transform(in, out, +func);

    EXPECT_EQ(out.data()[0], 2);
    EXPECT_EQ(out.data()[1], 3);
    EXPECT_EQ(out.data()[2], 4);
}

TEST(AlgoNext, Filter_StatefulPredicate)
{
    std::vector<int> in = { 1, 2, 3, 4, 5, 6 };

    int storage[6] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 6);

    int threshold = 3;

    wz::core::algo::next::filter(in, out,
        [threshold](int x) { return x > threshold; });

    ASSERT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 4);
    EXPECT_EQ(out.data()[1], 5);
    EXPECT_EQ(out.data()[2], 6);
}

TEST(AlgoNext, Reduce_IsDeterministic)
{
    std::vector<int> in = { 1, 2, 3, 4 };

    auto sum1 = wz::core::algo::next::reduce(in, 0,
        [](int acc, int v) { return acc + v; });

    auto sum2 = wz::core::algo::next::reduce(in, 0,
        [](int acc, int v) { return acc + v; });

    EXPECT_EQ(sum1, sum2);
    EXPECT_EQ(sum1, 10);
}

TEST(AlgoNext, MapFilter_FusedHandlesNonLinearPredicate)
{
    std::vector<int> in = { 1, 2, 3, 4, 5 };

    int storage[5] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 5);

    auto op =
        wz::core::algo::next::map([](int x) { return x * x; }) |
        wz::core::algo::next::filter([](int x) { return x > 10; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 16);
    EXPECT_EQ(out.data()[1], 25);
}

TEST(AlgoNext, MapFilter_FusedRespectsOverflowPolicy)
{
    std::vector<int> in = { 1, 2, 3, 4, 5, 6 };

    int storage1[2] = {};
    int storage2[2] = {};

    auto out1 = wz::core::containers::Buffer<int>::wrap(storage1, 2);
    auto out2 = wz::core::containers::Buffer<int>::wrap(storage2, 2);

    wz::core::algo::next::transform(in, out1,
        [](int x) { return x * 2; });

    wz::core::algo::next::filter(out1, out1,
        [](int x) { return x % 3 == 0; });

    auto op =
        wz::core::algo::next::map([](int x) { return x * 2; }) |
        wz::core::algo::next::filter([](int x) { return x % 3 == 0; });

    op(in, out2);

    ASSERT_EQ(out1.count(), out2.count());
}

TEST(AlgoNext, WorksWithStdVectorAsRange)
{
    std::vector<int> in = { 1, 2, 3 };

    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    wz::core::algo::next::transform(in, out,
        [](int x) { return x + 100; });

    EXPECT_EQ(out.data()[0], 101);
    EXPECT_EQ(out.data()[1], 102);
    EXPECT_EQ(out.data()[2], 103);
}

TEST(AlgoNext, Transform_StopsOnOverflow)
{
    std::vector<int> in = { 1, 2, 3, 4, 5, 6, 7, 8 };

    int storage[3] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 3);

    wz::core::algo::next::transform(in, out,
        [](int x) { return x; });

    EXPECT_EQ(out.count(), 3);
    EXPECT_LT(out.data()[2], 10);
}

TEST(AlgoNext, FilterT_Basic)
{
    std::vector<int> in = { 1, 2, 3, 4, 5, 6 };

    int storage[6] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 6);

    wz::core::algo::next::filter_t pred =
        wz::core::algo::next::filter([](int x) { return x % 2 == 0; });

    pred(in, out);

    ASSERT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 2);
    EXPECT_EQ(out.data()[1], 4);
    EXPECT_EQ(out.data()[2], 6);
}

// this one only uses filter, not filter + filter_t
TEST(AlgoNext, FilterT_EqualsDirectFilter)
{
    std::vector<int> in = { 1, 2, 3, 4, 5 };

    int storage1[5] = {};
    int storage2[5] = {};

    auto out1 = wz::core::containers::Buffer<int>::wrap(storage1, 5);
    auto out2 = wz::core::containers::Buffer<int>::wrap(storage2, 5);

    wz::core::algo::next::filter(in, out1,
        [](int x) { return x > 2; });

    auto op = wz::core::algo::next::filter([](int x) { return x > 2; });
    op(in, out2);

    ASSERT_EQ(out1.count(), out2.count());

    for (uint32_t i = 0; i < out1.count(); ++i)
        EXPECT_EQ(out1.data()[i], out2.data()[i]);
}

TEST(AlgoNext, FilterT_PreservesOrder)
{
    std::vector<int> in = { 10, 1, 20, 2, 30, 3 };

    int storage[6] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 6);

    auto op = wz::core::algo::next::filter([](int x) { return x >= 10; });

    op(in, out);

    ASSERT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 10);
    EXPECT_EQ(out.data()[1], 20);
    EXPECT_EQ(out.data()[2], 30);
}

TEST(AlgoNext, FilterT_StatefulPredicate)
{
    std::vector<int> in = { 1, 2, 3, 4, 5 };

    int storage[5] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 5);

    int threshold = 3;

    auto op = wz::core::algo::next::filter(
        [threshold](int x) { return x > threshold; });

    op(in, out);

    ASSERT_EQ(out.count(), 2);
    EXPECT_EQ(out.data()[0], 4);
    EXPECT_EQ(out.data()[1], 5);
}

TEST(AlgoNext, MapT_Then_FilterT_Composition)
{
    std::vector<int> in = { 1, 2, 3, 4 };

    int storage[4] = {};
    auto out = wz::core::containers::Buffer<int>::wrap(storage, 4);

    auto pipeline =
        wz::core::algo::next::map([](int x) { return x * 10; }) |
        wz::core::algo::next::filter([](int x) { return x >= 20; });

    pipeline(in, out);

    ASSERT_EQ(out.count(), 3);
    EXPECT_EQ(out.data()[0], 20);
    EXPECT_EQ(out.data()[1], 30);
    EXPECT_EQ(out.data()[2], 40);
}