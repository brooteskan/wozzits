#pragma once

#include <atomic>
#include <type_traits>
#include <utility>
#include "./assert.h"

// Test seam. When WZ_MPSC_QUEUE_TEST_HOOKS is defined (only for this container's
// own test target -- see CMakeLists.txt) MPSCQueue exposes two injectable hooks
// that fire inside the two-step publish/consume windows, so a concurrency test
// can park a producer or consumer mid-operation and observe a transient that is
// otherwise unreachable through the public API. When the macro is undefined the
// hooks compile to nothing: no members, no branches, no cost in production.
#ifdef WZ_MPSC_QUEUE_TEST_HOOKS
#include <functional>
#define WZ_MPSC_QUEUE_FIRE(hook) \
    do                           \
    {                            \
        if (hook)                \
            (hook)();            \
    } while (0)
#else
#define WZ_MPSC_QUEUE_FIRE(hook) \
    do                           \
    {                            \
    } while (0)
#endif

namespace wz::core::containers
{
    template <typename T>
    class MPSCQueue
    {
        // T must be default-constructible. The dummy node value-initializes a T
        // (Node()'s data()) and the drain path constructs a temporary T; a
        // non-default-constructible T fails to compile at both sites, with the
        // error pointing at this container rather than at the use. This
        // static_assert says so in one line instead. Wrap a non-default type,
        // e.g. std::optional<T> or a pointer.
        static_assert(
            std::is_default_constructible_v<T>,
            "MPSCQueue<T> requires T to be default-constructible "
            "(the dummy node and the drain temporary value-initialize a T).");

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

#ifdef WZ_MPSC_QUEUE_TEST_HOOKS
    public:
        // Fires in try_push between `tail.exchange` (the node becomes the tail)
        // and `prev->next.store` (the node becomes reachable from head): the
        // window in which a pushed element is queued but invisible to the
        // consumer. A test sets this to hold a producer inside that window.
        std::function<void()> test_hook_push_after_tail_exchange;

        // Fires in try_pop between `head.store` (head advanced off the old node)
        // and `delete head_node` (the old node is freed): the window in which a
        // non-consumer reader of the old head holds a pointer about to dangle.
        std::function<void()> test_hook_pop_after_head_store;
#endif

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
            WZ_MPSC_QUEUE_FIRE(test_hook_push_after_tail_exchange);
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
            WZ_MPSC_QUEUE_FIRE(test_hook_pop_after_head_store);
            delete head_node;

            return true;
        }

        // Compares head and tail; it does NOT dereference either node. Reading
        // head->next (the obvious implementation) dereferences a node the
        // consumer may be concurrently freeing in try_pop -- a use-after-free
        // for any caller that is not the consumer. Comparing the two atomic
        // pointers touches no node, so empty() is safe to call from ANY thread.
        //
        // Consequence, and it is the contract: a push in flight -- tail already
        // advanced by exchange, but prev->next not yet stored -- reads here as
        // NON-empty, even though try_pop cannot yet return that element. So to
        // drain, loop until empty() reports empty; never until try_pop() first
        // returns false, which stops one element short (see the in-flight tests
        // and #313 B4-S1 on the sibling MPSCRingBuffer).
        bool empty() const
        {
            return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
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