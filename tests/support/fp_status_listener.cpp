// tests/support/fp_status_listener.cpp
//
// Reports the IEEE-754 exception flags each test raises.
//
// By default floating point is SILENT: 0.0/0.0 yields NaN, an overflow yields
// inf, and both propagate through arithmetic that looks like it is working. The
// adversarial audit rotation (#320) found that class repeatedly -- a NaN reaching
// a read surface that reports it as a confident answer rather than as an error --
// and in every case the value was manufactured many frames before anything
// noticed. The hardware already records where it happened; nothing was reading it.
//
// This listener clears the accumulated flags before each test and reports them
// after, so a test that manufactures a NaN or an infinity says so by name even
// when its assertions pass.
//
// GATED UNDER ctest, reporting elsewhere. The listener landed reporting-only, on
// the principle that installing a diagnostic and gating on it are separate
// decisions and doing both at once means the gate arrives with a red tree nobody
// can read. That sweep found 32 tests raising hard IEEE exceptions -- among them
// a live culling bug (#326) -- and with all 32 closed the gate went on.
//
// CMake sets WZ_FP_STRICT=1 as a test property (option WZ_FP_STRICT_TESTS, ON),
// so `ctest` is strict and running a test executable DIRECTLY is not -- which is
// what you want while debugging one. This repo has no CI; ctest is the
// authoritative run, so that is where the gate lives.
//
// Three modes, and the third is the one that finds the bug:
//   (bare run)     report the flags a test raised, do not fail it;
//   WZ_FP_STRICT=1 fail the test -- what ctest sets;
//   WZ_FP_TRAP=1   UNMASK the exceptions, so the offending operation faults at
//                  its own instruction instead of being discovered N frames
//                  later. Run one test exe under a debugger with this set and
//                  the stack names the line that manufactured the NaN:
//                    cdb -c "g; kn 20; q" <test>.exe --gtest_filter=Suite.Name
//                  It is per-thread and it terminates the process, which is why
//                  it is opt-in and not the default.
//
// Only INVALID / DIVBYZERO / OVERFLOW are reported. FE_INEXACT fires on almost
// every floating-point operation (0.1 + 0.2 raises it), and underflow/denormal
// fire on ordinary small numbers, so all three are noise at this granularity.
//
// Debug builds only: it is a development diagnostic, and it costs two libc calls
// per test rather than per operation, so the cost is not why -- the reason is
// that a release build should not carry a reporting channel nobody reads.

#include <gtest/gtest.h>

#ifndef NDEBUG

#include <cfenv>
#include <cstdio>
#include <cstdlib>
#if defined(_MSC_VER)
#include <float.h>   // _controlfp_s, _EM_* (the MSVC CRT's trap-mask control)
#endif
#include <string>

namespace
{
    constexpr int kHardExceptions = FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW;

    std::string describe(int raised)
    {
        std::string out;
        if (raised & FE_INVALID)   out += "FE_INVALID ";
        if (raised & FE_DIVBYZERO) out += "FE_DIVBYZERO ";
        if (raised & FE_OVERFLOW)  out += "FE_OVERFLOW ";
        if (!out.empty()) out.pop_back();
        return out;
    }

    bool env_flag(const char* name)
    {
        // Reading getenv per test is pointless -- the answer cannot change
        // inside a run -- so each flag resolves once, on first use.
        const char* v = std::getenv(name);
        return v != nullptr && v[0] == '1';
    }

    bool strict_mode()
    {
        static const bool strict = env_flag("WZ_FP_STRICT");
        return strict;
    }

    bool trap_mode()
    {
        static const bool trap = env_flag("WZ_FP_TRAP");
        return trap;
    }

#if defined(_MSC_VER)
    void set_traps(bool on)
    {
        unsigned previous = 0;
        // Unmask only the three that indicate a value was manufactured, never
        // underflow/denormal/inexact -- those fire on ordinary arithmetic and
        // would fault instantly in correct code.
        constexpr unsigned kMask = _EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW;
        _controlfp_s(&previous, on ? 0u : kMask, kMask);
    }
#else
    void set_traps(bool) {}
#endif

    class FpStatusListener final : public ::testing::EmptyTestEventListener
    {
    public:
        void OnTestStart(const ::testing::TestInfo&) override
        {
            std::feclearexcept(FE_ALL_EXCEPT);
            if (trap_mode()) {
                set_traps(true);
            }
        }

        void OnTestEnd(const ::testing::TestInfo& info) override
        {
            if (trap_mode()) {
                // Fixture teardown and gtest's own reporting run outside the
                // test body; leaving the traps armed across them turns an
                // unrelated ordinary computation into the reported fault.
                set_traps(false);
            }
            const int raised = std::fetestexcept(kHardExceptions);
            if (raised == 0) {
                return;
            }
            const std::string what = describe(raised);
            if (strict_mode()) {
                ADD_FAILURE()
                    << "floating-point exception raised during "
                    << info.test_suite_name() << "." << info.name() << ": " << what
                    << "\nSomething manufactured a NaN or an infinity. If that is"
                       " the point of this test, declare it:"
                       " wz::testing::ExpectFpException{" << what << "}"
                       " (tests/support/fp_expectations.h). To locate the"
                       " operation, run this test under a debugger with"
                       " WZ_FP_TRAP=1 --gtest_catch_exceptions=0 and it will fault"
                       " at its own instruction. To turn the gate off entirely,"
                       " configure with -DWZ_FP_STRICT_TESTS=OFF.";
            } else {
                // Not a gtest failure, so it does not redden the tree; printed on
                // the same stream as the test output so it is attributable.
                std::fprintf(
                    stderr, "[  FP      ] %s.%s raised %s\n",
                    info.test_suite_name(), info.name(), what.c_str());
            }
            std::feclearexcept(FE_ALL_EXCEPT);
        }
    };

    // Registered from a static initializer rather than from a custom main:
    // every test group links GTest::gtest_main, and this TU is compiled straight
    // into each test executable by rs_add_test_group, so there is no static
    // library for the linker to drop it from.
    const bool g_installed = [] {
        ::testing::UnitTest::GetInstance()->listeners().Append(
            new FpStatusListener());
        return true;
    }();
}

#endif  // NDEBUG
