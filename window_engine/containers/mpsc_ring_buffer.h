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

        // Pop the head cell if its producer has PUBLISHED it.
        //
        // FALSE DOES NOT MEAN EMPTY. try_push claims a slot (the tail CAS) and
        // only then writes the value and stores Ready, so a producer that has
        // claimed the head slot but not yet published leaves a transient HOLE:
        // this returns false while later cells are already Ready and waiting
        // behind it. That is correct -- the ring is ordered, so those cells
        // cannot be handed out early -- but it means a `while (try_pop(...))`
        // FINAL drain stops at the hole and abandons everything behind it.
        // A drain that must not lose messages has to re-check empty() and retry;
        // see LoggerState::run's shutdown drain.
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

        // Reset to empty. NOT concurrency-safe -- every store is relaxed and the
        // cells are stamped Empty without regard for a producer mid-push, which
        // would then publish Ready into a slot this just reclaimed. Call only
        // when every producer and the consumer are known idle.
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