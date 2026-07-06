#pragma once
// engine/behavior/fsm2/fsm2_behavior.h
//
// Glue that lets a wozzits behavior plugin drive an HFSM2 hierarchical finite
// state machine while honoring the behavior instance-state contract: state must
// be trivially copyable, is preserved as RAW BYTES across hot-reload and scene
// rebuild, and is NEVER destructed (no RAII members). See wz_instance_state<T>
// in <engine/behavior/behavior_module_api.h>.
//
// HFSM2 (Andrew Gresyk, MIT -- vendored under external/hfsm2/) is a header-only,
// compile-time, template state machine. There is nothing to link and no engine
// or ABI change: any project behavior that wants an FSM just
//     #include <engine/behavior/fsm2/fsm2_behavior.h>
// The library is compiled only in the plugin TUs that include it, so behaviors
// that don't want it pay nothing.
//
// ─────────────────────────────────────────────────────────────────────────────
// THE PERSISTENCE MODEL (why LiveMachine exists)
// ─────────────────────────────────────────────────────────────────────────────
// An HFSM2 `FSM::Instance` is a live C++ object with a non-trivial destructor
// (it fires exit() on teardown), so it CANNOT be a member of a
// wz_instance_state<T> struct -- that static_asserts std::is_trivially_copyable.
//
// So we keep the instance in aligned raw storage inside a trivially-copyable POD
// holder (`LiveMachine<FSM>`), construct it ONCE, keep it alive across frames,
// and refresh its context each dispatch. enter()/exit() then fire only on real
// transitions (changeTo<>()), never per frame -- which a reconstruct-every-frame
// + load() scheme could not achieve (HFSM2's load() re-runs entry reactions).
//
// A scene rebuild may memcpy-relocate the instance-state block (see the note on
// wz_get_instance_state_of). HFSM2 instances are index-based (the active-state
// registry holds Short indices, not self-referential pointers), so they are
// trivially RELOCATABLE: the memcpy'd bytes are still a valid machine. This is
// verified by tests/behavior/fsm2_behavior_tests.cpp (construct -> transition ->
// memcpy to a new address -> keep running, state preserved, no re-enter).
//
// ─────────────────────────────────────────────────────────────────────────────
// REQUIREMENTS on your machine definition
// ─────────────────────────────────────────────────────────────────────────────
//   * Context BY VALUE, as a small POD: a pointer or a struct of pointers --
//     `Config::ContextT<MyCtx>`, NOT `ContextT<MyCtx&>`. The glue overwrites it
//     every dispatch, so make it a transient view onto the current facts / event
//     / your own instance state. A reference-typed context is not relocatable.
//   * State members stay POD (the same rule your wz instance state already
//     follows). No heap-owning members inside states.
//   * Changing the STATE CHART (adding/removing/reordering states) changes the
//     holder's byte layout. Treat it like any instance-state layout change and
//     bump WZ_BEHAVIOR_ABI_VERSION, so a stale preserved block is reset rather
//     than reinterpreted. (New event/react types alone do not change layout.)
//
// ─────────────────────────────────────────────────────────────────────────────
// USAGE SKETCH
// ─────────────────────────────────────────────────────────────────────────────
//   // View onto the current dispatch -- refreshed every call, never persisted.
//   struct Ctx {
//       const WzBehaviorFrameFacts* facts;
//       const WzBehaviorEvent*      event;
//       MyBehaviorState*            self;   // your own POD instance state
//   };
//   using M = hfsm2::MachineT<hfsm2::Config::ContextT<Ctx>>;
//
//   struct Idle; struct Pursue;
//   using FSM = M::PeerRoot<Idle, Pursue>;
//
//   struct Idle : FSM::State {
//       void update(FullControl& c) {
//           if (target_in_range(c.context())) c.changeTo<Pursue>();
//       }
//   };
//   struct Pursue : FSM::State {
//       void enter (Control& c)     { /* start engine sound once */ }
//       void update(FullControl& c) { drive_toward_target(c.context()); }
//   };
//
//   struct MyBehaviorState {                 // <- your wz_instance_state<T>
//       wz::fsm2::LiveMachine<FSM> brain;    // holds the live machine
//       /* ... your other POD fields ... */
//   };
//
//   void on_event(const WzBehaviorFrameFacts* facts,
//                 const WzBehaviorEvent* event, void*) {
//       auto* s = wz_instance_state<MyBehaviorState>(facts);
//       if (!s) return;
//       auto& fsm = wz::fsm2::machine(s->brain, Ctx{ facts, event, s });
//       if (wz_event_kind(event) == WZ_EVENT_FRAME_UPDATE) fsm.update();
//       // else: fsm.react(SomeEvent{ ... });   // for event-driven transitions
//   }

#include <engine/behavior/behavior_module_api.h>

#include <cstdint>
#include <new>          // placement new
#include <type_traits>  // is_trivially_copyable

#include "external/hfsm2/machine.hpp"

namespace wz::fsm2
{
    // A trivially-copyable POD that owns a live HFSM2 machine instance in aligned
    // raw storage. Embed ONE of these in your wz_instance_state<T> struct and
    // access it through machine() below. Zero-initialized on first construction
    // (constructed == 0); the host preserves it as raw bytes thereafter, so the
    // machine survives hot-reload and rebuild-relocation untouched.
    template <typename FSM>
    struct LiveMachine
    {
        using Instance = typename FSM::Instance;

        alignas(Instance) unsigned char storage[sizeof(Instance)];
        uint8_t constructed;

        // The live instance (only valid once machine() has constructed it).
        Instance& get() noexcept
        {
            return *std::launder(reinterpret_cast<Instance*>(&storage[0]));
        }
    };

    // Get the live machine for this holder, with `context` installed for THIS
    // dispatch. First call per node constructs the instance in place (its initial
    // states' enter() fires once); every later call returns the existing instance
    // and refreshes its context view. Drive it with .update() (on frame.update)
    // or .react(event) (for event-driven transitions) at the call site.
    //
    // `context` is taken by value: pass a small POD view (pointers) built fresh
    // each dispatch. It is copied into the machine, so it may be a temporary.
    template <typename FSM, typename Context>
    typename FSM::Instance& machine(LiveMachine<FSM>& holder, Context context)
    {
        static_assert(std::is_trivially_copyable<LiveMachine<FSM>>::value,
            "LiveMachine<FSM> must stay trivially copyable to live in wz "
            "instance state -- keep the machine's Context and states POD");

        using Instance = typename FSM::Instance;
        if (!holder.constructed) {
            ::new (static_cast<void*>(&holder.storage[0])) Instance{ context };
            holder.constructed = 1;
        } else {
            holder.get().context() = context;
        }
        return holder.get();
    }
}
