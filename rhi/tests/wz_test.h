#pragma once

// Tiny zero-dependency test harness for the wozzits-rhi seed.
//
// Deliberately minimal so the first build works offline with no fetched
// dependency. Each test executable defines main(), runs its cases, and returns
// non-zero if any WZ_CHECK failed. Migrate to GoogleTest (matching
// wozzits-window-engine) once the core stabilises and a vendored/ fetched gtest
// is wired in.

#include <cstdio>

namespace wz::test
{
    inline int& failure_count()
    {
        static int count = 0;
        return count;
    }
}

#define WZ_CHECK(cond)                                                        \
    do {                                                                      \
        if (!(cond)) {                                                        \
            ++::wz::test::failure_count();                                    \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                     \
    } while (0)

#define WZ_CHECK_EQ(a, b) WZ_CHECK((a) == (b))
#define WZ_CHECK_FALSE(cond) WZ_CHECK(!(cond))

#define WZ_RUN(fn)                                                            \
    do {                                                                      \
        std::printf("RUN  %s\n", #fn);                                        \
        fn();                                                                 \
    } while (0)

#define WZ_TEST_RETURN() return ::wz::test::failure_count() == 0 ? 0 : 1
