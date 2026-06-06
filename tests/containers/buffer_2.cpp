#include <gtest/gtest.h>
#include <containers/buffer.h>

using namespace wz::core::containers;

namespace
{
    struct Tracker
    {
        static int alive;

        Tracker() { alive++; }
        ~Tracker() { alive--; }
    };

    int Tracker::alive = 0;

    struct alignas(64) BigAligned
    {
        int x;
    };
}


TEST(Buffer, IterationMatchesContent)
{
    int storage[3];
    auto b = Buffer<int>::wrap(storage, 3);

    b.push(10);
    b.push(20);
    b.push(30);

    int expected[] = { 10, 20, 30 };
    int i = 0;

    for (auto& v : b)
    {
        EXPECT_EQ(v, expected[i++]);
    }

    EXPECT_EQ(i, 3);
}