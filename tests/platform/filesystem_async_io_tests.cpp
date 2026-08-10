// tests/platform/filesystem_async_io_tests.cpp
//
// async_read_file / async_write_file: the callback-based wrappers around
// read_file / write_file. They were the last untested corner of wz::fs --
// including their no-executor contract, which is the branch that fires in any
// process that never installed an async executor.
//
// Two things are pinned:
//   - With NO executor registered, both helpers fail CLOSED: they invoke the
//     callback synchronously with FileError::IOError rather than dropping the
//     submitted work on the floor.
//   - With an executor, the job runs on it and the callback receives the REAL
//     read_file / write_file result (bytes back on success, NotFound for a
//     missing source). A byte-exact async_write -> async_read round trip proves
//     the payload survives the executor hop.
//
// The test executor runs each posted job on its own std::thread and is joined
// (drain) before results are read, so the cross-thread callback path is
// genuinely exercised without racing. The process-global executor is saved and
// restored around every test (ScopedExecutor): it is global state, and a leaked
// pointer to a destroyed stack executor would poison any later test here.

#include <gtest/gtest.h>

#include <async/async.h>
#include <file/filesystem.h>

#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace fs = wz::fs;

namespace
{
    // Runs every posted job on a fresh worker thread; drain() joins them all, so
    // a test can wait for all outstanding callbacks to have finished.
    struct ThreadExecutor : wz::IAsyncExecutor
    {
        std::mutex m;
        std::vector<std::thread> threads;

        bool post(std::function<void()> job) override
        {
            std::lock_guard<std::mutex> lock(m);
            threads.emplace_back(std::move(job));
            return true;  // this executor never refuses
        }

        void drain()
        {
            std::vector<std::thread> local;
            {
                std::lock_guard<std::mutex> lock(m);
                local.swap(threads);
            }
            for (std::thread &t : local)
                if (t.joinable())
                    t.join();
        }

        ~ThreadExecutor() override { drain(); }
    };

    // Save/restore the process-global executor around a test scope.
    struct ScopedExecutor
    {
        wz::IAsyncExecutor *prev;
        explicit ScopedExecutor(wz::IAsyncExecutor *e)
            : prev(wz::get_async_executor())
        {
            wz::set_async_executor(e);
        }
        ~ScopedExecutor() { wz::set_async_executor(prev); }
    };

    fs::Path scratch(const char *name)
    {
        return fs::join(fs::temp_directory_path(), name);
    }
}

// --- no executor: fail closed ---------------------------------------------

TEST(FilesystemAsyncIO, WithoutExecutorCallsBackWithIOError)
{
    ScopedExecutor guard(nullptr);   // explicitly no executor installed

    bool read_called = false;
    fs::FileError read_err = fs::FileError::None;
    fs::async_read_file(scratch("wz_fs_async_noexec.bin"),
                        [&](fs::FileResult<fs::Buffer> r) {
                            read_called = true;
                            read_err = r.error;
                        });
    EXPECT_TRUE(read_called);
    EXPECT_EQ(read_err, fs::FileError::IOError);

    bool write_called = false;
    fs::FileError write_err = fs::FileError::None;
    fs::async_write_file(scratch("wz_fs_async_noexec.bin"), fs::Buffer{1, 2, 3},
                         [&](fs::FileError e) {
                             write_called = true;
                             write_err = e;
                         });
    EXPECT_TRUE(write_called);
    EXPECT_EQ(write_err, fs::FileError::IOError);
}

// --- with executor: real results across the thread hop --------------------

TEST(FilesystemAsyncIO, RoundTripsThroughExecutorByteExact)
{
    ThreadExecutor exec;
    ScopedExecutor guard(&exec);

    const fs::Path path = scratch("wz_fs_async_roundtrip.bin");
    fs::remove_file(path);

    const fs::Buffer payload = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x7F};

    std::promise<fs::FileError> wrote;
    auto wrote_fut = wrote.get_future();
    fs::async_write_file(path, payload,
                         [&wrote](fs::FileError e) { wrote.set_value(e); });
    exec.drain();
    ASSERT_EQ(wrote_fut.get(), fs::FileError::None);

    std::promise<fs::FileResult<fs::Buffer>> got;
    auto got_fut = got.get_future();
    fs::async_read_file(path, [&got](fs::FileResult<fs::Buffer> r) {
        got.set_value(std::move(r));
    });
    exec.drain();

    const auto result = got_fut.get();
    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value, payload);

    fs::remove_file(path);
}

// A missing source surfaces read_file's NotFound through the async callback,
// rather than a dropped or swallowed error.
TEST(FilesystemAsyncIO, MissingSourceSurfacesNotFound)
{
    ThreadExecutor exec;
    ScopedExecutor guard(&exec);

    std::promise<fs::FileResult<fs::Buffer>> got;
    auto got_fut = got.get_future();
    fs::async_read_file(scratch("wz_fs_async_missing.bin"),
                        [&got](fs::FileResult<fs::Buffer> r) {
                            got.set_value(std::move(r));
                        });
    exec.drain();

    const auto result = got_fut.get();
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error, fs::FileError::NotFound);
}
