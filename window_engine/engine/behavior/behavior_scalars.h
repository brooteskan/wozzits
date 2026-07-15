#pragma once

// engine/behavior/behavior_scalars.h
//
// Behavior-published NAMED scalars (v37): a behavior publishes a named number on an
// entity (wz_set_entity_scalar) and a statechart on that entity READS it as a pure-op
// value (a `read_state` op) -- the PULL twin of behavior events (behavior_events.h).
// Where an event PUSHES a discrete signal ("died"), this exposes a CONTINUOUS quantity
// ("damage", "ammo") a guard can compare -- the thing commit/after/proximity can't.
//
// DOUBLE-BUFFERED like events, so a read is independent of whether the publisher ticked
// before or after the reader: writes land in `pending`; the next frame's start swaps
// pending -> published (advance_frame), and readers see `published`. A value published
// in frame N is readable in N+1 -- the same one-frame latency the event + collision
// surfaces have. A scalar the publisher STOPS writing drops after one frame (published
// becomes the now-empty pending), so a stale reading self-clears.

#include <engine/behavior/behavior_plugin_abi.h>

#include <string>
#include <unordered_map>

namespace wz::engine::behavior
{
    struct BehaviorScalarBoard
    {
        // entity -> (name -> value). Per-entity so a chart reads scalars on self (or,
        // later, a bound entity) by name; the name space is OPEN, like actuators/events.
        using Row = std::unordered_map<std::string, double>;

        std::unordered_map<WzBehaviorEntityId, Row> pending;    // written THIS frame (readable next)
        std::unordered_map<WzBehaviorEntityId, Row> published;  // written LAST frame (readable now)

        void set(WzBehaviorEntityId entity, const char* name, double value)
        {
            if (name) {
                pending[entity][name] = value;
            }
        }

        // Returns false (leaving *out untouched) when no such scalar is published.
        bool get(WzBehaviorEntityId entity, const char* name, double* out) const
        {
            if (!name) {
                return false;
            }
            auto row = published.find(entity);
            if (row == published.end()) {
                return false;
            }
            auto v = row->second.find(name);
            if (v == row->second.end()) {
                return false;
            }
            if (out) {
                *out = v->second;
            }
            return true;
        }

        // Once per frame, before dispatch: last frame's writes become readable.
        void advance_frame()
        {
            published.swap(pending);
            pending.clear();
        }

        void clear()
        {
            pending.clear();
            published.clear();
        }
    };
}
