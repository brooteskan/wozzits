#pragma once
#include <atomic>
#include <cstddef>
#include <utility>

// TODO: snapshot lock ?

namespace wz::core::containers
{
    template <typename T, size_t Capacity>
    class MPSCRingBuffer
    {
        static_assert(Capacity > 1, "RingBuffer capacity must be > 1");

        enum class State : uint8_t
        {
            Empty = 0,
            Ready = 1
        };

        struct Cell
        {
            std::atomic<State> state{State::Empty};
            T value;
        };

    public:
        bool try_push(T value)
        {
            for (;;)
            {
                size_t h = head.load(std::memory_order_acquire);
                size_t t = tail.load(std::memory_order_relaxed);

                if (t - h >= Capacity)
                    return false; // full

                if (tail.compare_exchange_weak(
                        t,
                        t + 1,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed))
                {
                    Cell &cell = buffer[t % Capacity];

                    // IMPORTANT: write first, then publish
                    cell.value = std::move(value);

                    cell.state.store(State::Ready, std::memory_order_release);

                    return true;
                }
            }
        }

        bool try_pop(T &out)
        {
            size_t h = head.load(std::memory_order_relaxed);

            Cell &cell = buffer[h % Capacity];

            if (cell.state.load(std::memory_order_acquire) != State::Ready)
                return false;

            out = std::move(cell.value);

            cell.state.store(State::Empty, std::memory_order_release);

            head.store(h + 1, std::memory_order_release);

            return true;
        }

        bool empty() const
        {
            size_t h = head.load(std::memory_order_acquire);
            size_t t = tail.load(std::memory_order_acquire);
            return h == t;
        }

        void clear()
        {
            head.store(0, std::memory_order_relaxed);
            tail.store(0, std::memory_order_relaxed);

            for (size_t i = 0; i < Capacity; ++i)
                buffer[i].state.store(State::Empty, std::memory_order_relaxed);
        }

    private:
        alignas(64) std::atomic<size_t> head{0};
        alignas(64) std::atomic<size_t> tail{0};

        Cell buffer[Capacity];
    };
}