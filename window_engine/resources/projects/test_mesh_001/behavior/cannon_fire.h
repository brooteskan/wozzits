#pragma once
// behavior/cannon_fire.h
//
// THE single "fire the cannon" sequence, shared by every tank (header-only, like
// tank_drive.h). A behavior owns a cannon_fire::State, calls init() once, tick()
// every frame, and fire() to SHOOT. There is no timer -- the OWNER decides when
// to fire (player: a controller button in tank_controller; enemy: its AI in
// quantum_tank_agent).
//
// The shot is TWO concurrent processes -- the projectile flies for seconds while
// the muzzle flash fades in ~0.1s -- so it is modeled as an HFSM2 machine with
// two ORTHOGONAL (parallel) regions instead of the old flag soup
// (proj_active / proj_launch / bursting / firing / boom_done):
//
//   Flight region:  Parked --Fire--> InFlight --hit--> Bursting --done--> Parked
//                                             \--range-------------------> Parked
//   Flash  region:  FlashOff --Fire--> FlashActive --faded--> FlashOff
//
// fire() is delivered to the machine as a `Fire` react BEFORE update() in the
// same tick, so a shot LAUNCHES (InFlight.enter) and takes its first STEP
// (InFlight.update) on the very frame fire() is called -- exactly as the old
// single-function tick did -- and re-firing from any state re-launches. enter()
// carries the one-shots: the launch raycast, and the report sound.
//
// Persistence: the live machine rides in cannon_fire::State via
// wz::fsm2::LiveMachine, so its active states survive hot-reload + rebuild
// relocation the same way the tank's other instance state does. See
// <engine/behavior/fsm2/fsm2_behavior.h>.
//
// Muzzle anchor: the tank GLB carries a mesh-less Empty "barrl_orientation"
// parented to the gun (body -> "turret" -> "gun" -> "barrl_orientation"). Its
// world POSITION is the muzzle and its orientation gives the barrel/firing axis.
// We anchor the flash right on it; if that marker is ever missing we fall back to
// the gun node's origin plus a nudge along the turret->gun direction.

#include <engine/behavior/behavior_module_api.h>
#include <engine/behavior/fsm2/fsm2_behavior.h>
#include <math.h>

#include "collidable.h"

namespace cannon_fire
{
    // Exact GLB Empty name (note the spelling authored in tank1.glb).
    inline constexpr const char* kMuzzleMarkerName = "barrel_orientation";

    // ---- Tunables ----
    inline constexpr float kMuzzleForward = 0.0f;   // nudge past the marker, along the barrel axis
    inline constexpr float kMuzzleUp      = 0.0f;   // extra lift (marker Y already carries height)
    inline constexpr float kFlashScale    = 0.15f;  // flash blob half-extent (cube is -1..1)
    inline constexpr float kFadeSeconds   = 0.10f;  // how fast the flash fades (bright -> 0)
    inline constexpr float kBeamDistance  = 40.0f;  // trajectory beam length (world units)
    inline constexpr float kBeamThick     = 0.30f;  // trajectory beam thickness (world units)
    inline constexpr float kImpactScale   = 1.50f;  // impact-flash blob half-extent
    inline constexpr float kImpactMaxDist = 500.0f; // how far to trace the shot to the terrain

    // Projectile: a bright collider that physically FLIES from the muzzle down the
    // firing axis on a shot, so it sweeps through a target's hit-collider and fires
    // COLLISION_ENTER on it. One per tank, reused (parked far below when idle) -- no
    // spawn churn. cannon_fire owns the flight; the victim handles the hit.
    inline constexpr float kProjSpeed   = 300.0f;    // flight speed (world units / sec)
    inline constexpr float kProjMaxDist = 300.0f;   // range before it parks (units)
    inline constexpr float kProjParkY   = -1000.0f; // world Y to park it at when idle
    inline constexpr float kProjScale    = 0.5f;    // projectile size in flight (matches prefab)
    inline constexpr float kBurstScale   = 5.0f;    // impact-burst size (~x10 the projectile)
    inline constexpr float kBurstSeconds = 1.0f / 15.0f; // how long the impact burst holds

    inline constexpr uint32_t kIntensity = wz_renderable_param_hash("intensity");

    // Local-space quaternion {x,y,z,w} rotating the unit +X axis onto unit `d`.
    // Points the trajectory beam (a cube stretched along its local +X) down the
    // firing direction. Handles the aligned / opposite degeneracies.
    inline WzQuaternion quat_pos_x_to(float dx, float dy, float dz)
    {
        const float dot = dx;  // dot((1,0,0), d)
        if (dot > 0.99999f)  { return WzQuaternion{ 0.0f, 0.0f, 0.0f, 1.0f }; }
        if (dot < -0.99999f) { return WzQuaternion{ 0.0f, 0.0f, 1.0f, 0.0f }; }  // 180 about +Z
        // axis = normalize(cross((1,0,0), d)) = normalize((0, -dz, dy))
        float ax = 0.0f, ay = -dz, az = dy;
        const float al = sqrtf(ax * ax + ay * ay + az * az);
        if (al > 1e-6f) { ax /= al; ay /= al; az /= al; }
        const float ang = acosf(dot);
        const float s = sinf(ang * 0.5f);
        return WzQuaternion{ ax * s, ay * s, az * s, cosf(ang * 0.5f) };
    }

    // ── Persistent shot data the state machine reads/writes ─────────────────────
    // Everything that must survive across states (InFlight -> Bursting) and across
    // frames lives here; the FSM states are stateless controllers over it. Handles
    // are resolved once in init(); the rest is per-shot scratch.
    struct ShotData
    {
        WzBehaviorEntityId self   = WZ_INVALID_BEHAVIOR_ENTITY;
        WzBehaviorEntityId turret = WZ_INVALID_BEHAVIOR_ENTITY;  // pivot (fallback dir)
        WzBehaviorEntityId barrel = WZ_INVALID_BEHAVIOR_ENTITY;  // "gun" node (fallback anchor)
        WzBehaviorEntityId muzzle = WZ_INVALID_BEHAVIOR_ENTITY;  // "barrl_orientation" Empty
        WzBehaviorEntityId flash  = WZ_INVALID_BEHAVIOR_ENTITY;  // "muzzle_beam" node
        WzBehaviorEntityId beam   = WZ_INVALID_BEHAVIOR_ENTITY;  // "trajectory" node
        WzBehaviorEntityId impact = WZ_INVALID_BEHAVIOR_ENTITY;  // "impact_flash" node
        WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY; // for the impact raycast
        WzBehaviorEntityId projectile = WZ_INVALID_BEHAVIOR_ENTITY; // "projectile" node (flies)
        float   proj_x = 0.0f, proj_y = 0.0f, proj_z = 0.0f;    // projectile world pos
        float   proj_dx = 0.0f, proj_dy = 0.0f, proj_dz = 0.0f; // unit flight direction
        float   proj_dist = 0.0f;   // distance flown this shot
        float   impact_x = 0.0f, impact_y = 0.0f, impact_z = 0.0f; // burst position
        float   burst_timer = 0.0f; // seconds left on the burst
        float   terrain_dist = 0.0f;   // muzzle->terrain distance (launch raycast)
        float   terrain_x = 0.0f, terrain_y = 0.0f, terrain_z = 0.0f; // terrain hit point
        uint8_t terrain_valid = 0;  // 1 = the shot ray hit terrain this launch
        float   intensity = 0.0f;   // current flash brightness (fades to 0)
    };

    // Transient per-dispatch view (POD, by value): the persistent data plus the
    // muzzle anchor/axis recomputed each tick. Refreshed on every machine() call.
    struct ShotCtx
    {
        const WzBehaviorFrameFacts* facts = nullptr;
        ShotData* d = nullptr;
        float ax = 0.0f, ay = 0.0f, az = 0.0f;  // muzzle anchor (world)
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;  // firing axis (unit)
    };

    // React event that starts (or re-starts) a shot.
    struct Fire {};

    using M = hfsm2::MachineT<hfsm2::Config::ContextT<ShotCtx>>;

    struct ShotRoot; struct FlightRegion; struct FlashRegion;
    struct Parked; struct InFlight; struct Bursting;
    struct FlashOff; struct FlashActive;

    using ShotFsm = M::PeerRoot<
        M::Orthogonal<ShotRoot,
            M::Composite<FlightRegion, Parked, InFlight, Bursting>,
            M::Composite<FlashRegion,  FlashOff, FlashActive>
        >
    >;

    // ── States ──────────────────────────────────────────────────────────────────
    struct ShotRoot : ShotFsm::State {};

    // Flight region head: Fire (re)launches from whatever sub-state is active.
    struct FlightRegion : ShotFsm::State {
        void react(const Fire&, EventControl& c) { c.changeTo<InFlight>(); }
        using ShotFsm::State::react;
    };

    // Idle: the projectile sits parked far below (put there by whoever left to us,
    // or its authored y = -1000). Nothing to do until the next Fire.
    struct Parked : ShotFsm::State {};

    struct InFlight : ShotFsm::State {
        // LAUNCH (was the proj_launch tick): trace the shot to the terrain ONCE
        // (fixes the beam clip + ground-impact range + impact-flash point), clear
        // the projectile's hit flag + stamp the shooter, and seed pos/dir. Runs on
        // the Fire frame (react is processed before this frame's update()).
        void enter(Control& ctl) {
            ShotCtx& c = ctl.context();
            ShotData* d = c.d;
            d->terrain_valid = 0;
            d->terrain_dist = 0.0f;
            if (d->terrain != WZ_INVALID_BEHAVIOR_ENTITY) {
                const WzVec3 origin{ c.ax, c.ay, c.az };
                const WzVec3 dir{ c.nx, c.ny, c.nz };
                WzSurfaceSample surf{};
                if (wz_query_collision_surface_ray(
                        c.facts, d->terrain, origin, dir, kImpactMaxDist, &surf)
                    && surf.hit)
                {
                    const float ex = surf.position.x - c.ax;
                    const float ey = surf.position.y - c.ay;
                    const float ez = surf.position.z - c.az;
                    d->terrain_dist = sqrtf(ex * ex + ey * ey + ez * ez);
                    d->terrain_x = surf.position.x;
                    d->terrain_y = surf.position.y;
                    d->terrain_z = surf.position.z;
                    d->terrain_valid = 1;
                }
            }
            if (auto* p = wz_instance_state_of<collidable::State>(
                    c.facts, d->projectile, collidable::kModule)) {
                p->hit = 0;
                p->owner = d->self;  // credit the shooter (this tank's root)
            }
            d->proj_x = c.ax; d->proj_y = c.ay; d->proj_z = c.az;
            d->proj_dx = c.nx; d->proj_dy = c.ny; d->proj_dz = c.nz;
            d->proj_dist = 0.0f;
        }

        // STEP (was the proj_active branch): march the projectile down the firing
        // axis; on a tank/terrain strike burst, at max range park, else move+glow.
        void update(FullControl& ctl) {
            ShotCtx& c = ctl.context();
            ShotData* d = c.d;
            if (d->projectile == WZ_INVALID_BEHAVIOR_ENTITY) {
                return;
            }
            const float step = kProjSpeed * wz_delta_seconds(c.facts);
            d->proj_x += d->proj_dx * step;
            d->proj_y += d->proj_dy * step;
            d->proj_z += d->proj_dz * step;
            d->proj_dist += step;

            uint8_t hit_tank = 0;
            if (auto* p = wz_instance_state_of<collidable::State>(
                    c.facts, d->projectile, collidable::kModule)) {
                if (p->hit) {
                    hit_tank = 1;
                    d->impact_x = p->hx; d->impact_y = p->hy; d->impact_z = p->hz;
                }
            }
            const uint8_t hit_terrain =
                !hit_tank && d->terrain_valid && d->proj_dist >= d->terrain_dist;
            if (hit_terrain) {
                d->impact_x = d->terrain_x;
                d->impact_y = d->terrain_y;
                d->impact_z = d->terrain_z;
            }

            if (hit_tank || hit_terrain) {
                ctl.changeTo<Bursting>();   // Bursting.enter freezes + swells + glows
            }
            else if (d->proj_dist >= kProjMaxDist) {
                wz_write_set_world_translation(
                    c.facts, d->projectile, d->proj_x, kProjParkY, d->proj_z);
                wz_write_set_renderable_param(
                    c.facts, d->projectile, kIntensity, 0.0f, 0.0f, 0.0f);
                ctl.changeTo<Parked>();     // out of range: park far below, go dark
            }
            else {
                wz_write_set_world_translation(
                    c.facts, d->projectile, d->proj_x, d->proj_y, d->proj_z);
                wz_write_set_renderable_param(
                    c.facts, d->projectile, kIntensity, 1.0f, 0.0f, 0.0f);
            }
        }
    };

    struct Bursting : ShotFsm::State {
        // Freeze at the strike point and swell (~x10) bright.
        void enter(Control& ctl) {
            ShotCtx& c = ctl.context();
            ShotData* d = c.d;
            d->burst_timer = kBurstSeconds;
            wz_write_set_world_translation(
                c.facts, d->projectile, d->impact_x, d->impact_y, d->impact_z);
            wz_write_set_local_scale(
                c.facts, d->projectile, kBurstScale, kBurstScale, kBurstScale);
            wz_write_set_renderable_param(
                c.facts, d->projectile, kIntensity, 1.0f, 0.0f, 0.0f);
        }
        // Hold the burst, then shrink + park far below + go dark.
        void update(FullControl& ctl) {
            ShotCtx& c = ctl.context();
            ShotData* d = c.d;
            d->burst_timer -= wz_delta_seconds(c.facts);
            if (d->burst_timer <= 0.0f) {
                wz_write_set_local_scale(
                    c.facts, d->projectile, kProjScale, kProjScale, kProjScale);
                wz_write_set_world_translation(
                    c.facts, d->projectile, d->impact_x, kProjParkY, d->impact_z);
                wz_write_set_renderable_param(
                    c.facts, d->projectile, kIntensity, 0.0f, 0.0f, 0.0f);
                ctl.changeTo<Parked>();
            }
        }
    };

    // Flash region head: Fire (re)starts the muzzle flash.
    struct FlashRegion : ShotFsm::State {
        void react(const Fire&, EventControl& c) { c.changeTo<FlashActive>(); }
        using ShotFsm::State::react;
    };

    struct FlashOff : ShotFsm::State {};

    struct FlashActive : ShotFsm::State {
        // The one-shots the old `firing`/`boom_done` flags guarded.
        void enter(Control& ctl) {
            ShotCtx& c = ctl.context();
            c.d->intensity = 1.0f;
            wz_write_play_sound_named(c.facts, c.d->self, "Canon_a");
        }
        // Sit the flash on the muzzle, lay the trajectory beam down the firing
        // axis (clipped at the hillside), fade the glow, and preview the impact.
        void update(FullControl& ctl) {
            ShotCtx& c = ctl.context();
            ShotData* d = c.d;

            wz_write_set_world_translation(
                c.facts, d->flash,
                c.ax + c.nx * kMuzzleForward,
                c.ay + c.ny * kMuzzleForward + kMuzzleUp,
                c.az + c.nz * kMuzzleForward);
            wz_write_set_local_scale(
                c.facts, d->flash, kFlashScale, kFlashScale, kFlashScale);

            // Trajectory beam: a long thin cube from the muzzle along the firing
            // axis. The beam is a child of the hull, so express the world firing
            // direction in the hull's local frame and point the beam's local +X
            // down it, then stretch it (clipped at the terrain hit).
            if (d->beam != WZ_INVALID_BEHAVIOR_ENTITY) {
                WzMat4 hull{};
                if (wz_read_world_transform(c.facts, d->self, &hull)) {
                    float c0x = hull.m[0], c0y = hull.m[1], c0z = hull.m[2];
                    float c1x = hull.m[4], c1y = hull.m[5], c1z = hull.m[6];
                    float c2x = hull.m[8], c2y = hull.m[9], c2z = hull.m[10];
                    const float l0 = sqrtf(c0x * c0x + c0y * c0y + c0z * c0z);
                    const float l1 = sqrtf(c1x * c1x + c1y * c1y + c1z * c1z);
                    const float l2 = sqrtf(c2x * c2x + c2y * c2y + c2z * c2z);
                    if (l0 > 1e-6f) { c0x /= l0; c0y /= l0; c0z /= l0; }
                    if (l1 > 1e-6f) { c1x /= l1; c1y /= l1; c1z /= l1; }
                    if (l2 > 1e-6f) { c2x /= l2; c2y /= l2; c2z /= l2; }
                    const float lx = c0x * c.nx + c0y * c.ny + c0z * c.nz;
                    const float ly = c1x * c.nx + c1y * c.ny + c1z * c.nz;
                    const float lz = c2x * c.nx + c2y * c.ny + c2z * c.nz;
                    const WzQuaternion q = quat_pos_x_to(lx, ly, lz);
                    float beam_len = kBeamDistance;
                    if (d->terrain_valid && d->terrain_dist < beam_len) {
                        beam_len = d->terrain_dist;
                    }
                    const float half = 0.5f * beam_len;
                    wz_write_set_world_translation(
                        c.facts, d->beam,
                        c.ax + c.nx * half, c.ay + c.ny * half, c.az + c.nz * half);
                    wz_write_set_local_rotation(c.facts, d->beam, q);
                    wz_write_set_local_scale(
                        c.facts, d->beam, half, 0.5f * kBeamThick, 0.5f * kBeamThick);
                }
            }

            const float dt = wz_delta_seconds(c.facts);
            const float decay = (kFadeSeconds > 0.0f) ? (dt / kFadeSeconds) : 1.0f;
            d->intensity = (d->intensity > decay) ? (d->intensity - decay) : 0.0f;
            wz_write_set_renderable_param(
                c.facts, d->flash, kIntensity, d->intensity, 0.0f, 0.0f);
            if (d->beam != WZ_INVALID_BEHAVIOR_ENTITY) {
                wz_write_set_renderable_param(
                    c.facts, d->beam, kIntensity, d->intensity, 0.0f, 0.0f);
            }
            if (d->impact != WZ_INVALID_BEHAVIOR_ENTITY) {
                if (d->terrain_valid) {
                    wz_write_set_world_translation(
                        c.facts, d->impact, d->terrain_x, d->terrain_y, d->terrain_z);
                    wz_write_set_local_scale(
                        c.facts, d->impact, kImpactScale, kImpactScale, kImpactScale);
                    wz_write_set_renderable_param(
                        c.facts, d->impact, kIntensity, d->intensity, 0.0f, 0.0f);
                } else {
                    wz_write_set_renderable_param(
                        c.facts, d->impact, kIntensity, 0.0f, 0.0f, 0.0f);
                }
            }
            if (d->intensity <= 0.0f) {
                ctl.changeTo<FlashOff>();
            }
        }
    };

    // The owner embeds ONE of these (in its wz_instance_state). data + the live
    // machine + a one-shot fire request; trivially copyable, so it rides the
    // behavior state block through hot-reload + rebuild like the rest.
    struct State
    {
        ShotData data{};
        wz::fsm2::LiveMachine<ShotFsm> fsm{};
        uint8_t fire_pending = 0;
    };

    // Muzzle anchor (world) + firing axis (unit) for THIS frame. From the GLB
    // "barrl_orientation" Empty (exact), else the gun origin + turret->gun
    // direction. Returns false only when neither is readable (skip the tick).
    inline bool read_muzzle_anchor(
        const WzBehaviorFrameFacts* facts, const ShotData& d,
        float& ax, float& ay, float& az, float& nx, float& ny, float& nz)
    {
        nx = ny = nz = 0.0f;
        WzMat4 m{};
        if (d.muzzle != WZ_INVALID_BEHAVIOR_ENTITY
            && wz_read_world_transform(facts, d.muzzle, &m))
        {
            ax = m.m[12]; ay = m.m[13]; az = m.m[14];
            // Firing axis = the marker's local Y (empirically the bore), pointed
            // AWAY from the turret so +Y vs -Y needs no guessing.
            nx = m.m[4]; ny = m.m[5]; nz = m.m[6];
            WzMat4 tw{};
            if (d.turret != WZ_INVALID_BEHAVIOR_ENTITY
                && wz_read_world_transform(facts, d.turret, &tw))
            {
                const float rx = ax - tw.m[12];
                const float ry = ay - tw.m[13];
                const float rz = az - tw.m[14];
                if (nx * rx + ny * ry + nz * rz < 0.0f) {
                    nx = -nx; ny = -ny; nz = -nz;
                }
            }
            const float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            return true;
        }
        if (d.barrel != WZ_INVALID_BEHAVIOR_ENTITY
            && wz_read_world_transform(facts, d.barrel, &m))
        {
            ax = m.m[12]; ay = m.m[13]; az = m.m[14];
            WzMat4 turret_w{};
            if (d.turret != WZ_INVALID_BEHAVIOR_ENTITY
                && wz_read_world_transform(facts, d.turret, &turret_w))
            {
                const float dx = ax - turret_w.m[12];
                const float dz = az - turret_w.m[14];
                const float len = sqrtf(dx * dx + dz * dz);
                if (len > 1e-3f) { nx = dx / len; nz = dz / len; }
            }
            return true;
        }
        return false;
    }

    // The owner passes the turret + barrel (gun) handles it already resolved; we
    // resolve the muzzle marker + flash/beam/impact/projectile children
    // (self-relative -> instance-safe), reset the shot data, and reset the machine
    // to idle (Parked + FlashOff) so a rebuild/reload starts clean.
    inline void init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId self,
        WzBehaviorEntityId turret,
        WzBehaviorEntityId barrel,
        WzBehaviorEntityId terrain,
        State* s)
    {
        s->data = ShotData{};
        s->fire_pending = 0;
        s->fsm.constructed = 0;   // rebuild the machine at Parked/FlashOff next tick

        s->data.self = self;
        s->data.turret = turret;
        s->data.barrel = barrel;
        s->data.terrain = terrain;
        wz_find_descendant_by_name(facts, self, kMuzzleMarkerName, &s->data.muzzle);
        wz_find_descendant_by_name(facts, self, "muzzle_beam", &s->data.flash);
        wz_find_descendant_by_name(facts, self, "trajectory", &s->data.beam);
        wz_find_descendant_by_name(facts, self, "impact_flash", &s->data.impact);
        wz_find_descendant_by_name(facts, self, "projectile", &s->data.projectile);
        // 1 = the GLB muzzle Empty resolved (exact anchor); 0 = fell back to the
        // gun+turret heuristic (marker missing / GLB not re-imported / name typo).
        wz_log_infof(facts, "[cannon] find muzzle marker: %u",
            (unsigned)(s->data.muzzle != WZ_INVALID_BEHAVIOR_ENTITY));
    }

    // FIRE. Queues a shot; the sequence runs on the NEXT tick (same frame if the
    // owner ticks after firing, as the tanks do). Re-firing restarts the shot.
    inline void fire(State* s)
    {
        s->fire_pending = 1;
    }

    // Per-frame: refresh the muzzle anchor, deliver a pending Fire, then advance
    // the machine. Safe to call every frame (idle states are no-ops).
    inline void tick(
        const WzBehaviorFrameFacts* facts, const WzBehaviorEvent* event, State* s)
    {
        (void)event;
        if (!facts || !s || s->data.flash == WZ_INVALID_BEHAVIOR_ENTITY) {
            return;
        }

        float ax, ay, az, nx, ny, nz;
        if (!read_muzzle_anchor(facts, s->data, ax, ay, az, nx, ny, nz)) {
            return;
        }

        ShotCtx ctx{ facts, &s->data, ax, ay, az, nx, ny, nz };
        auto& machine = wz::fsm2::machine(s->fsm, ctx);
        if (s->fire_pending) {
            machine.react(Fire{});   // launch + start flash (enter() runs now)
            s->fire_pending = 0;
        }
        machine.update();            // step flight + fade flash (same frame as Fire)
    }
}
