#pragma once
// behavior/tank_lifecycle.h
//
// Shared tank life/death lifecycle as an HFSM2 machine (header-only, like
// tank_drive.h / cannon_fire.h). A tank owns a tank_lifecycle::State, calls
// init() once (with its "hitbox" child + max health), and tick() every frame.
// The machine captures the spawn point on the first readable frame, polls the
// hitbox's cumulative damage, and at max_health RESPAWNS the tank -- teleport
// back to spawn + clear the tally -- then continues:
//
//   Alive --(damage >= max_health)--> Dead --(respawn)--> Alive
//
// The tank is NOT removed on death (its camera / children would go with it). Only
// the player wires this today; an enemy can adopt it to gain death + respawn (it
// has none now). Dead is transient (respawns on entry, returns to Alive next
// tick) but is a REAL state, so a respawn delay / death effect can be added later
// -- softening the instant teleport -- without touching the poll logic.
//
// Persistence mirrors cannon_fire: the live machine + captured spawn ride in the
// tank's instance state and survive rebuild relocation. init() preserves the
// captured spawn + machine across reload (only (re)binds the hitbox + health),
// matching the old inline state that was never reset in tank_init.

#include <engine/behavior/behavior_module_api.h>
#include <engine/behavior/fsm2/fsm2_behavior.h>

#include "tank_damage.h"

namespace tank_lifecycle
{
    struct Data {
        WzBehaviorEntityId hitbox = WZ_INVALID_BEHAVIOR_ENTITY;  // child hit_logger tally
        float   max_health = 0.0f;                               // <= 0 = immortal
        float   spawn_x = 0.0f, spawn_y = 0.0f, spawn_z = 0.0f;  // captured on first tick
        uint8_t spawn_captured = 0;
    };

    // Transient per-dispatch view (POD, by value); refreshed on every machine()
    // call. `event` is needed for the self-relative transform read + respawn write.
    struct Ctx {
        const WzBehaviorFrameFacts* facts = nullptr;
        const WzBehaviorEvent* event = nullptr;
        Data* d = nullptr;
    };

    using M = hfsm2::MachineT<hfsm2::Config::ContextT<Ctx>>;
    struct Alive; struct Dead;
    using Fsm = M::PeerRoot<Alive, Dead>;

    struct Alive : Fsm::State {
        void update(FullControl& ctl) {
            Ctx& c = ctl.context();
            Data* d = c.d;
            // Capture the spawn point once, retrying until the transform reads.
            if (!d->spawn_captured) {
                WzMat4 w{};
                if (wz_self_world_transform(c.facts, c.event, &w)) {
                    d->spawn_x = w.m[12];
                    d->spawn_y = w.m[13];
                    d->spawn_z = w.m[14];
                    d->spawn_captured = 1;
                }
            }
            if (d->max_health <= 0.0f) {
                return;   // immortal: never polls for death
            }
            if (auto* t = wz_instance_state_of<tank_damage::Tally>(
                    c.facts, d->hitbox, tank_damage::kModule)) {
                if (t->total >= d->max_health) {
                    ctl.changeTo<Dead>();
                }
            }
        }
    };

    struct Dead : Fsm::State {
        // Announce, clear the tally, and teleport to the captured spawn point.
        void enter(Control& ctl) {
            Ctx& c = ctl.context();
            Data* d = c.d;
            if (auto* t = wz_instance_state_of<tank_damage::Tally>(
                    c.facts, d->hitbox, tank_damage::kModule)) {
                wz_log_infof(
                    c.facts, "[DEATH] tank destroyed (%.0f dmg) -- respawning",
                    (double)t->total);
                t->total = 0.0f;
            }
            if (d->spawn_captured) {
                wz_write_set_world_translation(
                    c.facts, wz_self(c.event), d->spawn_x, d->spawn_y, d->spawn_z);
            }
        }
        void update(FullControl& ctl) { ctl.changeTo<Alive>(); }
    };

    // The owner embeds one of these in its instance state (trivially copyable).
    struct State {
        Data data{};
        wz::fsm2::LiveMachine<Fsm> fsm{};
    };

    // Bind the hitbox + health. Preserves any already-captured spawn + the live
    // machine across reload (so a rebuild keeps the original spawn), matching the
    // old inline lifecycle that tank_init never reset.
    inline void init(WzBehaviorEntityId hitbox, float max_health, State* s)
    {
        s->data.hitbox = hitbox;
        s->data.max_health = max_health;
    }

    inline void tick(
        const WzBehaviorFrameFacts* facts, const WzBehaviorEvent* event, State* s)
    {
        Ctx c{ facts, event, &s->data };
        wz::fsm2::machine(s->fsm, c).update();
    }
}
