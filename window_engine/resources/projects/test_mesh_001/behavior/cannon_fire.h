#pragma once
// behavior/cannon_fire.h
//
// THE single "fire the cannon" function, shared by every tank (header-only, like
// tank_drive.h). A behavior owns a cannon_fire::State, calls init() once, tick()
// every frame, and fire() to SHOOT. There is no timer -- the OWNER decides when
// to fire (player: a controller button in tank_controller; enemy: its AI in
// quantum_tank_agent).
//
// For now this is deliberately minimal: fire() flashes ONE bright blob at the
// muzzle of the tank's gun (and plays the report).
//
// Muzzle anchor: the tank GLB carries a mesh-less Empty "barrl_orientation"
// parented to the gun (body -> "turret" -> "gun" -> "barrl_orientation"). Its
// world POSITION is the muzzle and its orientation gives the barrel/firing axis
// (for the shot trajectory later). We anchor the flash right on it. If that
// marker is ever missing we fall back to the gun node's origin plus a nudge along
// the turret->gun direction. The owner passes the turret + barrel (both recorded
// in the tank's state); this module resolves the flash child + the marker.

#include <engine/behavior/behavior_module_api.h>
#include <math.h>

#include "projectile_impact.h"

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

    struct State
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
        uint8_t proj_active = 0;    // 1 = in flight
        uint8_t proj_launch = 0;    // 1 = launch pending (seed pos/dir next tick)
        uint8_t bursting = 0;       // 1 = holding the impact burst
        float   burst_timer = 0.0f; // seconds left on the burst
        float   impact_x = 0.0f, impact_y = 0.0f, impact_z = 0.0f; // burst position
        float   terrain_dist = 0.0f;   // muzzle->terrain distance (launch raycast)
        float   terrain_x = 0.0f, terrain_y = 0.0f, terrain_z = 0.0f; // terrain hit point
        uint8_t terrain_valid = 0;  // 1 = the shot ray hit terrain this launch
        float   intensity = 0.0f;   // current flash brightness (fades to 0)
        uint8_t firing = 0;
        uint8_t boom_done = 1;
    };

    // The owner passes the turret + barrel (gun) handles it already resolved; we
    // resolve the muzzle marker + flash child (self-relative -> instance-safe).
    inline void init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId self,
        WzBehaviorEntityId turret,
        WzBehaviorEntityId barrel,
        WzBehaviorEntityId terrain,
        State* s)
    {
        s->self = self;
        s->turret = turret;
        s->barrel = barrel;
        s->terrain = terrain;
        s->muzzle = WZ_INVALID_BEHAVIOR_ENTITY;
        s->flash = WZ_INVALID_BEHAVIOR_ENTITY;
        s->beam = WZ_INVALID_BEHAVIOR_ENTITY;
        s->impact = WZ_INVALID_BEHAVIOR_ENTITY;
        wz_find_descendant_by_name(facts, self, kMuzzleMarkerName, &s->muzzle);
        wz_find_descendant_by_name(facts, self, "muzzle_beam", &s->flash);
        wz_find_descendant_by_name(facts, self, "trajectory", &s->beam);
        wz_find_descendant_by_name(facts, self, "impact_flash", &s->impact);
        wz_find_descendant_by_name(facts, self, "projectile", &s->projectile);
        // 1 = the GLB muzzle Empty resolved (exact anchor); 0 = fell back to the
        // gun+turret heuristic (marker missing / GLB not re-imported / name typo).
        wz_log_infof(facts, "[cannon] find muzzle marker: %u",
            (unsigned)(s->muzzle != WZ_INVALID_BEHAVIOR_ENTITY));
        s->intensity = 0.0f;
        s->firing = 0;
        s->boom_done = 1;
        s->proj_active = 0;
        s->proj_launch = 0;
        s->bursting = 0;
        s->terrain_valid = 0;
    }

    // FIRE. Flash the muzzle + play the report. Call from a trigger (controller
    // button, AI shot). Idempotent-ish: a re-fire restarts the flash.
    inline void fire(State* s)
    {
        s->intensity = 1.0f;
        s->firing = 1;
        s->boom_done = 0;
        s->proj_active = 1;
        s->proj_launch = 1;
    }

    // Per-frame: sit the flash on the muzzle, and fade it out after a shot. Safe to
    // call every frame (the flash is invisible at intensity 0 until fire()).
    inline void tick(
        const WzBehaviorFrameFacts* facts, const WzBehaviorEvent* event, State* s)
    {
        (void)event;
        if (!facts || !s || s->flash == WZ_INVALID_BEHAVIOR_ENTITY) {
            return;
        }

        // Anchor = the "barrl_orientation" marker's world position (the muzzle),
        // with the barrel/firing axis from its orientation. The marker rides the
        // gun, so the flash tracks hull + turret exactly. Fall back to the gun
        // origin + turret->gun direction if the marker is ever missing.
        float ax, ay, az;                       // anchor (muzzle)
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;  // firing axis (for the nudge + trajectory)
        WzMat4 m{};
        if (s->muzzle != WZ_INVALID_BEHAVIOR_ENTITY
            && wz_read_world_transform(facts, s->muzzle, &m))
        {
            ax = m.m[12]; ay = m.m[13]; az = m.m[14];
            // Firing axis = the marker's local Y. Empirically (on device) the
            // marker's -X reads 90 to the RIGHT of the bore and +Z is UP, so the
            // bore is the remaining axis, Y. Disambiguate +Y vs -Y by pointing it
            // AWAY from the turret (out the muzzle) -- no sign guessing, and it also
            // carries any barrel pitch the Empty was given.
            nx = m.m[4]; ny = m.m[5]; nz = m.m[6];
            WzMat4 tw{};
            if (s->turret != WZ_INVALID_BEHAVIOR_ENTITY
                && wz_read_world_transform(facts, s->turret, &tw))
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
        }
        else if (s->barrel != WZ_INVALID_BEHAVIOR_ENTITY
                 && wz_read_world_transform(facts, s->barrel, &m))
        {
            ax = m.m[12]; ay = m.m[13]; az = m.m[14];
            WzMat4 turret_w{};
            if (s->turret != WZ_INVALID_BEHAVIOR_ENTITY
                && wz_read_world_transform(facts, s->turret, &turret_w))
            {
                const float dx = ax - turret_w.m[12];
                const float dz = az - turret_w.m[14];
                const float len = sqrtf(dx * dx + dz * dz);
                if (len > 1e-3f) { nx = dx / len; nz = dz / len; }
            }
        }
        else {
            return;
        }

        // At launch, trace the shot to the terrain ONCE (the ray is fixed for the
        // shot): the distance clips the trajectory beam at a hillside and gives the
        // projectile its ground-impact range, and the point places the impact flash.
        // Also clear the projectile's own hit flag for the new shot.
        if (s->proj_launch) {
            s->terrain_valid = 0;
            s->terrain_dist = 0.0f;
            if (s->terrain != WZ_INVALID_BEHAVIOR_ENTITY) {
                const WzVec3 origin{ ax, ay, az };
                const WzVec3 dir{ nx, ny, nz };
                WzSurfaceSample surf{};
                if (wz_query_collision_surface_ray(
                        facts, s->terrain, origin, dir, kImpactMaxDist, &surf)
                    && surf.hit)
                {
                    const float ex = surf.position.x - ax;
                    const float ey = surf.position.y - ay;
                    const float ez = surf.position.z - az;
                    s->terrain_dist = sqrtf(ex * ex + ey * ey + ez * ez);
                    s->terrain_x = surf.position.x;
                    s->terrain_y = surf.position.y;
                    s->terrain_z = surf.position.z;
                    s->terrain_valid = 1;
                }
            }
            if (auto* p = wz_instance_state_of<projectile_impact::State>(
                    facts, s->projectile, projectile_impact::kModule)) {
                p->hit = 0;
            }
        }

        wz_write_set_world_translation(
            facts, s->flash,
            ax + nx * kMuzzleForward,
            ay + ny * kMuzzleForward + kMuzzleUp,
            az + nz * kMuzzleForward);
        wz_write_set_local_scale(
            facts, s->flash, kFlashScale, kFlashScale, kFlashScale);

        // Trajectory beam: a long thin cube from the muzzle along the firing axis.
        // The beam is a child of the hull, so express the world firing direction in
        // the hull's local frame (transpose of the hull's rotation basis) and point
        // the beam's local +X down it, then stretch it kBeamDistance long.
        if (s->beam != WZ_INVALID_BEHAVIOR_ENTITY) {
            WzMat4 hull{};
            if (wz_read_world_transform(facts, s->self, &hull)) {
                float c0x = hull.m[0], c0y = hull.m[1], c0z = hull.m[2];
                float c1x = hull.m[4], c1y = hull.m[5], c1z = hull.m[6];
                float c2x = hull.m[8], c2y = hull.m[9], c2z = hull.m[10];
                const float l0 = sqrtf(c0x * c0x + c0y * c0y + c0z * c0z);
                const float l1 = sqrtf(c1x * c1x + c1y * c1y + c1z * c1z);
                const float l2 = sqrtf(c2x * c2x + c2y * c2y + c2z * c2z);
                if (l0 > 1e-6f) { c0x /= l0; c0y /= l0; c0z /= l0; }
                if (l1 > 1e-6f) { c1x /= l1; c1y /= l1; c1z /= l1; }
                if (l2 > 1e-6f) { c2x /= l2; c2y /= l2; c2z /= l2; }
                const float lx = c0x * nx + c0y * ny + c0z * nz;
                const float ly = c1x * nx + c1y * ny + c1z * nz;
                const float lz = c2x * nx + c2y * ny + c2z * nz;
                const WzQuaternion q = quat_pos_x_to(lx, ly, lz);
                // Clip the beam at the hillside so it doesn't poke through terrain.
                float beam_len = kBeamDistance;
                if (s->terrain_valid && s->terrain_dist < beam_len) {
                    beam_len = s->terrain_dist;
                }
                const float half = 0.5f * beam_len;
                wz_write_set_world_translation(
                    facts, s->beam,
                    ax + nx * half, ay + ny * half, az + nz * half);
                wz_write_set_local_rotation(facts, s->beam, q);
                wz_write_set_local_scale(
                    facts, s->beam, half, 0.5f * kBeamThick, 0.5f * kBeamThick);
            }
        }

        // Projectile flight -- runs whether or not the flash is still "firing": the
        // flash fades in ~0.1s but the shot flies for seconds. Launched by fire();
        // seeds pos/dir from the muzzle on the first tick, then steps down the firing
        // axis until it parks at range. set_world_translation moves this hull-child in
        // WORLD space so its collider sweeps through a target's hit-collider.
        if (s->projectile != WZ_INVALID_BEHAVIOR_ENTITY) {
            const float dt = wz_delta_seconds(facts);
            if (s->proj_launch) {
                s->proj_x = ax; s->proj_y = ay; s->proj_z = az;
                s->proj_dx = nx; s->proj_dy = ny; s->proj_dz = nz;
                s->proj_dist = 0.0f;
                s->proj_launch = 0;
                s->bursting = 0;
            }
            if (s->bursting) {
                // Hold the big bright burst, then shrink + park far below + go dark.
                s->burst_timer -= dt;
                if (s->burst_timer <= 0.0f) {
                    s->bursting = 0;
                    wz_write_set_local_scale(
                        facts, s->projectile, kProjScale, kProjScale, kProjScale);
                    wz_write_set_world_translation(
                        facts, s->projectile, s->impact_x, kProjParkY, s->impact_z);
                    wz_write_set_renderable_param(
                        facts, s->projectile, kIntensity, 0.0f, 0.0f, 0.0f);
                }
            }
            else if (s->proj_active) {
                const float step = kProjSpeed * dt;
                s->proj_x += s->proj_dx * step;
                s->proj_y += s->proj_dy * step;
                s->proj_z += s->proj_dz * step;
                s->proj_dist += step;

                // Struck a tank? (the projectile's own collider recorded where.) Or
                // reached the ground? (terrain range from the launch raycast.)
                uint8_t hit_tank = 0;
                if (auto* p = wz_instance_state_of<projectile_impact::State>(
                        facts, s->projectile, projectile_impact::kModule)) {
                    if (p->hit) {
                        hit_tank = 1;
                        s->impact_x = p->hx; s->impact_y = p->hy; s->impact_z = p->hz;
                    }
                }
                const uint8_t hit_terrain =
                    !hit_tank && s->terrain_valid && s->proj_dist >= s->terrain_dist;
                if (hit_terrain) {
                    s->impact_x = s->terrain_x;
                    s->impact_y = s->terrain_y;
                    s->impact_z = s->terrain_z;
                }

                if (hit_tank || hit_terrain) {
                    // Impact: freeze here and burst (~x10 for a fraction of a second).
                    s->proj_active = 0;
                    s->bursting = 1;
                    s->burst_timer = kBurstSeconds;
                    wz_write_set_world_translation(
                        facts, s->projectile, s->impact_x, s->impact_y, s->impact_z);
                    wz_write_set_local_scale(
                        facts, s->projectile, kBurstScale, kBurstScale, kBurstScale);
                    wz_write_set_renderable_param(
                        facts, s->projectile, kIntensity, 1.0f, 0.0f, 0.0f);
                }
                else if (s->proj_dist >= kProjMaxDist) {
                    s->proj_active = 0;  // out of range: park far below, go dark
                    wz_write_set_world_translation(
                        facts, s->projectile, s->proj_x, kProjParkY, s->proj_z);
                    wz_write_set_renderable_param(
                        facts, s->projectile, kIntensity, 0.0f, 0.0f, 0.0f);
                }
                else {
                    wz_write_set_world_translation(
                        facts, s->projectile, s->proj_x, s->proj_y, s->proj_z);
                    wz_write_set_renderable_param(
                        facts, s->projectile, kIntensity, 1.0f, 0.0f, 0.0f);
                }
            }
        }

        if (!s->firing) {
            return;
        }

        if (!s->boom_done) {
            wz_write_play_sound_named(facts, s->self, "Canon_a");
            s->boom_done = 1;
        }

        // Impact-flash preview: the launch raycast already found where the shot meets
        // the terrain; sit the preview there (dark if the shot cleared the ridge).
        const float dt = wz_delta_seconds(facts);
        const float decay = (kFadeSeconds > 0.0f) ? (dt / kFadeSeconds) : 1.0f;
        s->intensity = (s->intensity > decay) ? (s->intensity - decay) : 0.0f;
        wz_write_set_renderable_param(
            facts, s->flash, kIntensity, s->intensity, 0.0f, 0.0f);
        if (s->beam != WZ_INVALID_BEHAVIOR_ENTITY) {
            wz_write_set_renderable_param(
                facts, s->beam, kIntensity, s->intensity, 0.0f, 0.0f);
        }
        if (s->impact != WZ_INVALID_BEHAVIOR_ENTITY) {
            if (s->terrain_valid) {
                wz_write_set_world_translation(
                    facts, s->impact, s->terrain_x, s->terrain_y, s->terrain_z);
                wz_write_set_local_scale(
                    facts, s->impact, kImpactScale, kImpactScale, kImpactScale);
                wz_write_set_renderable_param(
                    facts, s->impact, kIntensity, s->intensity, 0.0f, 0.0f);
            } else {
                wz_write_set_renderable_param(
                    facts, s->impact, kIntensity, 0.0f, 0.0f, 0.0f);
            }
        }
        if (s->intensity <= 0.0f) {
            s->firing = 0;
        }
    }
}
