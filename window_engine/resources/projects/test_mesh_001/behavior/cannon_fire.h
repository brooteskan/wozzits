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
        // 1 = the GLB muzzle Empty resolved (exact anchor); 0 = fell back to the
        // gun+turret heuristic (marker missing / GLB not re-imported / name typo).
        wz_log_infof(facts, "[cannon] find muzzle marker: %u",
            (unsigned)(s->muzzle != WZ_INVALID_BEHAVIOR_ENTITY));
        s->intensity = 0.0f;
        s->firing = 0;
        s->boom_done = 1;
    }

    // FIRE. Flash the muzzle + play the report. Call from a trigger (controller
    // button, AI shot). Idempotent-ish: a re-fire restarts the flash.
    inline void fire(State* s)
    {
        s->intensity = 1.0f;
        s->firing = 1;
        s->boom_done = 0;
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
                const float half = 0.5f * kBeamDistance;
                wz_write_set_world_translation(
                    facts, s->beam,
                    ax + nx * half, ay + ny * half, az + nz * half);
                wz_write_set_local_rotation(facts, s->beam, q);
                wz_write_set_local_scale(
                    facts, s->beam, half, 0.5f * kBeamThick, 0.5f * kBeamThick);
            }
        }

        if (!s->firing) {
            return;
        }

        if (!s->boom_done) {
            wz_write_play_sound_named(facts, s->self, "Canon_a");
            s->boom_done = 1;
        }

        // Impact flash: raycast the shot from the muzzle along the firing axis to
        // the terrain and sit the impact where it lands. No hit (fired over a ridge
        // into the sky) -> the impact stays dark.
        uint8_t impact_hit = 0;
        WzVec3 impact_pos{};
        if (s->impact != WZ_INVALID_BEHAVIOR_ENTITY
            && s->terrain != WZ_INVALID_BEHAVIOR_ENTITY)
        {
            const WzVec3 origin{ ax, ay, az };
            const WzVec3 dir{ nx, ny, nz };
            WzSurfaceSample surf{};
            if (wz_query_collision_surface_ray(
                    facts, s->terrain, origin, dir, kImpactMaxDist, &surf)
                && surf.hit)
            {
                impact_pos = surf.position;
                impact_hit = 1;
            }
        }

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
            if (impact_hit) {
                wz_write_set_world_translation(
                    facts, s->impact, impact_pos.x, impact_pos.y, impact_pos.z);
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
