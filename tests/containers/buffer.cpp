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

TEST(Buffer, PushWritesSequentially)
{
    int storage[4];
    auto b = Buffer<int>::wrap(storage, 4);

    EXPECT_TRUE(b.push(10));
    EXPECT_TRUE(b.push(20));

    EXPECT_EQ(b.count(), 2);
    EXPECT_EQ(storage[0], 10);
    EXPECT_EQ(storage[1], 20);
}

TEST(Buffer, PushStopsAtCapacity)
{
    int storage[2];
    auto b = Buffer<int>::wrap(storage, 2);

    EXPECT_TRUE(b.push(1));
    EXPECT_TRUE(b.push(2));
    EXPECT_FALSE(b.push(3));  // overflow is handled, not fatal
}

TEST(Buffer, HasSpaceWorks)
{
    int storage[3];
    auto b = Buffer<int>::wrap(storage, 3);

    EXPECT_TRUE(b.has_space(3));
    b.push(1);
    EXPECT_TRUE(b.has_space(2));
    EXPECT_FALSE(b.has_space(3));
}

TEST(Buffer, ResetClearsCountOnly)
{
    int storage[2] = { 42, 99 };
    auto b = Buffer<int>::wrap(storage, 2);

    b.push(1);
    b.push(2);

    b.reset();

    EXPECT_EQ(b.count(), 0);

    // memory should remain untouched
    EXPECT_EQ(storage[0], 1);
    EXPECT_EQ(storage[1], 2);
}

TEST(Buffer, WrapExistingRespectsInitialCount)
{
    int storage[4] = { 1,2,3,4 };

    auto b = Buffer<int>::wrap_existing(storage, 2, 4);

    EXPECT_EQ(b.count(), 2);

    b.push(99);

    EXPECT_EQ(storage[2], 99);
}

TEST(Buffer, ZeroCapacity)
{
    auto b = Buffer<int>::wrap(nullptr, 0);

    EXPECT_FALSE(b.push(1));      // cannot write into zero-capacity buffer
    EXPECT_TRUE(b.has_space(0));  // vacuously true
    EXPECT_FALSE(b.has_space(1)); // no space for anything
}

TEST(Buffer, DoesNotOverwriteBeyondCapacity)
{
    int storage[3] = { 0,0,0 };
    auto b = Buffer<int>::wrap(storage, 3);

    for (int i = 0; i < 100; i++)
        b.push(i);

    EXPECT_EQ(b.count(), 3);
    EXPECT_EQ(storage[0], 0);
    EXPECT_EQ(storage[1], 1);
    EXPECT_EQ(storage[2], 2);
}

TEST(Buffer, AliasingTwoBuffersSameStorage)
{
    int storage[4];

    auto b1 = Buffer<int>::wrap(storage, 4);
    auto b2 = Buffer<int>::wrap(storage, 4);

    EXPECT_TRUE(b1.push(1));
    EXPECT_TRUE(b2.push(2));

    // This is NOT deterministic ordering anymore unless you define it
    EXPECT_NE(storage[0], storage[1]);
}

TEST(Buffer, WrapExistingInitialStateIntegrity)
{
    int storage[4] = { 10, 20, 30, 40 };

    auto b = Buffer<int>::wrap_existing(storage, 2, 4);

    EXPECT_EQ(b.count(), 2);

    EXPECT_TRUE(b.push(99));

    EXPECT_EQ(storage[2], 99);
    EXPECT_EQ(b.count(), 3);
}

TEST(Buffer, WrapExistingRejectsInvalidCount)
{
    int storage[4];

    // This should trigger assert or at least be invalid
    EXPECT_DEATH(
        Buffer<int>::wrap_existing(storage, 5, 4),
        ""
    );
}


TEST(Buffer, ResetAllowsReuseWithoutCorruption)
{
    int storage[2];

    auto b = Buffer<int>::wrap(storage, 2);

    EXPECT_TRUE(b.push(1));
    EXPECT_TRUE(b.push(2));

    b.reset();

    EXPECT_TRUE(b.push(3));
    EXPECT_EQ(storage[0], 3);
}

TEST(Buffer, ZeroCapacityStress)
{
    auto b = Buffer<int>::wrap(nullptr, 0);

    for (int i = 0; i < 1000; i++)
    {
        EXPECT_FALSE(b.push(i));
        EXPECT_FALSE(b.has_space(1));
        EXPECT_EQ(b.count(), 0);
    }
}

TEST(Buffer, FailedPushDoesNotMutateState)
{
    int storage[2] = { 0, 0 };
    auto b = Buffer<int>::wrap(storage, 2);

    b.push(1);
    b.push(2);

    auto before = b.count();

    EXPECT_FALSE(b.push(999));

    EXPECT_EQ(b.count(), before);
    EXPECT_EQ(storage[0], 1);
    EXPECT_EQ(storage[1], 2);
}

TEST(Buffer, DoesNotTouchUnwrittenMemory)
{
    int storage[4] = { 0, 0, 0, 0 };

    auto b = Buffer<int>::wrap(storage, 4);

    b.push(1);
    b.push(2);

    EXPECT_EQ(storage[2], 0);
    EXPECT_EQ(storage[3], 0);
}

TEST(Buffer, SequentialIndexIntegrity)
{
    int storage[5];
    auto b = Buffer<int>::wrap(storage, 5);

    for (int i = 0; i < 5; i++)
        EXPECT_TRUE(b.push(i));

    for (int i = 0; i < 5; i++)
        EXPECT_EQ(storage[i], i);
}



TEST(Buffer, NonTrivialTypeConstructionDestruction)
{
    Tracker::alive = 0;

    Tracker storage[2];

    {
        auto b = Buffer<Tracker>::wrap(storage, 2);
        b.push(Tracker{});
        b.push(Tracker{});
    }

    // Buffer does NOT destroy elements
    EXPECT_EQ(Tracker::alive, 2);
}

TEST(Buffer, MoveOnlyTypes)
{
    std::unique_ptr<int> storage[2];

    auto b = Buffer<std::unique_ptr<int>>::wrap(storage, 2);

    EXPECT_TRUE(b.push(std::make_unique<int>(5)));
    EXPECT_TRUE(b.push(std::make_unique<int>(10)));

    EXPECT_EQ(*storage[0], 5);
    EXPECT_EQ(*storage[1], 10);
}



TEST(Buffer, AlignmentSafety)
{
    alignas(64) BigAligned storage[2];

    auto b = Buffer<BigAligned>::wrap(storage, 2);

    EXPECT_TRUE(b.push({ 1 }));
    EXPECT_TRUE(b.push({ 2 }));
}

TEST(Buffer, ReuseWithoutResetIsSafeButPredictable)
{
    int storage[2];

    auto b = Buffer<int>::wrap(storage, 2);

    EXPECT_TRUE(b.push(1));
    EXPECT_TRUE(b.push(2));

    EXPECT_FALSE(b.push(3));

    // Without reset, state is still full
    EXPECT_EQ(b.count(), 2);
}



TEST(Buffer, FailedPushLeavesNoPartialState)
{
    int storage[2] = { 7, 7 };

    auto b = Buffer<int>::wrap(storage, 2);

    b.push(1);
    b.push(2);

    EXPECT_FALSE(b.push(999));

    EXPECT_EQ(storage[0], 1);
    EXPECT_EQ(storage[1], 2);
}

TEST(Buffer, RandomizedStressNoCrash)
{
    int storage[128];
    auto b = Buffer<int>::wrap(storage, 128);

    for (int i = 0; i < 10000; i++)
    {
        b.push(i);
        EXPECT_LE(b.count(), b.capacity());
    }
}

TEST(Buffer, MoveOnlyStress)
{
    std::unique_ptr<int> storage[2];

    auto b = Buffer<std::unique_ptr<int>>::wrap(storage, 2);

    EXPECT_TRUE(b.push(std::make_unique<int>(1)));
    EXPECT_TRUE(b.push(std::make_unique<int>(2)));
    EXPECT_FALSE(b.push(std::make_unique<int>(3)));
}