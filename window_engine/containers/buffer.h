//file: twe/core/containers/buffer.h

#pragma once
#include <cstdint>
#include <cassert>
#include <utility>

#define WZ_BUFFER_ASSERT(x) assert(x)

namespace wz::core::containers
{
    /*
================================================================================
BUFFER SEMANTIC CONTRACT (ENGINE LEVEL GUARANTEE)
================================================================================

This file defines the *semantic meaning* of wz::core::containers::Buffer.

It is NOT a dynamic array.
It is NOT an owning container.
It is NOT a memory manager.

It is strictly a logical view over externally-owned storage.

--------------------------------------------------------------------------------
CORE MODEL
--------------------------------------------------------------------------------

Buffer is defined as:

    (T* data, uint32_t count, uint32_t capacity)

Where:

- data      → pointer to externally owned storage
- count     → number of *logically valid elements*
- capacity  → maximum writable elements in storage

--------------------------------------------------------------------------------
INVARIANTS (HARD RULES)
--------------------------------------------------------------------------------

1. count ALWAYS represents logical elements, not memory contents.

2. capacity is immutable after construction.

3. Buffer NEVER owns, allocates, or frees memory.

4. Buffer NEVER inspects memory to infer validity.

5. wrap(data, capacity):
   - ALWAYS creates an EMPTY logical buffer (count = 0)

6. wrap_existing(data, count, capacity):
   - defines logical size explicitly
   - does NOT validate memory contents

7. reset():
   - resets logical size only
   - does NOT modify underlying memory

--------------------------------------------------------------------------------
SAFETY MODEL
--------------------------------------------------------------------------------

- Out-of-bounds protection is enforced via assertions only.
- Overflow attempts increment diagnostics but do not mutate state.
- Invalid usage is undefined behavior unless caught by assertions.

--------------------------------------------------------------------------------
ALGO INTERACTION CONTRACT
--------------------------------------------------------------------------------

All wz::core::algo functions:

- operate ONLY on [0, count)
- must NOT access memory beyond count
- must NOT modify buffer metadata
- must NOT store references beyond call scope
- must remain deterministic for identical input

--------------------------------------------------------------------------------
DESIGN INTENT
--------------------------------------------------------------------------------

This buffer exists to enable:

- low-overhead iteration
- deterministic data pipelines (algo)
- external memory ownership (engine-controlled lifetimes)

It intentionally avoids:

- STL-like ownership semantics
- hidden allocations
- implicit resizing behavior

================================================================================
*/

    /**
     * @brief A lightweight non-owning contiguous buffer view.
     *
     * Buffer is a thin controller over externally-managed storage.
     *
     * It does NOT:
     * - allocate memory
     * - free memory
     * - destroy elements
     * - enforce strict bounds safety beyond assertions and overflow tracking
     *
     * It DOES:
     * - track insertion count
     * - enforce capacity limits for push operations
     * - optionally track overflow attempts
     * - provide a simple sequential write interface
     *
     * @tparam T element type stored in external memory
     */
    template<typename T>
    struct Buffer
    {
    private:
        /**
         * @brief Pointer to externally owned storage.
         *
         * Must point to valid memory when capacity > 0.
         * May be nullptr only when capacity == 0.
         */
        T* data_ = nullptr;

        /**
         * @brief Number of elements currently considered "written".
         *
         * Always satisfies: count <= capacity
         */
        uint32_t count_ = 0;

        /**
         * @brief Total capacity of the external storage.
         */
        uint32_t capacity_ = 0;

        /**
         * @brief Number of times push was attempted beyond capacity.
         *
         * This is a diagnostic counter only and does not affect behavior.
         */
        uint32_t overflow_count_ = 0;

        Buffer() = delete;

        Buffer(T* data, uint32_t count, uint32_t capacity)
            : data_(data), count_(count), capacity_(capacity), overflow_count_(0)
        {
            WZ_BUFFER_ASSERT(capacity_ == 0 || data_ != nullptr);
            WZ_BUFFER_ASSERT(count_ <= capacity_);
        }

    public:

        T* data() noexcept { return data_; }

        const T* data() const noexcept { return data_; }

        uint32_t count() const noexcept { return count_; }

        uint32_t size() const noexcept { return count_; }

        bool empty() const noexcept { return count_ == 0; }

        uint32_t capacity() const noexcept { return capacity_; }

        uint32_t overflow_count() const noexcept { return overflow_count_; }

        /**
         * @brief Resets logical size of the buffer.
         *
         * Does NOT destroy or modify underlying memory.
         * Previously written values remain intact.
         */
        void reset()
        {
            count_ = 0;
        }

        /**
         * @brief Pushes a copy of a value into the buffer.
         *
         * Writes to data[count] and increments count if space is available.
         *
         * @param value value to copy into buffer
         * @return true if insertion succeeded, false if buffer is full
         */
        bool push(const T& value)
        {

            if (count_ >= capacity_)
            {
                overflow_count_++;
                return false;
            }

            data_[count_++] = value;
            return true;
        }

        /**
         * @brief Pushes a moved value into the buffer.
         *
         * Uses move semantics where possible.
         *
         * @param value value to move into buffer
         * @return true if insertion succeeded, false if buffer is full
         */
        bool push(T&& value)
        {
            if (count_ >= capacity_)
            {
                overflow_count_++;
                return false;
            }

            data_[count_++] = std::move(value);
            return true;
        }

        /**
         * @brief Checks if at least n slots are available.
         *
         * @param n number of elements to check space for
         * @return true if buffer has at least n free slots
         *
         * @note count is asserted to always be <= capacity
         */
        bool has_space(uint32_t n = 1) const
        {
            return n <= (capacity_ - count_);
        }

        /**
         * @brief Returns number of remaining writable slots.
         */
        uint32_t remaining() const
        {
            return capacity_ - count_;
        }

        /**
         * @brief Creates a buffer view over raw storage with zero initial count.
         *
         * @param data pointer to external storage
         * @param capacity number of elements in storage
         * @return initialized Buffer view
         */
        static Buffer wrap(T* data, uint32_t capacity)
        {
            WZ_BUFFER_ASSERT(data != nullptr || capacity == 0);
            return Buffer(data, 0, capacity);
        }

        /**
         * @brief Creates a buffer view over pre-filled storage.
         *
         * Used when part of the storage is already valid.
         *
         * @param data pointer to external storage
         * @param count number of already-valid elements
         * @param capacity total capacity of storage
         * @return initialized Buffer view
         */
        static Buffer wrap_existing(T* data, uint32_t count, uint32_t capacity)
        {
            WZ_BUFFER_ASSERT(data != nullptr || capacity == 0);
            WZ_BUFFER_ASSERT(count <= capacity);
            return Buffer(data, count, capacity);
        }

        /**
         * @brief Alias for push(const T&)
         */
        bool try_push(const T& v) { return push(v); }

        /**
         * @brief Alias for push(T&&)
         */
        bool try_push(T&& v) { return push(std::move(v)); }

        bool empty_storage() const noexcept
        {
            return capacity_ == 0;
        }

        T* begin() noexcept { return data_; }
        T* end() noexcept { return data_ + count_; }

        const T* begin() const noexcept { return data_; }
        const T* end() const noexcept { return data_ + count_; }

        Buffer<T> slice(uint32_t start, uint32_t len) const
        {
            WZ_BUFFER_ASSERT(start <= count_);
            WZ_BUFFER_ASSERT(start + len <= count_);

            return Buffer(data_ + start, len, len);
        }

        template<typename F>
        void for_each(F&& fn)
        {
            for (uint32_t i = 0; i < count_; i++)
            {
                fn(data_[i]);
            }
        }

        template<typename F>
        void for_each(F&& fn) const
        {
            for (uint32_t i = 0; i < count_; i++)
            {
                fn(data_[i]);
            }
        }

        /**
         * @brief Transforms elements into output buffer.
         *
         * Applies fn to each element and attempts to push result into out.
         * If out runs out of space, additional results are dropped and
         * overflow_count is incremented.
         */
        template<typename F, typename U>
        void transform_to(Buffer<U>& out, F&& fn) const
        {

            for (uint32_t i = 0; i < count_; i++)
            {
                auto result = fn(data_[i]);
                out.push(std::move(result));
            }
        }

    };
}