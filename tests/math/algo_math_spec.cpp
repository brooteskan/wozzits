#include <gtest/gtest.h>
#include <containers/buffer.h>
#include <algo/algo.h>

using namespace wz::core;

TEST(AlgoMathSpec, TransformUsesMathFunctions)
{
    int storage[3] = { 1, 4, 9 };
    int out_storage[3] = { 0, 0, 0 };

    auto in = wz::core::containers::Buffer<int>::wrap_existing(storage, 3, 3);
    auto out = wz::core::containers::Buffer<int>::wrap(out_storage, 3);

    algo::transform(in, out, [](int v)
        {
            return static_cast<int>(std::sqrt(v));
        });

    EXPECT_EQ(out.data()[0], 1);
    EXPECT_EQ(out.data()[1], 2);
    EXPECT_EQ(out.data()[2], 3);
}

TEST(AlgoMathSpec, TransformIsDeterministicWithMath)
{
    int storage[3] = { 2, 3, 4 };
    int out1[3] = { 0 };
    int out2[3] = { 0 };

    auto in = wz::core::containers::Buffer<int>::wrap_existing(storage, 3, 3);

    auto a = wz::core::containers::Buffer<int>::wrap(out1, 3);
    auto b = wz::core::containers::Buffer<int>::wrap(out2, 3);

    algo::transform(in, a, [](int v) { return v * v; });
    algo::transform(in, b, [](int v) { return v * v; });

    EXPECT_EQ(a.data()[0], b.data()[0]);
    EXPECT_EQ(a.data()[1], b.data()[1]);
    EXPECT_EQ(a.data()[2], b.data()[2]);
}

TEST(AlgoMathSpec, ReduceWithMathOperations)
{
    int storage[4] = { 1, 2, 3, 4 };

    auto b = wz::core::containers::Buffer<int>::wrap_existing(storage, 4, 4);

    int sum_of_squares = algo::reduce(b, 0, [](int acc, int v)
        {
            return acc + (v * v);
        });

    EXPECT_EQ(sum_of_squares, 30);
}

TEST(AlgoMathSpec, FilterUsesMathPredicates)
{
    int storage[5] = { 1, 2, 3, 4, 5 };
    int out_storage[5] = { 0 };

    auto in = wz::core::containers::Buffer<int>::wrap_existing(storage, 5, 5);
    auto out = wz::core::containers::Buffer<int>::wrap(out_storage, 5);

    algo::filter(in, out, [](int v)
        {
            return (v % 2) == 0; // math predicate
        });

    EXPECT_EQ(out.data()[0], 2);
    EXPECT_EQ(out.data()[1], 4);
}

