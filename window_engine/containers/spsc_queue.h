// spsc_queeu
#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>

namespace wz::core::containers
{
    static constexpr size_t cache_line_size = 64;

    template <typename T>
    class SPSCQueue
    {
    public:
        explicit SPSCQueue(size_t capacity_pow2)
            : capacity(capacity_pow2),
              mask(capacity_pow2 - 1)
        {
            if ((capacity_pow2 == 0) || (capacity_pow2 & (capacity_pow2 - 1)))
            {
                throw std::runtime_error("SPSCQueue capacity must be power of two");
            }

            buffer = static_cast<T *>(::operator new[](capacity * sizeof(T)));
        }

        ~SPSCQueue()
        {
            ::operator delete[](buffer);
        }

        SPSCQueue(const SPSCQueue &) = delete;
        SPSCQueue &operator=(const SPSCQueue &) = delete;

        bool push(T value)
        {
            const size_t tail = write_index.load(std::memory_order_relaxed);
            const size_t next_tail = (tail + 1) & mask;

            if (next_tail == read_index.load(std::memory_order_acquire))
                return false;

            new (&buffer[tail]) T(std::move(value));

            write_index.store(next_tail, std::memory_order_release);
            return true;
        }

        bool try_pop(T &out)
        {
            const size_t head = read_index.load(std::memory_order_relaxed);

            if (head == write_index.load(std::memory_order_acquire))
                return false;

            out = std::move(buffer[head]);
            buffer[head].~T();

            read_index.store((head + 1) & mask, std::memory_order_release);
            return true;
        }

        bool empty() const
        {
            return read_index.load(std::memory_order_acquire) ==
                   write_index.load(std::memory_order_acquire);
        }

        void clear()
        {
            read_index.store(0, std::memory_order_relaxed);
            write_index.store(0, std::memory_order_relaxed);
        }

    private:
        const size_t capacity;
        const size_t mask;

        alignas(cache_line_size) std::atomic<size_t> read_index{0};
        char pad1[cache_line_size - sizeof(std::atomic<size_t>)];

        alignas(cache_line_size) std::atomic<size_t> write_index{0};
        char pad2[cache_line_size - sizeof(std::atomic<size_t>)];

        T *buffer = nullptr;
    };
}