// cannon_flash_plugin.cpp
//
// Prefab-internal "cannon flash" for an enemy tank. Lives on the enemy_tank
// scenelet root and drives that tank's OWN flash children (resolved by
// descendant lookup, so every spawned instance drives only itself). On a timer:
//
//   1. MUZZLE: a bright beam (a long thin box) from the barrel out along the aim
//      direction to `beam_distance` units. The aim is the horizontal direction to
//      the target (the turret tracks the target), so the beam follows the gun.
//   2. IMPACT (after `impact_delay`): a flash either at the beam's end in space
//      (impact_mode 0) or at where the beam meets the terrain (impact_mode 1),
//      found by marching the ray and sampling terrain height.
//   3. BOOM (after `boom_delay`): "Canon_b" on the tank's own AudioSource.
//
// All authoring on shipped seams: SET_RENDERABLE_PARAM (#232) for the fade,
// transform writes to place/aim the beam, wz_sample_terrain_surface for the
// hit, the audio PLAY command. No engine changes. Every knob below is read from
// the behavior's scene config (so you can tune per node without a rebuild) and
// falls back to the constexpr default.

#include <engine/behavior/behavior_module_api.h>

#include <math.h>

namespace
{
    // ---- Defaults (override per node via the behavior's "config" block) ----
    constexpr float kFireInterval = 3.0f;   // seconds between shots (auto-fire)
    constexpr float kBeamDistance = 40.0f;  // beam length out the barrel (world units)
    constexpr float kBeamThick    = 0.35f;  // beam cross-section (world units)
    constexpr float kBarrelHeight = 2.2f;   // muzzle height above the tank origin
    constexpr float kBarrelLength = 1.5f;   // muzzle forward offset from the gun mount
    constexpr float kImpactDelay  = 0.22f;  // muzzle -> impact ("almost immediately")
    constexpr float kBoomDelay    = 1.30f;  // muzzle -> boom (speed-of-sound feel)
    constexpr float kFadeSeconds  = 0.10f;  // how fast each flash fades (bright -> 0)
    constexpr float kBeamYawOffset = 0.0f;  // radians; correct the aim if it reads off
    constexpr float kImpactOnTerrain = 1.0f; // 1 = flash on terrain, 0 = flash in space

    constexpr float kPi = 3.14159265358979f;
    constexpr int   kMarchSamples = 48;     // terrain-intersection ray march steps

    constexpr uint32_t kIntensity = wz_renderable_param_hash("intensity");

    struct CannonFlashState
    {
        WzBehaviorEntityId self;
        WzBehaviorEntityId target;   // who we aim at (player tank)
        WzBehaviorEntityId terrain;  // clipmap landscape (for the terrain hit)
        WzBehaviorEntityId turret;   // the gun mount -- the beam's anchor
        WzBehaviorEntityId beam;     // "muzzle_beam" child (the line)
        WzBehaviorEntityId impact;   // "impact_flash" child (the hit)

        // Config (read once at init).
        float fire_interval, beam_distance, beam_thick, barrel_height, barrel_length;
        float impact_delay, boom_delay, fade_seconds, beam_yaw_offset, impact_on_terrain;

        float   t;          // seconds since the last muzzle fire
        float   muzzle_i;   // current beam intensity (fades to 0)
        float   impact_i;   // current impact intensity (fades to 0)
        uint8_t impact_done;
        uint8_t boom_done;
    };

    inline float wrap_pi(float a)
    {
        while (a > kPi) { a -= 2.0f * kPi; }
        while (a < -kPi) { a += 2.0f * kPi; }
        return a;
    }

    void push_intensity(
        const WzBehaviorFrameFacts* facts, WzBehaviorEntityId e, float v)
    {
        if (e != WZ_INVALID_BEHAVIOR_ENTITY) {
            wz_write_set_renderable_param(facts, e, kIntensity, v, 0.0f, 0.0f);
        }
    }

    void cannon_flash_init(
        const WzBehaviorInitFacts* facts, WzBehaviorEntityId self, void*)
    {
        CannonFlashState* s = wz_instance_state<CannonFlashState>(facts);
        if (!s) {
            return;
        }
        s->self = self;
        s->target = WZ_INVALID_BEHAVIOR_ENTITY;
        s->terrain = WZ_INVALID_BEHAVIOR_ENTITY;
        s->turret = WZ_INVALID_BEHAVIOR_ENTITY;
        s->beam = WZ_INVALID_BEHAVIOR_ENTITY;
        s->impact = WZ_INVALID_BEHAVIOR_ENTITY;

        // Aim + landscape (same references the quantum_tank_agent uses). The player
        // is a spawned prefab, so find it by its unique NAME "tank"; on the player
        // itself this resolves to self and the aim falls back to hull-forward.
        wz_find_entity_by_name(facts, "tank", &s->target);
        wz_find_entity_by_authored_id(facts, "clipmap_landscape", &s->terrain);
        // Our OWN gun mount + flash children -- descendant lookup is instance-safe,
        // so a second spawned enemy drives its own beam, not the first one's.
        wz_find_descendant_by_name(facts, self, "turret", &s->turret);
        wz_find_descendant_by_name(facts, self, "muzzle_beam", &s->beam);
        wz_find_descendant_by_name(facts, self, "impact_flash", &s->impact);

        s->fire_interval = kFireInterval;
        s->beam_distance = kBeamDistance;
        s->beam_thick = kBeamThick;
        s->barrel_height = kBarrelHeight;
        s->barrel_length = kBarrelLength;
        s->impact_delay = kImpactDelay;
        s->boom_delay = kBoomDelay;
        s->fade_seconds = kFadeSeconds;
        s->beam_yaw_offset = kBeamYawOffset;
        s->impact_on_terrain = kImpactOnTerrain;
        (void)wz_config_float(facts, "fire_interval", &s->fire_interval);
        (void)wz_config_float(facts, "beam_distance", &s->beam_distance);
        (void)wz_config_float(facts, "beam_thick", &s->beam_thick);
        (void)wz_config_float(facts, "barrel_height", &s->barrel_height);
        (void)wz_config_float(facts, "barrel_length", &s->barrel_length);
        (void)wz_config_float(facts, "impact_delay", &s->impact_delay);
        (void)wz_config_float(facts, "boom_delay", &s->boom_delay);
        (void)wz_config_float(facts, "fade_seconds", &s->fade_seconds);
        (void)wz_config_float(facts, "beam_yaw_offset", &s->beam_yaw_offset);
        (void)wz_config_float(facts, "impact_on_terrain", &s->impact_on_terrain);

        s->t = s->fire_interval;   // fire on the first frame
        s->muzzle_i = 0.0f;
        s->impact_i = 0.0f;
        s->impact_done = 1;
        s->boom_done = 1;
        wz_log_infof(
            facts, "[cannon_flash] init self=%u target=%u beam=%u impact=%u",
            s->self, s->target, s->beam, s->impact);
    }

    void cannon_flash_event(
        const WzBehaviorFrameFacts* facts, const WzBehaviorEvent* event, void*)
    {
        if (!facts || !event || wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        CannonFlashState* s =
            static_cast<CannonFlashState*>(wz_get_instance_state(facts));
        if (!s) {
            return;
        }

        // --- Read our position + the aim direction (horizontal, toward target). ---
        WzMat4 self_w{};
        WzVec3 tpos{};
        const uint8_t have_self = wz_read_world_transform(facts, s->self, &self_w);
        const uint8_t have_tgt =
            wz_read_world_position(facts, s->target, &tpos);
        if (!have_self) {
            return;
        }
        const float px = self_w.m[12], py = self_w.m[13], pz = self_w.m[14];
        const float hull_yaw = atan2f(-self_w.m[2], self_w.m[0]);  // column-0 forward
        // Aim at a DISTINCT target if we have one; else straight out the hull
        // forward. On the player the "target" resolves to itself, so it falls back
        // to hull-forward -- the beam points where the tank faces.
        const uint8_t aim_at_target = have_tgt && s->target != s->self;
        const float aim_yaw = aim_at_target
            ? atan2f(-(tpos.z - pz), (tpos.x - px))
            : hull_yaw;
        // Horizontal unit forward in the atan2(-z, x) convention.
        const float fx = cosf(aim_yaw);
        const float fz = -sinf(aim_yaw);

        // Gun mount = the turret's world position, so the beam starts AT the gun
        // rather than a guessed height over the tank. Fall back to a height over the
        // tank origin if there is no turret node.
        float gx = px, gy = py + s->barrel_height, gz = pz;
        WzMat4 turret_w{};
        if (s->turret != WZ_INVALID_BEHAVIOR_ENTITY
            && wz_read_world_transform(facts, s->turret, &turret_w))
        {
            gx = turret_w.m[12];
            gy = turret_w.m[13];
            gz = turret_w.m[14];
        }
        // Muzzle = a little forward of the gun mount along the aim.
        const float bx = gx + fx * s->barrel_length;
        const float by = gy;
        const float bz = gz + fz * s->barrel_length;

        // --- Place + aim the beam (a long thin box along +X, yawed to the aim). ---
        if (s->beam != WZ_INVALID_BEHAVIOR_ENTITY) {
            const float half = 0.5f * s->beam_distance;
            wz_write_set_world_translation(
                facts, s->beam, bx + fx * half, by, bz + fz * half);
            const float yaw =
                wrap_pi(aim_yaw - hull_yaw + s->beam_yaw_offset);  // local to hull
            const float h = yaw * 0.5f;
            const WzQuaternion q{ 0.0f, sinf(h), 0.0f, cosf(h) };  // yaw about +Y
            wz_write_set_local_rotation(facts, s->beam, q);
            // The cube spans -1..1, so a scale is the HALF-extent: half the beam
            // length along +X (centered above), half the thickness on Y/Z.
            wz_write_set_local_scale(
                facts, s->beam, half, 0.5f * s->beam_thick, 0.5f * s->beam_thick);
        }

        // --- Timer / sequence. ---
        const float dt = wz_delta_seconds(facts);
        s->t += dt;

        const float decay = (s->fade_seconds > 0.0f) ? (dt / s->fade_seconds) : 1.0f;
        if (s->muzzle_i > 0.0f) {
            s->muzzle_i = (s->muzzle_i > decay) ? (s->muzzle_i - decay) : 0.0f;
            push_intensity(facts, s->beam, s->muzzle_i);
        }
        if (s->impact_i > 0.0f) {
            s->impact_i = (s->impact_i > decay) ? (s->impact_i - decay) : 0.0f;
            push_intensity(facts, s->impact, s->impact_i);
        }

        // IMPACT: place the flash at the beam end (in space) or where the ray meets
        // the terrain, then light it.
        if (!s->impact_done && s->t >= s->impact_delay) {
            float ix = bx + fx * s->beam_distance;
            float iy = by;
            float iz = bz + fz * s->beam_distance;
            if (s->impact_on_terrain > 0.5f
                && s->terrain != WZ_INVALID_BEHAVIOR_ENTITY)
            {
                // March the (near-horizontal) ray; the terrain "hits" the beam
                // where the ground first rises to the beam height.
                for (int i = 1; i <= kMarchSamples; ++i) {
                    const float march =
                        (static_cast<float>(i) / kMarchSamples) * s->beam_distance;
                    const float sx = bx + fx * march;
                    const float sz = bz + fz * march;
                    WzSurfaceSample g{};
                    if (wz_sample_terrain_surface(facts, s->terrain, sx, sz, &g)
                        && g.hit && g.position.y >= by)
                    {
                        ix = sx;
                        iy = g.position.y;
                        iz = sz;
                        break;
                    }
                }
            }
            if (s->impact != WZ_INVALID_BEHAVIOR_ENTITY) {
                wz_write_set_world_translation(facts, s->impact, ix, iy, iz);
            }
            s->impact_i = 1.0f;
            push_intensity(facts, s->impact, s->impact_i);
            s->impact_done = 1;
        }

        // BOOM: on our own AudioSource, delayed by the speed-of-sound feel.
        if (!s->boom_done && s->t >= s->boom_delay) {
            wz_write_play_sound_named(facts, s->self, "Canon_b");
            s->boom_done = 1;
        }

        // NEXT SHOT.
        if (s->t >= s->fire_interval) {
            s->t = 0.0f;
            s->muzzle_i = 1.0f;
            push_intensity(facts, s->beam, s->muzzle_i);
            s->impact_done = 0;
            s->boom_done = 0;
        }
    }

    const char* kEvents[] = { "frame.update" };
}

WZ_BEHAVIOR_MODULE_INIT(
    "cannon_flash",
    cannon_flash_init,
    cannon_flash_event,
    kEvents)
