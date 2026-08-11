#pragma once

// containers/work_stealing_deque.h
//
// A lock-free work-stealing deque (#293 S1; lock-free upgrade #304). The OWNER
// thread pushes and pops at the BACK (LIFO -- newest task first: better locality,
// and it bounds how deep the live fork-join gets); THIEF threads steal from the
// FRONT (FIFO -- the oldest task, usually the root of a large subtree, so a steal
// hands off a big chunk and rarely contends with the owner's end).
//
// This is the lock-free Chase-Lev deque (Chase & Lev 2005; the C++11 memory
// orderings follow Le, Pop, Cohen & Jagannathan 2013, "Correct and Efficient
// Work-Stealing for Weak Memory Models"): a growable circular array with atomic
// `top`/`bottom` indices. The owner's push/pop touch only `bottom` and one slot in
// the common case -- no lock, and no contention with thieves except on the last
// element. Thieves claim the front with a single CAS on `top`; a lost CAS aborts
// (the caller retries elsewhere). This replaces the earlier mutex-guarded first cut,
// whose single lock serialized the owner's own push/pop against every thief.
//
// OWNER-ONLY push/pop. Unlike the mutex version, push() and pop() may be called
// ONLY by the deque's owner thread; steal() is the sole cross-thread entry. The
// task pool honors this: a worker pushes to its own deque, and cross-thread submits
// go through the pool's MPSC injection queue rather than push() (#304).
//
// ABA-free by construction: `top`/`bottom` are 64-bit indices that only ever
// increase, so the CAS on `top` never observes a recycled index value -- there is
// no A-B-A for it to confuse for "unchanged".
//
// MEMORY: growth allocates a larger array and copies the live [top, bottom) range;
// a concurrent thief may still hold a pointer into the OLD array, so old arrays are
// RETAINED (not freed) until the deque is destroyed. Growth doubles, so at most
// O(log capacity) arrays are ever retained -- negligible. That retained set is
// mutated only by the owner (in push/grow) and read only by the destructor, which
// runs with no work in flight; a thief never touches it.
//
// SLOT ATOMICITY: slots are std::atomic<T>, accessed relaxed. The owner's slot
// write in push() can race a stale thief still reading that same PHYSICAL slot for
// an index the circular buffer has since recycled; the thief's following `top` CAS
// fails and it discards the value, but the concurrent read/write of the slot itself
// must be well-defined, hence atomic slots. For a T wider than the platform's
// lock-free atomic width this uses the standard library's per-address lock pool for
// the slot access ONLY -- the top/bottom/CAS work-stealing protocol stays lock-free,
// and since the owner and a thief touch different physical slots except on the last
// element, they do not contend there.
//
// Domain-agnostic, like the SPSC/MPSC queues beside it: the pool instantiates it
// with its own Task type; the container knows nothing about tasks.

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

// Test seam, mirroring mpsc_queue.h. When WZ_WORK_STEALING_DEQUE_TEST_HOOKS is
// defined (only for this container's own test target -- see CMakeLists.txt) steal()
// exposes an injectable hook that fires inside the read-then-CAS window, so a
// concurrency test can park a thief mid-steal and construct the last-element /
// recycled-index race deterministically. Undefined (production) it compiles to
// nothing: no member, no branch, no cost.
#ifdef WZ_WORK_STEALING_DEQUE_TEST_HOOKS
#include <functional>
#define WZ_WSD_FIRE(hook) \
    do                    \
    {                     \
        if (hook)         \
            (hook)();     \
    } while (0)
#else
#define WZ_WSD_FIRE(hook) \
    do                    \
    {                     \
    } while (0)
#endif

namespace wz::core::containers
{
    template <typename T>
    class WorkStealingDeque
    {
        // Slots are std::atomic<T> (see the SLOT ATOMICITY note), which requires T
        // to be trivially copyable. Task and the test's int both qualify.
        static_assert(
            std::is_trivially_copyable_v<T>,
            "WorkStealingDeque<T> stores slots as std::atomic<T>, which requires T "
            "to be trivially copyable.");

    public:
        WorkStealingDeque()
        {
            Array* a = new Array(kInitialCapacity);
            arrays_.emplace_back(a);
            array_.store(a, std::memory_order_relaxed);
        }

        WorkStealingDeque(const WorkStealingDeque&) = delete;
        WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

        // Owner thread: push a task onto the back.
        void push(T value)
        {
            const std::int64_t b = bottom_.load(std::memory_order_relaxed);
            const std::int64_t t = top_.load(std::memory_order_acquire);
            Array* a = array_.load(std::memory_order_relaxed);
            if (b - t > a->capacity - 1)  // would overflow the ring: grow first
                a = grow(a, t, b);
            a->put(b, value);
            std::atomic_thread_fence(std::memory_order_release);
            bottom_.store(b + 1, std::memory_order_relaxed);
        }

        // Owner thread: take the most-recently-pushed task (LIFO). nullopt if empty.
        std::optional<T> pop()
        {
            const std::int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
            Array* a = array_.load(std::memory_order_relaxed);
            bottom_.store(b, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            std::int64_t t = top_.load(std::memory_order_relaxed);

            if (t > b)
            {
                // Empty: restore bottom to the canonical empty state (bottom == top).
                bottom_.store(b + 1, std::memory_order_relaxed);
                return std::nullopt;
            }

            const T value = a->get(b);
            if (t != b)
                return value;  // more than one element: no thief can be at slot b

            // Exactly one element (t == b): race the thieves for it on `top`.
            std::optional<T> result = std::nullopt;
            if (top_.compare_exchange_strong(
                    t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
                result = value;  // won the last element
            // else: a thief took it -- result stays nullopt.
            bottom_.store(b + 1, std::memory_order_relaxed);
            return result;
        }

        // Thief thread: take the oldest task (FIFO), from the front. nullopt if the
        // deque is empty OR the claim lost a race -- the caller simply retries
        // elsewhere, so the two cases need not be distinguished here.
        std::optional<T> steal()
        {
            std::int64_t t = top_.load(std::memory_order_acquire);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            const std::int64_t b = bottom_.load(std::memory_order_acquire);

            if (t >= b)
                return std::nullopt;  // empty

            // Non-empty. Read the slot BEFORE the CAS. Load the array with acquire so
            // a grow the owner published is fully visible; even reading a stale
            // (pre-grow) array pointer is safe -- the value at index t was copied into
            // the new array, and old arrays are retained until destruction.
            Array* a = array_.load(std::memory_order_acquire);
            // Parked here, a thief holds a possibly-STALE array pointer and has not
            // read from it yet -- the only window from which the "owner grows
            // underneath an in-flight steal" case can be built. The hook below
            // fires after the read, which is already past it.
            WZ_WSD_FIRE(test_hook_steal_after_array_load);
            const T value = a->get(t);
            WZ_WSD_FIRE(test_hook_steal_after_read);
            if (top_.compare_exchange_strong(
                    t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
                return value;
            return std::nullopt;  // lost the race; abort
        }

        // Approximate, and safe to call from ANY thread. Reports empty when
        // bottom <= top. It may momentarily disagree with a concurrent push/steal --
        // callers use it as a hint (the pool re-checks under its wake lock before
        // parking), never as a linearizable count.
        bool empty() const
        {
            const std::int64_t b = bottom_.load(std::memory_order_relaxed);
            const std::int64_t t = top_.load(std::memory_order_relaxed);
            return b <= t;
        }

#ifdef WZ_WORK_STEALING_DEQUE_TEST_HOOKS
        // Fires in steal() between reading the slot value and the `top` CAS -- the
        // window a test parks a thief in to build the last-element / recycled-index
        // race deterministically. Compiles to nothing when the macro is off.
        std::function<void()> test_hook_steal_after_read;

        // Fires EARLIER: between loading the array pointer and reading the slot
        // out of it. That is where a thief holds a stale array while the owner
        // grows, which is the case the "old arrays are retained until destruction"
        // rule exists for -- and which the hook above fires too late to construct.
        std::function<void()> test_hook_steal_after_array_load;
#endif

    private:
        // A circular buffer of `capacity` slots (a power of two, so `index & mask`
        // maps a monotonic 64-bit index onto a slot). Slots are atomic; see the
        // SLOT ATOMICITY note in the header comment.
        struct Array
        {
            explicit Array(std::int64_t cap)
                : capacity(cap), mask(cap - 1), slots(new std::atomic<T>[cap])
            {
            }
            ~Array() { delete[] slots; }

            Array(const Array&) = delete;
            Array& operator=(const Array&) = delete;

            T get(std::int64_t i) const
            {
                return slots[i & mask].load(std::memory_order_relaxed);
            }
            void put(std::int64_t i, T v)
            {
                slots[i & mask].store(v, std::memory_order_relaxed);
            }

            std::int64_t capacity;
            std::int64_t mask;
            std::atomic<T>* slots;
        };

        // Full ring: allocate a doubled array, copy the live [t, b) range preserving
        // each element's slot (i & new_mask), publish it (release, so a thief's
        // acquire load sees the copies), and retain the old array -- a thief may
        // still be reading it. Owner-only.
        Array* grow(Array* old, std::int64_t t, std::int64_t b)
        {
            Array* a = new Array(old->capacity * 2);
            for (std::int64_t i = t; i < b; ++i)
                a->put(i, old->get(i));
            arrays_.emplace_back(a);
            array_.store(a, std::memory_order_release);
            return a;
        }

        static constexpr std::int64_t kInitialCapacity = 256;

        std::atomic<std::int64_t> top_{ 0 };
        std::atomic<std::int64_t> bottom_{ 0 };
        std::atomic<Array*> array_{ nullptr };

        // Every array ever allocated (the current one plus every retired one), owned
        // here and freed only at destruction. Owner-mutated in push/grow and read
        // only by the destructor; a thief never touches it.
        std::vector<std::unique_ptr<Array>> arrays_;
    };
}

#undef WZ_WSD_FIRE
