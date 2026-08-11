#include <io/io_executor.h>

#include <utility>

// The IO lane (#305 step 4a). Each thread loops: wait for a closure, run it
// OUTSIDE the lock (it blocks on disk), repeat -- until teardown drains the queue
// and stops it. See io_executor.h for why this is dedicated blocking threads with
// a plain mutex queue rather than the lock-free compute pool.

namespace wz::io
{
    namespace
    {
        // Small blocking-I/O group: enough to overlap a stalled write with other
        // I/O, not so many that idle threads pile up (I/O is throughput-bound).
        unsigned resolve_threads(unsigned requested)
        {
            return requested != 0 ? requested : 2u;
        }
    }

    IoExecutor::IoExecutor(unsigned threads)
    {
        const unsigned n = resolve_threads(threads);
        threads_.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            threads_.emplace_back([this] { worker_main(); });
    }

    IoExecutor::~IoExecutor()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            accepting_ = false;  // stop accepting
            running_ = false;    // ...and let the workers leave once drained
        }
        cv_.notify_all();
        for (std::thread& t : threads_)
            t.join();
    }

    bool IoExecutor::post(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!accepting_)
                return false;  // quiesced or tearing down -- refuse (see the header)
            queue_.push_back(std::move(job));
        }
        cv_.notify_one();
        return true;
    }

    void IoExecutor::quiesce()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        accepting_ = false;
        // Wait out the backlog AND whatever is mid-write. in_flight_ is what makes
        // this a real barrier: an empty queue alone would return while a worker is
        // still inside a job, which for the scene save means still holding the file
        // open. running_ stays true, so no worker leaves its loop here.
        idle_cv_.wait(lock, [this] {
            return queue_.empty() && in_flight_ == 0;
        });
    }

    void IoExecutor::worker_main()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
                // Drain-on-teardown: only exit once running_ is false AND the queue
                // is empty, so every posted closure runs before the thread leaves.
                // A quiesce() leaves running_ true, so it parks the thread here
                // rather than retiring it.
                if (queue_.empty())
                    return;
                job = std::move(queue_.front());
                queue_.pop_front();
                ++in_flight_;
            }
            // Runs outside the lock: it blocks on I/O.
            //
            // A THROWING JOB MUST NOT TAKE THE LANE DOWN. Jobs on this lane allocate
            // -- read_file sizes its buffer to the whole file, the save closure
            // builds its document -- so std::bad_alloc is a reachable outcome of
            // ordinary work, not a programming error. Letting it escape terminated
            // the process; swallowing it without the scope guard below would be
            // worse, leaving in_flight_ stuck above zero so quiesce() -- and with it
            // teardown, which waits on quiesce before destroying the device -- hangs
            // forever. So: decrement on EVERY path, then swallow.
            //
            // The job itself still owes its caller an answer; a job that lets an
            // exception out has already failed to deliver one, which is why the
            // async_* wrappers (file/filesystem.cpp) catch internally and report a
            // FileError instead of relying on this net. This is the backstop for
            // everything that does not.
            {
                struct InFlightScope
                {
                    IoExecutor* self;
                    ~InFlightScope()
                    {
                        {
                            std::lock_guard<std::mutex> lock(self->mutex_);
                            --self->in_flight_;
                        }
                        // No job outstanding for this thread now -- a waiting
                        // quiesce() may be the last thing this releases.
                        self->idle_cv_.notify_all();
                    }
                } scope{ this };

                try
                {
                    job();
                }
                catch (...)
                {
                }
                // Drop the closure's captured state (a prepared save document, a
                // read buffer) while still inside the scope guard, so quiesce()
                // returning means that state is released and not merely idle.
                // Reachable on both paths -- the catch above swallows.
                job = nullptr;
            }
        }
    }
}
