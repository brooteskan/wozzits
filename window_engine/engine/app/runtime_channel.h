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
        //
        // A THROWING `apply` LOSES THE REST OF THE BATCH. The items were swapped
        // out of pending_ before the loop (they have to be -- the lock is
        // released), so an exception out of apply() unwinds with the untouched
        // TAIL still in the local vector, which is then destroyed. For the edit
        // mailboxes, where every operation must land or be reported dropped,
        // that is silent loss of edits the owner already believes were accepted.
        //
        // The contract is therefore on the CALLER: `apply` must not throw. Every
        // apply today either is noexcept in practice or reports its own failure
        // through record_dropped_edit rather than an exception. Re-queueing the
        // tail instead would be the alternative, but it inverts the ordering
        // guarantee (the tail would land after items posted since the swap), so
        // the requirement is stated rather than papered over.
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

    // Build a Mailbox<T>::SameKey that compares a member of T (the common "same
    // node" key), e.g. Mailbox<CameraEdit>{Replace, coalesce_by(&CameraEdit::
    // node_id)}. A free function rather than a Mailbox constructor on purpose: a
    // `M T::*` parameter in a member of Mailbox<T> is ill-formed the moment T is a
    // non-class type (Mailbox<int>), whereas this template is only instantiated at
    // the call sites, which always pass a real struct member.
    template <class T, class M>
    std::function<bool(const T&, const T&)> coalesce_by(M T::* field)
    {
        return [field](const T& a, const T& b) {
            return a.*field == b.*field;
        };
    }

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
            // The common case: the payload is input only. call_inout moves it in,
            // and the moved-back copy lands in this local and is discarded.
            return call_inout(args);
        }

        // In-out variant, for a payload that is BOTH input and output and must not
        // be left moved-from in the caller (the asset-graph draft): `inout` is
        // moved into the request -- so the caller's object is a husk for the
        // service window, which is exactly what AssetGraphEditorSession's
        // draft-loan detects -- the servicer mutates it in place, and it is moved
        // back before returning: mutated on success, or reclaimed unchanged if the
        // engine stopped / the servicer threw.
        RequestOutcome<R> call_inout(Args& inout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return state_ == State::Idle || abandoned_;
            });
            if (abandoned_) {
                return {};  // never claimed -- inout is untouched, still the caller's
            }

            args_ = std::move(inout);
            state_ = State::Requested;

            // Wake on a published result or a failed (threw) service. On abandon,
            // wake ONLY while the request is still un-claimed (Requested): once it
            // is InFlight the servicer is touching the payload, so the caller must
            // let it finish (Published/Failed) rather than reclaim underneath it.
            cv_.wait(lock, [this] {
                return state_ == State::Published || state_ == State::Failed
                    || (abandoned_ && state_ == State::Requested);
            });

            RequestOutcome<R> out;  // serviced=false unless Published below
            if (state_ == State::Published) {
                out = RequestOutcome<R>{ true, std::move(result_) };
            }
            inout = std::move(args_);  // hand the payload back (mutated or reclaimed)
            state_ = State::Idle;
            cv_.notify_all();  // release the next serialised caller
            return out;
        }

        // Engine thread, once per frame: if a request is pending, run `fn(args_)`
        // OUTSIDE the lock and publish its result. `fn` is passed the payload BY
        // REFERENCE (the caller is parked and does not touch it), so an in-out
        // verb can mutate it in place; by-value verbs simply read it. A no-op when
        // nothing is pending.
        template <class Fn>
        void service(Fn&& fn)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (state_ != State::Requested) {
                    return;
                }
                state_ = State::InFlight;
            }

            // fn runs outside the lock (a bind can take seconds). If it throws,
            // wake the caller as NOT serviced -- never a default-valued "success",
            // since a default R can read as OK (e.g. FileError::None) -- then
            // rethrow so the engine loop logs it exactly as before.
            R result{};
            try {
                result = fn(args_);
            }
            catch (...) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (state_ == State::InFlight) {
                        state_ = State::Failed;
                    }
                }
                cv_.notify_all();
                throw;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                // If abandon() ran while `fn` was in flight the caller is waiting
                // for THIS transition (not the abandon), so the result is still
                // handed over cleanly.
                if (state_ == State::InFlight) {
                    result_ = std::move(result);
                    state_ = State::Published;
                }
            }
            cv_.notify_all();
        }

        // ── Deferred (cross-thread) completion: claim() + publish() ────────────
        // service() runs the servicer and publishes on ONE thread. When the work
        // must finish on ANOTHER thread -- the engine thread claims a save but the
        // blocking disk write runs on the IO lane (#305 step 4b / #300 layer 2) --
        // that pair splits into these two: service(fn) == claim(a) then, once the
        // work is done, publish(fn(a)).
        //
        // For the ASYNC shape only (a by-value verb whose result is produced later),
        // NOT the in-out (call_inout) shape: claim() moves the payload OUT to the
        // servicer so the async closure is self-contained (design doc §5.3 -- an
        // async job borrows no stack frame, it owns its inputs), which means the
        // servicer's mutations do NOT ride args_ back to the caller the way an
        // in-out service() needs. save_ is Request<std::monostate,FileError>, so
        // there is no payload to lose here.

        // Observability: is a request POSTED and not yet claimed?
        //
        // Non-destructive, unlike claim()/service(), which is the whole point:
        // it is the only way to observe that a caller has arrived without also
        // consuming its request. Teardown tests used to sleep ~50ms and hope the
        // caller had parked by then -- and when it had not, abandon() took the
        // "never posted" path instead of the "release a parked caller" one, so
        // the test passed having exercised the weaker case.
        //
        // And it is a PROOF, not a heuristic: call() holds the lock continuously
        // from setting state_ = Requested until cv_.wait atomically releases it,
        // so acquiring the lock here and seeing Requested means the caller has
        // necessarily reached that wait.
        bool pending() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return state_ == State::Requested;
        }

        // Engine thread: if a request is pending, move its payload into `out`,
        // transition to InFlight, and return true; the caller stays parked until a
        // matching publish() (which may run on a different thread). Returns false
        // with `out` untouched when nothing is pending. Once claim() returns true a
        // publish() MUST follow on some thread, or the caller waits forever -- the
        // InFlight state does not wake on abandon() (the servicer owns the payload),
        // exactly as service()'s own InFlight window does not.
        bool claim(Args& out)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_ != State::Requested) {
                return false;
            }
            out = std::move(args_);
            state_ = State::InFlight;
            return true;
        }

        // Any thread: publish the result of a claim()ed request, waking its caller.
        // A no-op unless the request is still InFlight (double publish, or a request
        // that was never claimed, changes nothing). This is service()'s publish tail
        // lifted out so it can run on the thread that finished the work; the
        // abandon()-during-InFlight handover is identical -- an abandon() that landed
        // while the work was in flight still hands this result over cleanly, because
        // the caller is waiting for precisely this InFlight->Published transition.
        void publish(R result)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
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
            Failed,      // `fn` threw; the caller wakes as not-serviced
        };

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        State state_ = State::Idle;
        bool abandoned_ = false;
        Args args_{};
        R result_{};
    };
}
