#include <tasks/fiber_backend.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// Win32 realization of the wz::tasks fibre seam (#293 S2).
//
// FIBER_FLAG_FLOAT_SWITCH is set on both the thread-fibre and every created fibre
// so the x87/SSE floating-point state is saved and restored across a switch --
// without it a fibre could resume with another fibre's rounding/FP state, which
// would corrupt the contraction's arithmetic (and its bit-exactness). The engine
// is x64-only, where the single calling convention makes casting the portable
// void(*)(void*) entry to LPFIBER_START_ROUTINE (a __stdcall type) a no-op.

namespace wz::tasks
{
    Fiber convert_thread_to_fiber()
    {
        return Fiber{ ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH) };
    }

    void convert_fiber_to_thread()
    {
        ConvertFiberToThread();
    }

    Fiber create_fiber(std::size_t stack_size, FiberEntry entry, void* arg)
    {
        void* fiber = CreateFiberEx(
            0,            // initial commit -- grow the stack lazily
            stack_size,   // reserve (0 => system default)
            FIBER_FLAG_FLOAT_SWITCH,
            reinterpret_cast<LPFIBER_START_ROUTINE>(entry),
            arg);
        return Fiber{ fiber };
    }

    void destroy_fiber(Fiber fiber)
    {
        if (fiber.impl != nullptr)
            DeleteFiber(fiber.impl);
    }

    void switch_to_fiber(Fiber target)
    {
        SwitchToFiber(target.impl);
    }

    Fiber current_fiber()
    {
        return Fiber{ GetCurrentFiber() };
    }
}
