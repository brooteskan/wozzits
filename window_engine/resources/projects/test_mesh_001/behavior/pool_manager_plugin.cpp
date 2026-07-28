#include <engine/behavior/behavior_module_api.h>
#include <engine/behavior/behavior_plugin_abi.h>

#include "squad_deploy.h"

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
    constexpr double      kPrewarmCooldown = 0.5;  // seconds between LAZY prewarm spawns, so
                                                   // each spawn's O(scene) rebuild lands on a
                                                   // different frame -- no single load spike

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
        int      submitted_count;     // parked spawns SUBMITTED so far (lazy prewarm progress)
        int      ready_count;         // completions recorded (submitted_count catches up to this)
        int      live_count;          // currently-live instances (== count of slot_deployed)
        int      deploy_cursor;       // round-robin start for the next deploy (rotate the pool)
        double   next_prewarm_time;   // sim_time the next lazy prewarm may submit
        double   next_deploy_time;
    };

    static const char* kPoolEvents[] = {
        "self.start",
        "spawn.completed",
        "spawn.failed",     // else the SPAWN_FAILED case below is dead -- a failed
                            // prewarm would strand its slot with no diagnostic
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
        // The command -> pool deploy queue (get-or-create): the commander's reinforce
        // decision posts here, this pool consumes it. See squad_deploy.h.
        (void)wz_create_shared_state(
            facts, squad_deploy::kKey,
            sizeof(squad_deploy::Queue), alignof(squad_deploy::Queue));
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
            if (state->pool_size > 0) {
                return;   // already initialized (self.start fires once)
            }
            state->pool_size =
                kPoolSize < kMaxPoolSlots ? kPoolSize : kMaxPoolSlots;
            state->next_prewarm_time = 0.0;   // begin the lazy prewarm next frame
            // The prewarm spawns are submitted ONE PER kPrewarmCooldown in
            // frame.update (below), not all here -- spreading each spawn's O(scene)
            // rebuild across frames so there is no single load spike.
            wz_log_infof(
                facts,
                "[pool] lazily prewarming %d x '%s' (1 / %.1fs, parked)",
                state->pool_size, kPoolPrefab,
                static_cast<double>(kPrewarmCooldown));
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
            // A prewarm spawn failed: its slot never becomes ready, so the pool runs
            // with one fewer reserve (if enough fail, the squad can't reach strength).
            // The lazy prewarm counts it as submitted and moves on -- it is NOT retried,
            // so this log is the only signal. Loud so a permanently-bad prefab (all
            // spawns failing -> no enemy ever deploys) is obvious rather than silent.
            wz_log_errorf(
                facts,
                "[pool] prewarm spawn FAILED (tag %llu) -- reserve slot lost, "
                "squad may run under strength",
                static_cast<unsigned long long>(wz_spawn_event_request_tag(facts)));
            return;
        }

        case WZ_EVENT_FRAME_UPDATE:
        {
            const double now = wz_sim_time(facts);

            // The command -> pool deploy queue. We publish our live_count into it (see
            // below) so the commander gates on a count consistent with `pending`.
            squad_deploy::Queue* deploy_q = static_cast<squad_deploy::Queue*>(
                wz_find_shared_state(facts, squad_deploy::kKey));

            // LAZY PREWARM: submit one parked spawn per kPrewarmCooldown until the pool
            // is full, so the O(scene) rebuild each spawn triggers lands on a DIFFERENT
            // frame instead of stacking into one load spike. The reserves trickle in
            // behind the initial squad; deploys (paced slower) are always fed.
            if (state->submitted_count < state->pool_size
                && now >= state->next_prewarm_time)
            {
                WzSpawnPrefabRequest req{};
                req.spawner = wz_self(event);
                req.prefab_name_hash = wz_prefab_hash(kPoolPrefab);
                req.offset[0] = 0.0f;
                req.offset[1] = 0.0f;
                req.offset[2] = 0.0f;
                req.request_tag = static_cast<uint64_t>(state->submitted_count);
                req.spawn_parked = 1u;   // materialize INACTIVE -- no dispatch/collision
                WzSpawnTicket ticket{};
                if (wz_submit_spawn_prefab(facts, &req, &ticket)) {
                    state->submitted_count++;
                    state->next_prewarm_time = now + kPrewarmCooldown;
                    wz_log_infof(
                        facts, "[pool] prewarm %d/%d submitted (parked)",
                        state->submitted_count, state->pool_size);
                }
            }

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
                    if (deploy_q) { deploy_q->live = state->live_count; }
                    wz_log_infof(
                        facts, "[pool] recycle slot %d <- %s (%d/%d live)",
                        i, state->slot_id[i], state->live_count, kActiveTarget);
                }
            }

            // COMMAND-DRIVEN DEPLOY: unpark a reserve only when the COMMANDER has
            // posted a reinforce order to the deploy queue -- bringing a tank up is the
            // commander's decision now, not an automatic top-up. kActiveTarget stays a
            // hard CAP, and the cooldown still paces successive deploys.
            if (state->live_count >= kActiveTarget) {
                return;   // at strength cap
            }
            if (!deploy_q || deploy_q->pending <= 0) {
                return;   // no reinforce order outstanding
            }
            if (state->ready_count <= 0) {
                return;   // nothing prewarmed yet
            }
            if (now < state->next_deploy_time) {
                return;   // still on cooldown
            }

            // Pick the next available slot ROTATING from deploy_cursor, so a redeploy
            // prefers a fresh reserve (ahead of the cursor) over the slot that just
            // recycled (which now sits BEHIND it). Wear spreads evenly across the whole
            // pool instead of thrashing the low indices; a recycled slot is reused only
            // after every fresher one has had its turn.
            int chosen = -1;
            for (int n = 0; n < state->pool_size; ++n) {
                const int i = (state->deploy_cursor + n) % state->pool_size;
                if (state->slot_ready[i] && !state->slot_deployed[i]) {
                    chosen = i;
                    break;
                }
            }
            if (chosen < 0) {
                return;   // nothing available (all live or not yet prewarmed)
            }

            WzBehaviorEntityId handle =
                (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY;
            if (!wz_find_entity_by_authored_id(
                    facts, state->slot_id[chosen], &handle)
                || handle == (WzBehaviorEntityId)WZ_INVALID_BEHAVIOR_ENTITY)
            {
                // Not resolvable this frame (mid-rebuild); try again next frame.
                return;
            }

            const float lateral =
                (static_cast<float>(state->live_count)
                    - 0.5f * static_cast<float>(kActiveTarget - 1))
                * kDeploySpread;

            // HQ (this pool node) world position: the deploy fan is placed RELATIVE
            // to HQ. A deployed instance is a TOP-LEVEL spawn node, so its local ==
            // world -- setting a bare local (lateral, 0, ahead) would land the fan at
            // the WORLD ORIGIN, not at HQ (the "spawn at 0,0" bug). Add HQ's world.
            WzVec3 hq{};
            (void)wz_read_world_position(facts, wz_self(event), &hq);

            // The deploy: unpark (fires the tank's self.activated -> claim lease +
            // reset), show, and place it at HQ + fan. Three cheap field writes -- no
            // spawn, no rebuild.
            wz_write_set_active(facts, handle, 1u);
            wz_write_set_visible(facts, handle, 1u);
            wz_write_set_world_translation(
                facts, handle,
                hq.x + lateral, hq.y, hq.z + kDeployAhead);

            // Move one unit from pending -> live ATOMICALLY (same step) so the
            // commander, reading (live + pending), never sees the deploy uncounted.
            deploy_q->pending -= 1;   // consume the commander's reinforce order
            state->slot_deployed[chosen] = 1u;
            state->live_count++;
            deploy_q->live = state->live_count;
            state->deploy_cursor = (chosen + 1) % state->pool_size;
            state->next_deploy_time = now + kDeployCooldown;
            wz_log_infof(
                facts,
                "[pool] deploy slot %d -> %s (%d/%d live) at (%.1f, 0, %.1f)  UNPARK",
                chosen, state->slot_id[chosen],
                state->live_count, kActiveTarget,
                static_cast<double>(lateral),
                static_cast<double>(kDeployAhead));
            return;   // one deploy per cooldown
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
