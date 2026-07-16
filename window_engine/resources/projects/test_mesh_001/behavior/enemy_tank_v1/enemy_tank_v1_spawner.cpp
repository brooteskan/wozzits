#include <engine/behavior/behavior_module_api.h>

// enemy_tank_v1_spawner -- drops an enemy_tank_v1 tank into the world so you can test
// the new tank without hand-placing it (tanks are spawned, not authored into the tree).
//
// Modeled on spawn_player: it spawns a registered PREFAB (a scenelet) by name, ONCE, at
// scene start. That prefab is the tank body with the enemy_tank_v1 behavior on its root
// -- author it in the editor, then attach THIS behavior to a node (e.g. the scene root)
// and point its `prefab` config at it.
//
// Idempotency: a spawn triggers a behavior-scene rebuild that RESETS this behavior's
// instance state, so a plain "have I spawned?" flag would re-spawn every frame. Like
// spawn_player, we instead key off the spawned tank's PRESENCE -- a node named
// `exists_name`, which survives the rebuild. Once it exists we never spawn again (and if
// it is ever destroyed, we spawn a fresh one -- a simple respawner). IMPORTANT: set
// `exists_name` to a node name that actually appears in the prefab (its root by default),
// or the presence check never matches and the spawner loops.

namespace
{
    struct SpawnState { uint8_t spawned; };

    // Read a string config into `buf`: the authored value if the field was edited,
    // else the default declared in kParams below.
    void read_config_string(
        const WzBehaviorFrameFacts* facts, const char* key, char* buf, uint32_t cap)
    {
        uint32_t required = 0;
        wz_config_string(facts, key, buf, cap, &required);
    }

    void spawner_init(const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        if (SpawnState* s = wz_instance_state<SpawnState>(facts)) {
            s->spawned = 0;
        }
    }

    void spawner_event(
        const WzBehaviorFrameFacts* facts, const WzBehaviorEvent* event, void*)
    {
        if (!facts || !event || wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        SpawnState* s = wz_instance_state<SpawnState>(facts);
        if (!s) {
            return;
        }

        // Defaults are declared ONCE, in kParams below -- wz_config_* falls back to them
        // when a field was never edited, so there is no C++ copy to drift.
        char prefab[64] = "";
        char exists[64] = "";
        read_config_string(facts, "prefab", prefab, sizeof(prefab));
        read_config_string(facts, "exists_name", exists, sizeof(exists));

        // Already in the world? (presence survives the spawn-induced rebuild).
        WzBehaviorEntityId existing = WZ_INVALID_BEHAVIOR_ENTITY;
        if (wz_find_entity_by_name(facts, exists, &existing)
            && existing != WZ_INVALID_BEHAVIOR_ENTITY)
        {
            return;   // it exists -> nothing to do
        }
        if (s->spawned) {
            return;   // spawn is in flight; it materializes next frame
        }

        // Spawn offset in the spawner's frame (== world position when the spawner sits
        // at the origin, e.g. on the scene root). Zeroes are placeholders, never the
        // default: each read lands the authored value or the kParams default.
        float x = 0.0f, y = 0.0f, z = 0.0f;
        (void)wz_config_float(facts, "spawn_x", &x);
        (void)wz_config_float(facts, "spawn_y", &y);
        (void)wz_config_float(facts, "spawn_z", &z);

        wz_write_spawn_prefab(facts, wz_self(event), prefab, x, y, z);
        s->spawned = 1;
        wz_log_infof(
            facts, "[enemy_tank_v1_spawner] spawned prefab '%s' at (%.0f, %.0f, %.0f)",
            prefab, (double)x, (double)y, (double)z);
    }

    const char* kChannels[] = { "frame.update" };

    // Declared params -> the editor renders typed config fields for each (a text box for
    // the prefab name, a number for each coordinate) instead of raw strings.
    const WzBehaviorParamDesc kParams[] = {
        { "prefab",      "Tank prefab",   WZ_BEHAVIOR_PARAM_STRING, 0.0,   "enemy_tank_v1" },
        { "exists_name", "Exists node",   WZ_BEHAVIOR_PARAM_STRING, 0.0,   "enemy_tank_v1" },
        { "spawn_x",     "Spawn X",       WZ_BEHAVIOR_PARAM_FLOAT,  430.0, "" },
        { "spawn_y",     "Spawn Y",       WZ_BEHAVIOR_PARAM_FLOAT,  260.0, "" },
        { "spawn_z",     "Spawn Z",       WZ_BEHAVIOR_PARAM_FLOAT,  500.0, "" },
    };
}

WZ_BEHAVIOR_MODULE_INIT_PARAMS(
    "enemy_tank_v1_spawner", spawner_init, spawner_event, kChannels, kParams)
