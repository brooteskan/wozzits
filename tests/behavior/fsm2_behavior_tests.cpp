// tests/behavior/fsm2_behavior_tests.cpp
//
// Covers the HFSM2 glue in <engine/behavior/fsm2/fsm2_behavior.h>: that a live
// machine can live in a trivially-copyable behavior instance-state block,
// construct once, refresh its context per dispatch, and -- the load-bearing
// claim -- survive a raw-byte relocation (as a scene rebuild performs) with its
// active state intact and no spurious enter(). Doubles as the canonical usage
// example.

#include <engine/behavior/fsm2/fsm2_behavior.h>

#include <gtest/gtest.h>

#include <type_traits>

namespace
{
    struct Counters {
        int enter_idle = 0, enter_pursue = 0;
        int upd_idle   = 0, upd_pursue   = 0;
    };

    // Transient per-dispatch view (POD, by value) -- the context is refreshed on
    // every machine() call, never persisted.
    struct Ctx { Counters* c; };

    using M = hfsm2::MachineT<hfsm2::Config::ContextT<Ctx>>;

    struct Idle; struct Pursue;
    using FSM = M::PeerRoot<Idle, Pursue>;

    struct Idle : FSM::State {
        void enter (Control& c)     { c.context().c->enter_idle++; }
        void update(FullControl& c) { c.context().c->upd_idle++; c.changeTo<Pursue>(); }
    };
    struct Pursue : FSM::State {
        void enter (Control& c)     { c.context().c->enter_pursue++; }
        void update(FullControl& c) { c.context().c->upd_pursue++; }
    };

    // The wz behavior instance state that embeds the machine.
    struct BehState {
        wz::fsm2::LiveMachine<FSM> brain;
        int other = 0;
    };
}

TEST(Fsm2Behavior, HolderStaysTriviallyCopyable)
{
    // The whole point: a machine-bearing state block still satisfies the
    // wz_instance_state<T> contract.
    EXPECT_TRUE(std::is_trivially_copyable<wz::fsm2::LiveMachine<FSM>>::value);
    EXPECT_TRUE(std::is_trivially_copyable<BehState>::value);
}

TEST(Fsm2Behavior, ConstructsOnceAndDrivesTransitions)
{
    BehState s{};
    Counters ctr{};

    // First dispatch: constructs the machine (enter Idle), Idle.update() runs and
    // requests changeTo<Pursue> (exit Idle, enter Pursue). Pursue.update() does
    // NOT run this frame -- the transition is applied after update().
    wz::fsm2::machine(s.brain, Ctx{ &ctr }).update();
    EXPECT_EQ(s.brain.constructed, 1u);
    EXPECT_EQ(ctr.enter_idle,   1);
    EXPECT_EQ(ctr.enter_pursue, 1);
    EXPECT_EQ(ctr.upd_idle,     1);
    EXPECT_EQ(ctr.upd_pursue,   0);

    // Second dispatch: Pursue is active, Pursue.update() runs. No re-construction,
    // no re-entry.
    wz::fsm2::machine(s.brain, Ctx{ &ctr }).update();
    EXPECT_EQ(ctr.enter_idle,   1);
    EXPECT_EQ(ctr.enter_pursue, 1);
    EXPECT_EQ(ctr.upd_pursue,   1);
}

TEST(Fsm2Behavior, SurvivesBlockRelocation)
{
    BehState s{};
    Counters ctr{};
    wz::fsm2::machine(s.brain, Ctx{ &ctr }).update();  // -> Pursue
    wz::fsm2::machine(s.brain, Ctx{ &ctr }).update();  // Pursue.update -> upd_pursue = 1
    ASSERT_EQ(ctr.upd_pursue, 1);
    const int enters_before = ctr.enter_pursue;

    // A scene rebuild may memcpy the instance-state block to a new address. Model
    // that with a raw copy of the trivially-copyable state, then keep running the
    // COPY: the active state (Pursue) must be preserved and enter() must NOT fire.
    BehState relocated = s;
    wz::fsm2::machine(relocated.brain, Ctx{ &ctr }).update();

    EXPECT_EQ(relocated.brain.constructed, 1u);
    EXPECT_EQ(ctr.upd_pursue, 2);                 // Pursue kept running after the move
    EXPECT_EQ(ctr.enter_pursue, enters_before);   // no spurious re-enter on relocation
}

TEST(Fsm2Behavior, RefreshesContextEachDispatch)
{
    BehState s{};
    Counters first{}, second{};

    wz::fsm2::machine(s.brain, Ctx{ &first }).update();   // uses `first` (-> Pursue)
    wz::fsm2::machine(s.brain, Ctx{ &second }).update();  // context refreshed to `second`

    EXPECT_EQ(second.upd_pursue, 1);   // the later dispatch saw the new context
    EXPECT_EQ(first.upd_pursue,  0);   // and not the stale one
}
