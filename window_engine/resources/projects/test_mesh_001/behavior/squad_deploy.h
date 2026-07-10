#pragma once
// behavior/squad_deploy.h
//
// The command -> pool DEPLOY QUEUE: the single shared contract by which the
// commander's REINFORCE decision drives the pool_manager's unpark. The commander
// (tank_commander) is the sole PRODUCER -- when its reinforce qubit commits it
// posts a request here; the pool_manager is the sole CONSUMER -- it unparks one
// parked reserve per pending request (paced by its own deploy cooldown) and
// decrements. Kept in its own tiny header with no other deps so the generic pool
// can read it without pulling in the whole tank/agent stack.
//
// Shared behavior state (get-or-create by key, wz_create_shared_state): POD,
// survives the structural rebuild each prewarm spawn triggers.

#include <cstdint>

namespace squad_deploy
{
    inline constexpr const char* kKey = "squad_deploy";

    struct Queue
    {
        // Deploy requests POSTED by the commander but not yet CONSUMED by the pool.
        // The commander bounds this so (live + pending) never exceeds the squad
        // target, so it saturates at the current deficit rather than running away.
        int32_t pending = 0;
    };
}
