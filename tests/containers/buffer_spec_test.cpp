#include <gtest/gtest.h>
#include <containers/buffer.h>

using namespace wz::core::containers;

/*
    BUFFER SEMANTIC SPEC

    These tests define *what Buffer means*, not how it is implemented.

    If any of these fail, it indicates a semantic regression or incorrect usage
    in higher-level code (algo, engine, etc.).
*/

namespace
{
    struct Trivial
    {
        int v;
    };
}

/*
------------------------------------------------------------
1. wrap() ALWAYS produces an empty logical buffer
------------------------------------------------------------
*/
TEST(BufferSpec, WrapProducesEmptyLogicalBuffer)
{
    int storage[3] = { 1, 2, 3 };

    auto b = Buffer<int>::wrap(storage, 3);

    EXPECT_EQ(b.count(), 0);
    EXPECT_EQ(b.capacity(), 3);
}

/*
------------------------------------------------------------
2. wrap() never assumes initialized data
------------------------------------------------------------
*/
TEST(BufferSpec, WrapDoesNotImplyInitializedData)
{
    int storage[3] = { 10, 20, 30 };

    auto b = Buffer<int>::wrap(storage, 3);

    // must be empty logically even if memory contains values
    EXPECT_EQ(b.count(), 0);

    // pushing defines logical content
    b.push(1);

    EXPECT_EQ(b.count(), 1);
    EXPECT_EQ(storage[0], 1);
}

/*
------------------------------------------------------------
3. wrap_existing defines logical size explicitly
------------------------------------------------------------
*/
TEST(BufferSpec, WrapExistingRespectsLogicalSize)
{
    int storage[3] = { 1, 2, 3 };

    auto b = Buffer<int>::wrap_existing(storage, 2, 3);

    EXPECT_EQ(b.count(), 2);
}

/*
------------------------------------------------------------
4. wrap_existing does NOT validate contents
------------------------------------------------------------
*/
TEST(BufferSpec, WrapExistingDoesNotValidateMemory)
{
    int storage[3] = { 999, 888, 777 };

    auto b = Buffer<int>::wrap_existing(storage, 3, 3);

    EXPECT_EQ(b.count(), 3);

    // semantics: buffer does not inspect or modify existing memory
    EXPECT_EQ(b.data()[0], 999);
}

/*
------------------------------------------------------------
5. count always represents logical elements, not capacity
------------------------------------------------------------
*/
TEST(BufferSpec, CountNeverEqualsCapacityUnlessFilled)
{
    int storage[3];

    auto b = Buffer<int>::wrap(storage, 3);

    EXPECT_NE(b.count(), b.capacity());

    b.push(1);
    b.push(2);
    b.push(3);

    EXPECT_EQ(b.count(), b.capacity());
}

/*
------------------------------------------------------------
6. reset only affects logical state
------------------------------------------------------------
*/
TEST(BufferSpec, ResetOnlyAffectsLogicalSize)
{
    int storage[2];

    auto b = Buffer<int>::wrap(storage, 2);

    b.push(1);
    b.push(2);

    b.reset();

    EXPECT_EQ(b.count(), 0);

    // underlying memory remains unchanged
    EXPECT_EQ(storage[0], 1);
    EXPECT_EQ(storage[1], 2);
}

/*
------------------------------------------------------------
7. overflow does not modify logical state
------------------------------------------------------------
*/
TEST(BufferSpec, OverflowDoesNotCorruptState)
{
    int storage[2];

    auto b = Buffer<int>::wrap(storage, 2);

    b.push(1);
    b.push(2);

    auto before = b.count();

    EXPECT_FALSE(b.push(3));

    EXPECT_EQ(b.count(), before);
}

/*
------------------------------------------------------------
8. push always appends sequentially
------------------------------------------------------------
*/
TEST(BufferSpec, PushIsSequential)
{
    int storage[3];

    auto b = Buffer<int>::wrap(storage, 3);

    b.push(10);
    b.push(20);
    b.push(30);

    EXPECT_EQ(storage[0], 10);
    EXPECT_EQ(storage[1], 20);
    EXPECT_EQ(storage[2], 30);
}

/*
------------------------------------------------------------
9. capacity is immutable
------------------------------------------------------------
*/
TEST(BufferSpec, CapacityIsImmutable)
{
    int storage[2];

    auto b = Buffer<int>::wrap(storage, 2);

    EXPECT_EQ(b.capacity(), 2);

    b.push(1);
    b.push(2);

    EXPECT_EQ(b.capacity(), 2);
}

/*
------------------------------------------------------------
10. zero-capacity buffer is valid and inert
------------------------------------------------------------
*/
TEST(BufferSpec, ZeroCapacityIsValid)
{
    auto b = Buffer<int>::wrap(nullptr, 0);

    EXPECT_EQ(b.capacity(), 0);
    EXPECT_EQ(b.count(), 0);

    EXPECT_FALSE(b.push(1));
    EXPECT_EQ(b.remaining(), 0);
}