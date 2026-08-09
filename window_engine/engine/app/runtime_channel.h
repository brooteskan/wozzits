#pragma once

// engine/app/runtime_channel.h
//
// #300 layer 1: the two cross-thread primitives the editor<->engine command
// channel is built from, replacing the ~30 hand-rolled queues/handshakes in
// EditorRuntimeControl. Each verb keeps its own payload struct and policy as
// DATA; only the plumbing (the vectors, the coalesce loops, the hand-written
// futures on a shared condition variable) is shared here.
//
//   Mailbox<T>       fire-and-forget. The owner thread post()s edits; the engine
//                    thread drain()s them once per frame and applies each. An
//                    OnDuplicate policy (matched by a caller-supplied SameKey)
//                    collapses a drag's stream of edits for one node down to the
//                    latest (Replace), drops repeats (Drop), or keeps everything
//                    in order (Keep).
//
//   Request<Args,R>  blocking request/response. The owner thread call()s and
//                    blocks; the engine thread service()s it at a frame-safe
//                    point and publishes the result the caller wakes to take.
//                    A std::promise/future specialised for this channel's
//                    contract, with its OWN mutex+cv so a publish wakes only ITS
//                    caller -- the shared-cv cross-talk that forced the per-verb
//                    *_cycle_busy_ gate (#313 B4-C2) cannot occur here. The
//                    Outcome carries a `serviced` flag distinct from the value,
//                    so "the engine never answered" is never confused with "it
//                    answered with a default-valued result" (#313 D3-P039).
//
// Teardown: a Request that a caller may be blocked in must be abandon()ed before
// the object that owns it is destroyed, so the caller wakes and returns
// {serviced=false} rather than waiting on a freed cv. EditorRuntimeControl drives
// that from begin_close()/mark_finished(), behind the same CallerScope +
// active-caller drain that already guards its hand-rolled handshakes.

#include <condition_variable>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace wz::app
{
    // ── Mailbox<T> ─────────────────────────────────────────────────────────
    // Fire-and-forget queue. Self-contained (its own mutex), so unrelated
    // queues no longer serialise on one shared lock the way the hand-rolled
    // vectors did.
    template <class T>
    class Mailbox
    {
    public:
        enum class OnDuplicate
        {
            Keep,     // append every post, in order (no key needed) -- the default
            Replace,  // coalesce: a matching pending item is overwritten (latest wins)
            Drop,     // dedupe: a matching pending item makes the new post a no-op
        };

        // Returns true if `a` and `b` are "the same" post for coalesce/dedupe
        // (typically same node id). Unused for Keep.
        using SameKey = std::function<bool(const T& a, const T& b)>;

        Mailbox() = default;
        Mailbox(OnDuplicate on_duplicate, SameKey same_key)
            : on_duplicate_(on_duplicate)
            , same_key_(std::move(same_key))
        {
        }

        // Owner thread: enqueue `item`, applying the duplicate policy.
        void post(T item)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (on_duplicate_ != OnDuplicate::Keep && same_key_) {
                for (T& pending : pending_) {
                    if (same_key_(pending, item)) {
                        if (on_duplicate_ == OnDuplicate::Replace) {
                            pending = std::move(item);  // latest wins
                        }
                        return;  // Drop, or replaced in place
                    }
                }
            }
            pending_.push_back(std::move(item));
        }

        // Engine thread: swap the queue out and apply each item OUTSIDE the lock
        // -- an apply mutates the app/renderer, and a post from the owner thread
        // must never block on it.
        template <class Apply>
        void drain(Apply&& apply)
        {
            std::vector<T> items;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (pending_.empty()) {
                    return;
                }
                items.swap(pending_);
            }
            for (const T& item : items) {
                apply(item);
            }
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return pending_.empty();
        }

    private:
        mutable std::mutex mutex_;
        std::vector<T> pending_;
        OnDuplicate on_duplicate_ = OnDuplicate::Keep;
        SameKey same_key_;
    };

    // ── Request<Args,R> ────────────────────────────────────────────────────
    template <class R>
    struct RequestOutcome
    {
        // false => the engine never serviced the request: it was not running
        // when the call arrived, or it stopped while the caller was blocked.
        // Kept distinct from `value` so a serviced call returning a default R is
        // never mistaken for "no answer" (#313 D3-P039).
        bool serviced = false;
        R value{};
    };

    template <class Args, class R>
    class Request
    {
    public:
        // Owner thread: post `args` and block until the engine services the
        // request (or it is abandoned). Serialises callers of THIS request: a
        // second caller waits for the first's cycle to finish before posting, so
        // one request object is only ever in one cycle at a time.
        RequestOutcome<R> call(Args args)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return state_ == State::Idle || abandoned_;
            });
            if (abandoned_) {
                return {};
            }

            args_ = std::move(args);
            state_ = State::Requested;

            cv_.wait(lock, [this] {
                return state_ == State::Published || abandoned_;
            });
            if (state_ != State::Published) {
                return {};  // abandoned before a result was published
            }

            RequestOutcome<R> out{ true, std::move(result_) };
            state_ = State::Idle;
            cv_.notify_all();  // release the next serialised caller
            return out;
        }

        // Engine thread, once per frame: if a request is pending, run `fn(args)`
        // OUTSIDE the lock and publish its result. `fn` takes Args by value (it
        // is moved in) and returns R. A no-op when nothing is pending.
        template <class Fn>
        void service(Fn&& fn)
        {
            Args args;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (state_ != State::Requested) {
                    return;
                }
                args = std::move(args_);
                state_ = State::InFlight;
            }

            R result = fn(std::move(args));

            {
                std::lock_guard<std::mutex> lock(mutex_);
                // If abandon() ran while `fn` was in flight the caller has
                // already left; there is no one to hand the result to.
                if (state_ == State::InFlight) {
                    result_ = std::move(result);
                    state_ = State::Published;
                }
            }
            cv_.notify_all();
        }

        // Teardown: wake a blocked caller so call() returns {serviced=false}.
        // Terminal -- the owning object is about to be destroyed, so there is no
        // "next cycle" to reset for.
        void abandon()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                abandoned_ = true;
            }
            cv_.notify_all();
        }

    private:
        enum class State
        {
            Idle,        // no request in flight
            Requested,   // args posted, awaiting the engine
            InFlight,    // engine claimed it; `fn` is running
            Published,   // result ready for the caller to take
        };

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        State state_ = State::Idle;
        bool abandoned_ = false;
        Args args_{};
        R result_{};
    };
}
