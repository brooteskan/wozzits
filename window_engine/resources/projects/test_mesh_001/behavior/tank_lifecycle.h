#pragma once
// behavior/tank_lifecycle.h
//
// Shared tank life/death lifecycle as an HFSM2 machine (header-only, like
// tank_drive.h / cannon_fire.h). A tank owns a tank_lifecycle::State, calls
// init() once (with its "hitbox" child + max health), tick()s every frame, and
// gates its own actions on is_alive():
//
//   Alive --(damage >= max_health)--> Dead --(after kRespawnDelay)--> Alive
//
// Alive captures the self spawn point on the first readable frame and polls the
// hitbox tally. On death the tank enters Dead: it HIDES (a "destroyed" beat --
// render-only, so the machine keeps ticking) and holds for kRespawnDelay; the
// owner freezes its controls while !is_alive(). On leaving Dead it RESPAWNS --
// clears the tally, teleports to its respawn point, and shows again. So the
// respawn teleport is a deliberate, telegraphed beat rather than an instant snap
// mid-combat. The tank is never removed (its camera / children ride with it).
//
// The respawn point is either the captured self spawn (player) or a configured
// ANCHOR node's CURRENT world position (enemies respawn at the squad command
// node) -- pass the anchor to init(); it falls back to the self spawn if the
// anchor is unset or unreadable.
//
// The player and (now) the enemy both wire this; the enemy passes its command
// node as the respawn anchor so a destroyed enemy regroups at HQ. Persistence mirrors cannon_fire: the machine + captured
// spawn ride in the tank's instance state and survive rebuild relocation; init()
// preserves the captured spawn across reload (the old inline state was never
// reset in tank_init either).

#include <engine/behavior/behavior_module_api.h>
#include <engine/behavior/fsm2/fsm2_behavior.h>

#include "tank_damage.h"

namespace tank_lifecycle
{
    // How long the tank stays "destroyed" (hidden, frozen) before it respawns.
    inline constexpr float kRespawnDelay = 1.5f;  // seconds

    struct Data {
        WzBehaviorEntityId hitbox = WZ_INVALID_BEHAVIOR_ENTITY;  // child hit_logger tally
        // Respawn point. INVALID -> the captured self spawn below (player). Valid
        // -> that node's CURRENT world position, read at respawn (enemies at the
        // squad command node); falls back to the self spawn if it can't be read.
        WzBehaviorEntityId respawn_anchor = WZ_INVALID_BEHAVIOR_ENTITY;
        float   max_health = 0.0f;                               // <= 0 = immortal
        float   spawn_x = 0.0f, spawn_y = 0.0f, spawn_z = 0.0f;  // captured on first tick
        uint8_t spawn_captured = 0;
        float   respawn_timer = 0.0f;   // seconds left in Dead
        uint8_t alive = 0;              // 1 while Alive (the owner gates on this)
    };

    // Transient per-dispatch view (POD, by value); refreshed on every machine()
    // call. `event` drives the self-relative transform read + respawn/hide writes.
    struct Ctx {
        const WzBehaviorFrameFacts* facts = nullptr;
        const WzBehaviorEvent* event = nullptr;
        Data* d = nullptr;
    };

    using M = hfsm2::MachineT<hfsm2::Config::ContextT<Ctx>>;
    struct Alive; struct Dead;
    using Fsm = M::PeerRoot<Alive, Dead>;

    struct Alive : Fsm::State {
        void enter(Control& ctl) { ctl.context().d->alive = 1; }
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
        // Destroyed: announce, hold for kRespawnDelay, and vanish (render-only).
        void enter(Control& ctl) {
            Ctx& c = ctl.context();
            Data* d = c.d;
            d->alive = 0;
            d->respawn_timer = kRespawnDelay;
            if (auto* t = wz_instance_state_of<tank_damage::Tally>(
                    c.facts, d->hitbox, tank_damage::kModule)) {
                wz_log_infof(
                    c.facts, "[DEATH] tank destroyed (%.0f dmg) -- respawning in %.1fs",
                    (double)t->total, (double)kRespawnDelay);
            }
            wz_write_set_visible(c.facts, wz_self(c.event), 0);
        }
        void update(FullControl& ctl) {
            Ctx& c = ctl.context();
            c.d->respawn_timer -= wz_delta_seconds(c.facts);
            if (c.d->respawn_timer <= 0.0f) {
                ctl.changeTo<Alive>();
            }
        }
        // Respawn: clear the tally, teleport to the respawn point (the anchor
        // node's current position if set + readable, else the captured self
        // spawn), show again.
        void exit(Control& ctl) {
            Ctx& c = ctl.context();
            Data* d = c.d;
            if (auto* t = wz_instance_state_of<tank_damage::Tally>(
                    c.facts, d->hitbox, tank_damage::kModule)) {
                t->total = 0.0f;
            }
            float rx = d->spawn_x, ry = d->spawn_y, rz = d->spawn_z;
            uint8_t have = d->spawn_captured;
            if (d->respawn_anchor != WZ_INVALID_BEHAVIOR_ENTITY) {
                WzVec3 p{};
                if (wz_read_world_position(c.facts, d->respawn_anchor, &p)) {
                    rx = p.x; ry = p.y; rz = p.z; have = 1;
                }
            }
            if (have) {
                wz_write_set_world_translation(
                    c.facts, wz_self(c.event), rx, ry, rz);
            }
            wz_write_set_visible(c.facts, wz_self(c.event), 1);
        }
    };

    // The owner embeds one of these in its instance state (trivially copyable).
    struct State {
        Data data{};
        wz::fsm2::LiveMachine<Fsm> fsm{};
    };

    // Bind the hitbox + health. Preserves any already-captured spawn + the live
    // machine across reload, matching the old inline lifecycle tank_init never reset.
    inline void init(
        WzBehaviorEntityId hitbox, float max_health, State* s,
        WzBehaviorEntityId respawn_anchor = WZ_INVALID_BEHAVIOR_ENTITY)
    {
        s->data.hitbox = hitbox;
        s->data.max_health = max_health;
        s->data.respawn_anchor = respawn_anchor;
    }

    inline void tick(
        const WzBehaviorFrameFacts* facts, const WzBehaviorEvent* event, State* s)
    {
        Ctx c{ facts, event, &s->data };
        wz::fsm2::machine(s->fsm, c).update();
    }

    // 1 while alive, 0 while destroyed/respawning. Owners gate driving + firing on
    // this so a dead tank sits frozen at the death spot until it respawns. Valid
    // after the first tick() (which constructs the machine into Alive).
    inline uint8_t is_alive(const State* s) { return s->data.alive; }
}
