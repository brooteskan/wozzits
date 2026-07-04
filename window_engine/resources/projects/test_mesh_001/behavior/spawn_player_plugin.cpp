// spawn_player_plugin.cpp
//
// Spawns the "tank" (player) prefab once at scene start and makes its camera the
// active view. The player tank is no longer authored into the scene tree -- put
// this behavior on a top-level node (e.g. the scene root) and it drops the player
// in at init, then points the runtime camera at the spawned tank's "camera" child.
//
// Why a dedicated behavior (not prefab_spawner): prefab_spawner fires on a key
// press, and the camera activation matters. The spawn command is applied at the
// next frame boundary, so the spawned camera does not exist yet during the same
// frame -- we therefore retry the activation each frame until the camera appears.
// If the spawn ever fails, the engine's free-fly camera stays active, so a hiccup
// degrades to "you can still fly around", never a black screen.

#include <engine/behavior/behavior_module_api.h>

namespace
{
    // Where the player drops in (world; the spawner node is the scene root at the
    // origin, so the offset IS the world position). Y is a little above the terrain
    // -- the tank's terrain-constrained motion settles it onto the surface.
    constexpr float kSpawnX = 394.0f;
    constexpr float kSpawnY = 260.0f;
    constexpr float kSpawnZ = 556.0f;

    struct SpawnPlayerState
    {
        uint8_t spawned;
        uint8_t camera_active;
    };

    void spawn_player_init(
        const WzBehaviorInitFacts* facts, WzBehaviorEntityId, void*)
    {
        SpawnPlayerState* s = wz_instance_state<SpawnPlayerState>(facts);
        if (s) {
            s->spawned = 0;
            s->camera_active = 0;
        }
    }

    void spawn_player_event(
        const WzBehaviorFrameFacts* facts, const WzBehaviorEvent* event, void*)
    {
        if (!facts || !event || wz_event_kind(event) != WZ_EVENT_FRAME_UPDATE) {
            return;
        }
        SpawnPlayerState* s =
            static_cast<SpawnPlayerState*>(wz_get_instance_state(facts));
        if (!s) {
            return;
        }

        // IDEMPOTENT spawn: only spawn if the player ("tank") does not already
        // exist. A successful spawn triggers a behavior-scene rebuild that resets
        // this behavior's instance state, so a plain "have I spawned?" flag would
        // re-spawn every frame -- an infinite spawn -> rebuild -> re-resolve loop.
        // Keying off the player's PRESENCE (which persists across the rebuild)
        // breaks that: once the tank exists we never spawn again.
        WzBehaviorEntityId player = WZ_INVALID_BEHAVIOR_ENTITY;
        const uint8_t have_player =
            wz_find_entity_by_name(facts, "tank", &player)
            && player != WZ_INVALID_BEHAVIOR_ENTITY;

        if (!have_player) {
            if (!s->spawned) {
                float x = kSpawnX, y = kSpawnY, z = kSpawnZ;
                (void)wz_config_float(facts, "spawn_x", &x);
                (void)wz_config_float(facts, "spawn_y", &y);
                (void)wz_config_float(facts, "spawn_z", &z);
                wz_write_spawn_prefab(facts, wz_self(event), "tank", x, y, z);
                s->spawned = 1;
                wz_log_info(facts, "[spawn_player] spawned tank prefab");
            }
            return;  // wait for the player to materialize
        }

        // The tank exists -> point the runtime camera at its "camera" child (names
        // survive the spawn's id remapping). Once is enough.
        if (!s->camera_active && facts->write_command) {
            WzBehaviorEntityId cam = WZ_INVALID_BEHAVIOR_ENTITY;
            if (wz_find_entity_by_name(facts, "camera", &cam)
                && cam != WZ_INVALID_BEHAVIOR_ENTITY)
            {
                const WzBehaviorCommand cmd = {
                    cam,
                    WZ_BEHAVIOR_COMMAND_SET_ACTIVE_CAMERA,
                    { 0.0f, 0.0f, 0.0f, 0.0f },
                };
                facts->write_command(facts->command_writer_user, &cmd);
                s->camera_active = 1;
                wz_log_info(facts, "[spawn_player] activated player camera");
            }
        }
    }

    const char* kEvents[] = { "frame.update" };
}

WZ_BEHAVIOR_MODULE_INIT(
    "spawn_player",
    spawn_player_init,
    spawn_player_event,
    kEvents)
