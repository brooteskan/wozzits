#pragma once

#include <atomic>
#include <utility>
#include "./assert.h"

namespace wz::core::containers
{
    template <typename T>
    class MPSCQueue
    {
    private:
        struct Node
        {
            std::atomic<Node *> next;
            T data;

            Node() : next(nullptr), data() {} // dummy node
            Node(T value) : next(nullptr), data(std::move(value)) {}
        };

        std::atomic<Node *> head;
        std::atomic<Node *> tail;

    public:
        MPSCQueue()
        {
            Node *dummy = new Node();
            head.store(dummy, std::memory_order_relaxed);
            tail.store(dummy, std::memory_order_relaxed);
        }

        ~MPSCQueue()
        {
            T tmp;
            while (try_pop(tmp))
            {
            }

            Node *node = head.load(std::memory_order_relaxed);
            delete node;
        }

        MPSCQueue(const MPSCQueue &) = delete;
        MPSCQueue &operator=(const MPSCQueue &) = delete;

        // Unbounded by design. Unlike the fixed-capacity sibling MPSCRingBuffer
        // -- which returns false when full and so gives the producer
        // back-pressure -- MPSCQueue allocates one node per element and never
        // refuses a push for fullness. It grows until the allocator fails, at
        // which point it fires CONTAINER_ASSERT in debug and returns false in
        // release. The two containers differ on this and their names do not say
        // so: if you need a bound or a drop policy, reach for MPSCRingBuffer.
        bool try_push(T value)
        {
            Node *node = new (std::nothrow) Node(std::move(value));

            CONTAINER_ASSERT(node != nullptr && "Out of memory in MPSCQueue::try_push");

            if (!node)
                return false;

            Node *prev = tail.exchange(node, std::memory_order_acq_rel);
            prev->next.store(node, std::memory_order_release);

            return true;
        }

        void push(T value)
        {
            bool ok = try_push(std::move(value));

            CONTAINER_ASSERT(ok && "MPSCQueue push failed (OOM)");
        }

        bool try_pop(T &out)
        {
            Node *head_node = head.load(std::memory_order_acquire);
            Node *next = head_node->next.load(std::memory_order_acquire);

            if (!next)
                return false;

            out = std::move(next->data);
            head.store(next, std::memory_order_release);
            delete head_node;

            return true;
        }

        bool empty() const
        {
            Node *head_node = head.load(std::memory_order_acquire);
            Node *next = head_node->next.load(std::memory_order_acquire);
            return next == nullptr;
        }

        void clear()
        {
            T tmp;
            while (try_pop(tmp))
            {
            }
        }
    };

} // namespace WZ