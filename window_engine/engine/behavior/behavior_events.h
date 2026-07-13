#pragma once

// engine/behavior/behavior_events.h
//
// Behavior-DEFINED events (v34): behaviors emit NAMED events (wz_emit_behavior_event)
// that a statechart on the target node reacts to via an `event` transition trigger --
// the piece that lets a collision / poll / threshold drive a chart, which
// commit/after/guard cannot. DOUBLE-BUFFERED so delivery is independent of dispatch
// order: emit appends to `pending` during a frame; the next frame's start swaps
// pending -> delivered (advance_frame), and readers (the runner) see `delivered`. So an
// event emitted in frame N is delivered in N+1 -- the same one-frame latency the
// collision/input event surfaces already have.

#include <engine/behavior/behavior_plugin_abi.h>

#include <string>
#include <vector>

namespace wz::engine::behavior
{
    struct BehaviorEventBuffer
    {
        struct Entry
        {
            WzBehaviorEntityId target = WZ_INVALID_BEHAVIOR_ENTITY;
            std::string name;
        };

        std::vector<Entry> pending;    // emitted THIS frame (delivered next)
        std::vector<Entry> delivered;  // emitted LAST frame (readable this frame)

        void emit(WzBehaviorEntityId target, const char* name)
        {
            pending.push_back(Entry{
                target, name ? std::string(name) : std::string{} });
        }

        // Once per frame, before dispatch: last frame's emissions become deliverable.
        void advance_frame()
        {
            delivered.swap(pending);
            pending.clear();
        }

        void clear()
        {
            pending.clear();
            delivered.clear();
        }
    };
}
