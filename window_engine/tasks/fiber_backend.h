#pragma once

// tasks/fiber_backend.h
//
// The fibre backend seam for #293 S2. A LINK-TIME free-function seam over an
// opaque Fiber handle -- the same idiom as wz::gpu (free functions over
// `struct Device { void* impl; }`, backend chosen by which .cpp CMake compiles),
// so a second platform is a sibling source file, not a runtime vtable. The Win32
// realization lives in platform/win32 (src/platform/win32/src/fiber_win32.cpp),
// per the settled placement: the interface here in tasks/, the OS impl under
// platform/. A future posix backend (ucontext) is a sibling .cpp.
//
// A fibre is a cooperatively-scheduled stack: switching to one suspends the
// current fibre and resumes the target ON THE SAME THREAD, exactly where it last
// switched away. The S2 scheduler uses this so a worker whose task blocks in
// wait() can switch to other work and resume the parked task's stack later.

#include <cstddef>

namespace wz::tasks
{
    // An opaque fibre handle. `impl` is the platform fibre object (on Win32, the
    // fibre address); impl == nullptr is the null handle.
    struct Fiber
    {
        void* impl = nullptr;

        bool valid() const { return impl != nullptr; }
    };

    using FiberEntry = void (*)(void* arg);

    // Turn the CALLING THREAD into a fibre so it can switch_to_fiber. Returns the
    // handle for the thread's current context. Call once when a worker starts;
    // pair with convert_fiber_to_thread before the thread exits.
    Fiber convert_thread_to_fiber();
    void convert_fiber_to_thread();

    // Create a fibre with `stack_size` bytes (0 = a platform default) that runs
    // entry(arg) the first time it is switched to. `entry` MUST NOT return -- it
    // switches away instead; falling off the end of a fibre terminates the thread
    // on Win32.
    Fiber create_fiber(std::size_t stack_size, FiberEntry entry, void* arg);

    // Destroy a fibre from create_fiber. Must not be the currently running fibre.
    void destroy_fiber(Fiber fiber);

    // Suspend the current fibre and resume `target` on this thread.
    void switch_to_fiber(Fiber target);

    // The fibre currently running on this thread.
    Fiber current_fiber();
}
