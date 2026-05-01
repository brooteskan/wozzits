// api/time.h
#pragma once
#include <cstdint>

namespace wz::time
{
    /**
     * @brief Raw tick value returned by the engine's monotonic clock.
     *
     * A Tick represents a value from the platform high-resolution timer
     * (e.g. QueryPerformanceCounter on Windows). Tick values are only
     * meaningful when compared with other ticks from the same clock.
     *
     * Use ticks_per_second() to convert tick differences into seconds.
     */
    using Tick = uint64_t;

    /**
     * @brief Provides access to the engine's monotonic high-resolution clock.
     *
     * TimeSource exposes a platform-independent interface for obtaining
     * high-precision timing information. The underlying implementation
     * typically uses the operating system's performance counter.
     *
     * This clock is:
     * - Monotonic (never moves backwards)
     * - High precision
     * - Not related to wall-clock or calendar time
     *
     * It is intended for:
     * - Frame timing
     * - Profiling
     * - Animation timing
     * - Simulation step calculations
     *
     * It should not be used for real-world timestamps such as logging
     * dates or file times.
     */
    class TimeSource
    {
    public:
        /**
         * @brief Returns the current time in seconds.
         *
         * The value represents seconds elapsed since an unspecified
         * reference point (typically system boot or engine start).
         *
         * The absolute value is not meaningful; only differences between
         * two calls should be used.
         *
         * @return Current monotonic time in seconds.
         */
        static double now();

        /**
         * @brief Returns the current raw tick value of the high-resolution clock.
         *
         * This is the most precise representation of time available from the
         * engine clock. Tick values should be compared or subtracted to measure
         * elapsed time.
         *
         * @return Current tick count.
         */
        static Tick now_ticks();

        /**
         * @brief Returns the number of timer ticks that occur per second.
         *
         * This value is constant for the lifetime of the process and can be
         * used to convert tick differences to seconds.
         *
         * Example:
         * @code
         * auto start = TimeSource::now_ticks();
         * auto end = TimeSource::now_ticks();
         *
         * double elapsed =
         *     double(end - start) / TimeSource::ticks_per_second();
         * @endcode
         *
         * @return Tick frequency of the underlying timer.
         */
        static Tick ticks_per_second();

#ifdef WZ_ENABLE_TESTING
        /**
         * @brief Returns the nominal resolution of std::chrono::steady_clock in nanoseconds.
         *
         * This function exists only for testing and diagnostic purposes.
         * It exposes the compile-time resolution of the standard steady clock
         * used by the test environment.
         *
         * @return Clock resolution in nanoseconds.
         */
        static Tick tick_resolution_ns()
        {
            return 1'000'000'000ull / ticks_per_second();
        }
#endif

    private:
        /**
         * @brief Deleted constructor.
         *
         * TimeSource is a static utility class and cannot be instantiated.
         */
        TimeSource() = delete;
    };
}

namespace wz::time
{
    struct Interval
    {
        Tick start;
        Tick end;

        Tick duration() const
        {
            return end - start;
        }
    };

    struct Frame
    {
        Interval interval;

        uint64_t index; // frame number
        Tick delta_ticks() const 
        {
            return interval.duration();
        }

        double delta_seconds() const
        {
            static const double seconds_per_tick =
                1.0 / double(TimeSource::ticks_per_second());

            return delta_ticks() * seconds_per_tick;
        }
    };
}
