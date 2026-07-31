#pragma once

#include <windows.h>

#include <event/event.h>

#include <cstddef>
#include <cstdint>

namespace wz::platform::win32
{
    bool ri_init(HINSTANCE hInstance);
    void ri_shutdown();

    void ri_process_input(LPARAM lParam);

    // ONE RAWMOUSE packet can carry SEVERAL independent transitions at once —
    // motion, a wheel notch, and one or more button edges all ride the same HID
    // report. Translating it therefore yields 0..N events, not one.
    //
    // This used to be an if / else-if chain that emitted at most a single event
    // per packet, so a button edge sharing a report with motion was discarded,
    // and a report carrying two button edges kept only the first. Because the
    // consumer (input.cpp) treats mouse.down as a LATCH cleared only by a
    // received UP, one dropped UP leaves that button stuck down permanently.
    // Issue #313, B4-C6 and B4-C15.
    //
    // Split out of ri_process_input so it can be tested without a real
    // HRAWINPUT: GetRawInputData needs a live message, this needs only bytes.
    //
    // Writes up to `capacity` events into `out` and returns how many it
    // produced. kRawMouseMaxEvents is the most one packet can generate, so a
    // buffer of that size never truncates.
    inline constexpr std::size_t kRawMouseMaxEvents = 6;

    std::size_t ri_translate_mouse(
        const RAWMOUSE& mouse,
        wz::event::Event* out,
        std::size_t capacity);

    // Raw-input transitions lost to a full event queue since process start
    // (#313, B4-S4 -- the drop hook here used to be commented out). Non-zero
    // means input was silently discarded, which for a button UP means a latch
    // stuck down in input.cpp.
    uint64_t ri_dropped_input_events();
}