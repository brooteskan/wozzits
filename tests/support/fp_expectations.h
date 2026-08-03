#pragma once

// tests/support/fp_expectations.h
//
// Declares that a scope DELIBERATELY raises an IEEE-754 exception.
//
// fp_status_listener.cpp reports the exception flags each test leaves behind, so
// that a NaN or an infinity manufactured deep inside the engine says so by name.
// A test that feeds hostile input on purpose raises those same flags while doing
// exactly its job -- and the listener cannot tell the two apart. Without a way to
// say which is which, the honest tests are permanent noise in the channel, and a
// channel with permanent noise in it can never become a gate.
//
//     TEST(SceneJsonHostileDocument, TransformThatCannotNarrowToFloatIsRejected)
//     {
//         wz::testing::ExpectFpException expected{ FE_OVERFLOW };
//         const auto result = parse_scene_json(document_with_1e39_translation);
//         EXPECT_FALSE(result.ok);
//     }
//
// It is a DECLARATION, not a suppression: the destructor fails the test if the
// flags it named were never raised. A test that says "this input overflows a
// float" and then does not overflow one has stopped testing what it claims, and
// that is worth a failure rather than silence -- the fix is to delete the
// declaration, which is a one-line edit that also records the behaviour change.

#include <gtest/gtest.h>

#include <cfenv>

namespace wz::testing
{
    class ExpectFpException
    {
    public:
        // `expected` is a bitwise-or of FE_INVALID / FE_DIVBYZERO / FE_OVERFLOW.
        explicit ExpectFpException(int expected) noexcept
            : expected_(expected)
        {
            // Clear only the named flags, so an unrelated exception raised
            // earlier in the test still reaches the listener.
            std::feclearexcept(expected_);
        }

        ~ExpectFpException()
        {
            const int raised = std::fetestexcept(expected_);
            if (raised != expected_) {
                ADD_FAILURE()
                    << "ExpectFpException declared 0x" << std::hex << expected_
                    << " but the scope raised 0x" << raised << std::dec
                    << ".\nThe input this test calls hostile is no longer hostile, "
                       "or the code now rejects it before the arithmetic happens. "
                       "Either is a real change: delete the declaration.";
            }
            std::feclearexcept(expected_);
        }

        ExpectFpException(const ExpectFpException&) = delete;
        ExpectFpException& operator=(const ExpectFpException&) = delete;

    private:
        int expected_;
    };
}
