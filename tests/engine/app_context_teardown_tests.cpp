// tests/engine/app_context_teardown_tests.cpp
//
// The two-phase engine teardown (engine/app_context.h).
//
// A caller with background lanes cannot use the one-call engine::shutdown():
// lane threads must be joined AFTER destroy_device (the AMD-driver ordering that
// governs every lane in run_project_runtime), so they are necessarily still alive
// at the point the one-call form would already have freed the logger -- and
// wz::Logger's state is a plain heap pointer. The IO lane DRAINS before it joins,
// and wz::fs::async_* callbacks are caller-supplied, so the first one that logs on
// its way out would be a use-after-free reachable only at teardown.
//
// These pin the ordering property that makes it safe: shutdown_subsystems() leaves
// the logger up, and only shutdown_logging() ends it.
//
// Headless by construction: a default-constructed AppContext has no device, no
// window and null asset/gpu pointers, and every step of shutdown_subsystems() is
// gated on those, so it runs its logging and no-ops through without a GPU.

#include <gtest/gtest.h>

#include <engine/app_context.h>
#include <logging/logger.h>

namespace
{
    TEST(AppContextTeardown, ShutdownSubsystemsLeavesTheLoggerUsable)
    {
        wz::engine::AppContext ctx;
        ASSERT_TRUE(wz::logging::init_logger(ctx.logger, {}));
        ASSERT_NE(ctx.logger.state, nullptr);

        wz::engine::shutdown_subsystems(ctx);

        // THE contract: the caller's lane threads are joined after this returns,
        // and they may still log while draining.
        ASSERT_NE(ctx.logger.state, nullptr)
            << "shutdown_subsystems must leave the logger up -- lane threads are "
               "joined after it and the IO lane drains queued jobs as it goes";
        ctx.logger.info("a lane job logging during teardown must be safe");

        wz::engine::shutdown_logging(ctx);
        EXPECT_EQ(ctx.logger.state, nullptr);
    }

    TEST(AppContextTeardown, ShutdownLoggingIsIdempotentAndNullSafe)
    {
        // The teardown guard runs on the unwind path too, where init may never
        // have brought the logger up.
        wz::engine::AppContext never_initialized;
        ASSERT_EQ(never_initialized.logger.state, nullptr);
        wz::engine::shutdown_logging(never_initialized);
        EXPECT_EQ(never_initialized.logger.state, nullptr);

        wz::engine::AppContext ctx;
        ASSERT_TRUE(wz::logging::init_logger(ctx.logger, {}));
        wz::engine::shutdown_logging(ctx);
        EXPECT_EQ(ctx.logger.state, nullptr);
        wz::engine::shutdown_logging(ctx);  // must not double-free
        EXPECT_EQ(ctx.logger.state, nullptr);
    }

    TEST(AppContextTeardown, OneCallShutdownStillEndsTheLogger)
    {
        // The ~15 callers with no lanes of their own (the render device suites)
        // keep the single-call form, and its behaviour is unchanged.
        wz::engine::AppContext ctx;
        ASSERT_TRUE(wz::logging::init_logger(ctx.logger, {}));
        ASSERT_NE(ctx.logger.state, nullptr);

        wz::engine::shutdown(ctx);
        EXPECT_EQ(ctx.logger.state, nullptr);
    }

    TEST(AppContextTeardown, SubsystemsTeardownIsSafeWithNoDeviceOrWindow)
    {
        // The init-failure and unwind paths reach teardown with a half-built
        // context; every step is gated, so this must not fault.
        wz::engine::AppContext ctx;
        ASSERT_TRUE(wz::logging::init_logger(ctx.logger, {}));
        ASSERT_FALSE(ctx.device.valid());
        ASSERT_FALSE(ctx.window.valid());
        ASSERT_EQ(ctx.gpu, nullptr);
        ASSERT_EQ(ctx.assets, nullptr);

        wz::engine::shutdown_subsystems(ctx);
        wz::engine::shutdown_subsystems(ctx);  // idempotent too

        EXPECT_FALSE(ctx.device.valid());
        EXPECT_FALSE(ctx.window.valid());
        wz::engine::shutdown_logging(ctx);
    }
}
