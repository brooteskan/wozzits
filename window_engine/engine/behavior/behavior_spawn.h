#pragma once

// engine/behavior/behavior_spawn.h
//
// Spawn-with-identity buffer (#252 pooling). A behavior calls
// wz_submit_spawn_prefab (facts.submit_spawn_prefab) to enqueue a spawn and get a
// rebuild-stable ticket back; the host drains this buffer at the frame boundary
// (spawning mid-dispatch would renumber the runtime ids the rest of the dispatch
// addresses), materializes each via the incremental spawn path, and fires
// WZ_EVENT_SPAWN_COMPLETED / _FAILED to the spawner. The spawner is stored as its
// STABLE authored id (resolved at submit time) so it survives the rebuild each
// spawn triggers. Mirrors BehaviorGpuComputeBuffer's submit/next-id idiom.

#include <engine/behavior/behavior_plugin_abi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::behavior
{
    struct BehaviorSpawnRequest
    {
        WzSpawnTicket ticket{};
        std::string   spawner_id;          // STABLE authored id (survives rebuild)
        uint32_t      prefab_name_hash = 0u;
        float         offset[3]{ 0.0f, 0.0f, 0.0f };
        uint64_t      request_tag = 0u;
        uint8_t       spawn_parked = 0u;
    };

    struct BehaviorSpawnBuffer
    {
        std::vector<BehaviorSpawnRequest> requests;
        uint64_t next_ticket = 1u;

        void clear() { requests.clear(); }

        // Enqueue a request, allocating its ticket. `spawner_id` is the caller's
        // STABLE authored id (the host resolves it before calling). Rejects a
        // request with no prefab hash (returns ticket 0 = not enqueued).
        [[nodiscard]] WzSpawnTicket submit(
            std::string spawner_id,
            const WzSpawnPrefabRequest& desc)
        {
            if (desc.prefab_name_hash == 0u) {
                return WzSpawnTicket{ 0u };
            }
            BehaviorSpawnRequest req{};
            req.ticket.value = next_ticket++;
            req.spawner_id = std::move(spawner_id);
            req.prefab_name_hash = desc.prefab_name_hash;
            req.offset[0] = desc.offset[0];
            req.offset[1] = desc.offset[1];
            req.offset[2] = desc.offset[2];
            req.request_tag = desc.request_tag;
            req.spawn_parked = desc.spawn_parked;
            requests.push_back(std::move(req));
            return req.ticket;
        }
    };
}
