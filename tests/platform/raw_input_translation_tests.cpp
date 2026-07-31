// tests/platform/raw_input_translation_tests.cpp
//
// The FIRST test of src/platform/win32 (issue #313, B4-T4: the platform layer
// had zero tests, which is why B4-C6 and B4-C15 lived there undetected).
//
// ri_translate_mouse is the packet -> events step, split out of
// ri_process_input precisely so it can be driven without a live HRAWINPUT.
// Everything here builds a RAWMOUSE by hand, exactly as a HID report would
// arrive, and asserts what comes out.
//
// The property under test: ONE packet can carry SEVERAL independent
// transitions, and every one of them must be emitted. The old if/else-if chain
// emitted at most one, so a button edge sharing a report with motion was
// discarded, and a report carrying two button edges kept only the first.
// Because input.cpp latches mouse.down and clears it only on a RECEIVED up, a
// single dropped UP sticks that button down permanently -- so these are not
// cosmetic drops.

#include <gtest/gtest.h>

#include <platform/win32/ri_win32.h>

#include <cstddef>
#include <vector>

namespace
{
    using wz::event::Event;
    using wz::platform::win32::kRawMouseMaxEvents;
    using wz::platform::win32::ri_translate_mouse;

    std::vector<Event> translate(const RAWMOUSE& m)
    {
        Event buffer[kRawMouseMaxEvents];
        const std::size_t count =
            ri_translate_mouse(m, buffer, kRawMouseMaxEvents);
        return std::vector<Event>(buffer, buffer + count);
    }

    RAWMOUSE motion(LONG dx, LONG dy)
    {
        RAWMOUSE m{};
        m.lLastX = dx;
        m.lLastY = dy;
        return m;
    }

    const Event* find(
        const std::vector<Event>& events,
        Event::Type type,
        int occurrence = 0)
    {
        int seen = 0;
        for (const Event& e : events) {
            if (e.type != type) {
                continue;
            }
            if (seen++ == occurrence) {
                return &e;
            }
        }
        return nullptr;
    }
}

TEST(RawInputTranslation, MotionAloneProducesOneMoveEvent)
{
    const auto events = translate(motion(7, -3));

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, Event::Type::MouseMove);
    EXPECT_EQ(events[0].mouse_move.dx, 7);
    EXPECT_EQ(events[0].mouse_move.dy, -3);
}

TEST(RawInputTranslation, AButtonEdgeSharingAPacketWithMotionIsNotDiscarded)
{
    // B4-C6. A real mouse reports movement and a button transition in the SAME
    // HID packet routinely -- releasing during a drag is the everyday case.
    RAWMOUSE m = motion(4, 4);
    m.usButtonFlags = RI_MOUSE_LEFT_BUTTON_UP;

    const auto events = translate(m);

    ASSERT_EQ(events.size(), 2u) << "motion must not shadow the button edge";
    const Event* move = find(events, Event::Type::MouseMove);
    const Event* button = find(events, Event::Type::MouseButton);
    ASSERT_NE(move, nullptr);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(move->mouse_move.dx, 4);
    EXPECT_EQ(button->mouse_button.button, 0u);
    EXPECT_FALSE(button->mouse_button.pressed)
        << "the UP is the half that matters: input.cpp clears its latch only on "
           "a received UP, so dropping this sticks the button down forever";
}

TEST(RawInputTranslation, AWheelNotchSharingAPacketWithMotionIsNotDiscarded)
{
    RAWMOUSE m = motion(1, 1);
    m.usButtonFlags = RI_MOUSE_WHEEL;
    m.usButtonData = static_cast<USHORT>(120);

    const auto events = translate(m);

    ASSERT_EQ(events.size(), 2u);
    const Event* wheel = find(events, Event::Type::MouseWheel);
    ASSERT_NE(wheel, nullptr);
    EXPECT_EQ(wheel->mouse_wheel.delta, 120);
}

TEST(RawInputTranslation, TwoButtonEdgesInOnePacketBothSurvive)
{
    // B4-C15. The flags genuinely OR together; the author's own comment said so
    // ("may OR together in same packet, but still ONE event") while the code
    // kept only the first.
    RAWMOUSE m{};
    m.usButtonFlags = RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_DOWN;

    const auto events = translate(m);

    ASSERT_EQ(events.size(), 2u);
    const Event* left = find(events, Event::Type::MouseButton, 0);
    const Event* right = find(events, Event::Type::MouseButton, 1);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->mouse_button.button, 0u);
    EXPECT_FALSE(left->mouse_button.pressed);
    EXPECT_EQ(right->mouse_button.button, 1u);
    EXPECT_TRUE(right->mouse_button.pressed);
}

TEST(RawInputTranslation, ADownAndUpOfTheSameButtonInOnePacketBothSurvive)
{
    // The sharpest case, and it needs no mouse motion at all: a click faster
    // than one report interval sets DOWN and UP in the same packet. The old
    // chain took DOWN and dropped UP -- button latched down forever.
    RAWMOUSE m{};
    m.usButtonFlags = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP;

    const auto events = translate(m);

    ASSERT_EQ(events.size(), 2u)
        << "a fast click must not be reduced to a press with no release";
    const Event* down = find(events, Event::Type::MouseButton, 0);
    const Event* up = find(events, Event::Type::MouseButton, 1);
    ASSERT_NE(down, nullptr);
    ASSERT_NE(up, nullptr);
    EXPECT_TRUE(down->mouse_button.pressed);
    EXPECT_FALSE(up->mouse_button.pressed);
    EXPECT_EQ(down->mouse_button.button, up->mouse_button.button);
}

TEST(RawInputTranslation, EverythingAtOnceProducesEveryTransition)
{
    RAWMOUSE m = motion(-2, 9);
    m.usButtonFlags = RI_MOUSE_WHEEL
        | RI_MOUSE_LEFT_BUTTON_DOWN
        | RI_MOUSE_RIGHT_BUTTON_UP;
    m.usButtonData = static_cast<USHORT>(-120);

    const auto events = translate(m);

    ASSERT_EQ(events.size(), 4u);
    EXPECT_NE(find(events, Event::Type::MouseMove), nullptr);
    EXPECT_NE(find(events, Event::Type::MouseWheel), nullptr);
    ASSERT_NE(find(events, Event::Type::MouseButton, 0), nullptr);
    ASSERT_NE(find(events, Event::Type::MouseButton, 1), nullptr);
    EXPECT_EQ(find(events, Event::Type::MouseWheel)->mouse_wheel.delta, -120);
}

TEST(RawInputTranslation, AnEmptyPacketProducesNothing)
{
    EXPECT_TRUE(translate(RAWMOUSE{}).empty());
}

TEST(RawInputTranslation, CapacityIsNeverExceededAndNeverTruncatesARealPacket)
{
    // kRawMouseMaxEvents must cover the worst real packet, or the fix trades a
    // silent drop in the chain for a silent drop at the buffer edge.
    RAWMOUSE m = motion(1, 1);
    m.usButtonFlags = RI_MOUSE_WHEEL
        | RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP
        | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP;
    EXPECT_EQ(translate(m).size(), kRawMouseMaxEvents);

    // A smaller buffer must stop at capacity rather than run off the end.
    // NB: `small` is a windows.h macro (rpcndr.h defines it as char).
    wz::event::Event two_slots[2];
    EXPECT_EQ(ri_translate_mouse(m, two_slots, 2), 2u);
    EXPECT_EQ(ri_translate_mouse(m, nullptr, 4), 0u);
    EXPECT_EQ(ri_translate_mouse(m, two_slots, 0), 0u);
}
