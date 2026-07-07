#include <engine/behavior/behavior_module_api.h>
#include <engine/behavior/behavior_plugin_abi.h>

// pool_manager -- a self-driving instance pool (the #252 prewarm-and-park model).
//
// It replaces the commander's per-reinforcement fresh SPAWN (each an O(scene)
// rebuild -- 12-15 ms for the frame-1 double-spawn, see the pooling baseline) with
// a PREWARM once at load + cheap UNPARK at deploy time. On self.start it submits
// pool_size PARKED spawns of `prefab`; on each spawn.completed it hides the parked
// instance and records its STABLE authored id; on frame.update it deploys the pool
// one instance per cooldown by re-resolving that id to a live handle and flipping
// active+visible+position. Deploying is a SET_NODE_ACTIVE flip -- no graft, no
// resolve_all, no rebuild_behavior_scene. The prewarm spawns pay the rebuild cost
// ONCE at load, off the gameplay hot path.
//
// Handle stability: runtime handles renumber on every rebuild, so the pool keeps
// the stable authored id ("spawn:N:1") from the completion event and re-resolves it
// with wz_find_entity_by_authored_id at each deploy -- never a cached handle.
//
// v1 scope: pool_size == the squad target (3), so it also respects the commander's
// group-agent 3-member qubit cap; a larger pool (with a slot free-list + death->park
// recycling) is the natural follow-up. Deployment cadence/fan mirror the baseline's
// reinforce knobs so the before/after is apples to apples.

namespace
{
    // Pool config (set here, read once at prewarm). Kept local so the module is
    // self-contained and generic -- point kPoolPrefab at any registered prefab.
    constexpr const char* kPoolPrefab = "enemy_tank";
    constexpr int         kPoolSize = 5;        // prewarmed instances (reserves included)
    constexpr int         kActiveTarget = 3;    // how many to keep LIVE at once (<= squad
                                                // lease slots); spares stay parked until a
                                                // live one dies + recycles
    constexpr double      kDeployCooldown = 4.0;   // seconds between deploys (baseline cadence)
    constexpr float       kDeploySpread = 8.0f;    // lateral fan between live slots
    constexpr float       kDeployAhead = 12.0f;    // forward offset from HQ (the pool's node)

    constexpr int      kMaxPoolSlots = 8;   // fixed storage cap (v1: pool_size <= this)
    constexpr unsigned kSlotIdLen = 40;     // "spawn:NNN:ROOT" + slack

    // Instance state (POD, preserved across the structural rebuilds each prewarm
    // spawn triggers -- so prewarm survives the very rebuilds it causes).
    struct PoolManagerState
    {
        char     slot_id[kMaxPoolSlots][kSlotIdLen];  // stable authored id per slot
        uint8_t  slot_ready[kMaxPoolSlots];           // 1 once completion recorded + hidden
        uint8_t  slot_deployed[kMaxPoolSlots];        // 1 while LIVE (toggles: deploy/recycle)
        int      pool_size;
        int      ready_count;
        int      live_count;          // currently-live instances (== count of slot_deployed)
        double   next_deploy_time;
        uint8_t  prewarm_submitted;
    };

    static const char* kPoolEvents[] = {
        "self.start",
        "spawn.completed",
        "frame.update",
    };

    void pool_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId,
        void*)
    {
        // Just materialize the state block (constructed zeroed on first alloc, then
        // returned AS-IS across rebuilds). The prewarm runs on self.start, which
        // fires once when the (authored-active) pool node goes live.
        (void)wz_instance_state<PoolManagerState>(facts);
    }

    void pool_on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }
        PoolManagerState* state = wz_instance_state<PoolManagerState>(facts);
        if (!state) {
            return;
        }

        switch (wz_event_kind(event)) {

        case WZ_EVENT_SELF_START:
        {
            if (state->prewarm_submitted) {
                return;   // once per pool lifetime
            }
            state->prewarm_submitted = 1u;
            state->pool_size =
                kPoolSize < kMaxPoolSlots ? kPoolSize : kMaxPoolSlots;

            for (int i = 0; i < state->pool_size; ++i) {
                WzSpawnPrefabRequest req{};
                req.spawner = wz_self(event);
                req.prefab_name_hash = wz_prefab_hash(kPoolPrefab);
                req.offset[0] = 0.0f;
                req.offset[1] = 0.0f;
                req.offset[2] = 0.0f;
                req.request_tag = static_cast<uint64_t>(i);
                req.spawn_parked = 1u;   // materialize INACTIVE -- no dispatch/collision
                WzSpawnTicket ticket{};
                (void)wz_submit_spawn_prefab(facts, &req, &ticket);
            }
            wz_log_infof(
                facts, "[pool] prewarm %d x '%s' (parked)",
                state->pool_size, kPoolPrefab);
            return;
        }

        case WZ_EVENT_SPAWN_COMPLETED:
        {
            const uint64_t tag = wz_spawn_event_request_tag(facts);
            const char* id = wz_spawn_event_root_authored_id(facts);
            if (tag >= static_cast<uint64_t>(state->pool_size) || !id) {
                return;
            }
            const int slot = static_cast<int>(tag);
            if (state->slot_ready[slot]) {
                return;   // idempotent
            }
            // Copy the STABLE id (the event's pointer is only valid during dispatch).
            unsigned n = 0u;
            for (; n + 1u < kSlotIdLen && id[n]; ++n) {
                state->slot_id[slot][n] = id[n];
            }
            state->slot_id[slot][n] = '\0';
            state->slot_ready[slot] = 1u;
            state->ready_count++;

            // Hide the parked instance: `parked` freezes dispatch + collision but a
            // node still DRAWS, so a prewarmed pool would otherwise show a stack of
            // frozen tanks at HQ until deployed.
            const WzBehaviorEntityId root = wz_spawn_event_root(facts);
            if (root != (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY) {
                wz_write_set_visible(facts, root, 0u);
            }
            wz_log_infof(
                facts, "[pool] slot %d ready: %s (parked + hidden) [%d/%d]",
                slot, state->slot_id[slot], state->ready_count, state->pool_size);
            return;
        }

        case WZ_EVENT_SPAWN_FAILED:
        {
            wz_log_infof(
                facts, "[pool] prewarm spawn FAILED (tag %llu)",
                static_cast<unsigned long long>(wz_spawn_event_request_tag(facts)));
            return;
        }

        case WZ_EVENT_FRAME_UPDATE:
        {
            // RECYCLE sweep: a live instance that died PARKED itself (its effective
            // active went 0). Reclaim its slot so a spare can take its place. Only
            // acts on a resolvable handle reading inactive -- a transient resolve miss
            // is skipped, not mistaken for a death.
            for (int i = 0; i < state->pool_size; ++i) {
                if (!state->slot_deployed[i]) {
                    continue;
                }
                WzBehaviorEntityId handle =
                    (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY;
                if (wz_find_entity_by_authored_id(facts, state->slot_id[i], &handle)
                    && handle != (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY
                    && !wz_node_active(facts, handle))
                {
                    state->slot_deployed[i] = 0u;
                    state->live_count--;
                    wz_log_infof(
                        facts, "[pool] recycle slot %d <- %s (%d/%d live)",
                        i, state->slot_id[i], state->live_count, kActiveTarget);
                }
            }

            // DEPLOY to keep kActiveTarget live, paced by the cooldown.
            if (state->live_count >= kActiveTarget) {
                return;   // squad at strength
            }
            if (state->ready_count <= 0) {
                return;   // nothing prewarmed yet
            }
            const double now = wz_sim_time(facts);
            if (now < state->next_deploy_time) {
                return;   // still on cooldown
            }

            for (int i = 0; i < state->pool_size; ++i) {
                if (!state->slot_ready[i] || state->slot_deployed[i]) {
                    continue;   // not prewarmed, or already live
                }
                WzBehaviorEntityId handle =
                    (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY;
                if (!wz_find_entity_by_authored_id(
                        facts, state->slot_id[i], &handle)
                    || handle == (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY)
                {
                    // Not resolvable this frame (mid-rebuild); try again next frame.
                    return;
                }

                const float lateral =
                    (static_cast<float>(state->live_count)
                        - 0.5f * static_cast<float>(kActiveTarget - 1))
                    * kDeploySpread;

                // The deploy: unpark (fires the tank's self.activated -> claim lease +
                // reset), show, and place it at HQ + fan. Three cheap field writes --
                // no spawn, no rebuild.
                wz_write_set_active(facts, handle, 1u);
                wz_write_set_visible(facts, handle, 1u);
                wz_write_set_local_translation(
                    facts, handle, lateral, 0.0f, kDeployAhead);

                state->slot_deployed[i] = 1u;
                state->live_count++;
                state->next_deploy_time = now + kDeployCooldown;
                wz_log_infof(
                    facts,
                    "[pool] deploy slot %d -> %s (%d/%d live) at (%.1f, 0, %.1f)  UNPARK",
                    i, state->slot_id[i],
                    state->live_count, kActiveTarget,
                    static_cast<double>(lateral),
                    static_cast<double>(kDeployAhead));
                return;   // one deploy per cooldown
            }
            return;
        }

        default:
            return;
        }
    }
}

WZ_BEHAVIOR_MODULE_INIT(
    "pool_manager",
    pool_init,
    pool_on_event,
    kPoolEvents)
