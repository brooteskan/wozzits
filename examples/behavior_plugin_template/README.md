# Behavior Plugin Template

This folder is a minimal C++ DLL behavior module for the scene editor.

Build it with CMake, then load it through a scene editor project.

The module uses the small event-handler API:

```cpp
void on_event(const WzBehaviorFrameFacts* facts,
              const WzBehaviorEvent* event,
              void*) {
    switch (event->kind) {
    case WZ_EVENT_COLLISION_ENTER:
        // wz_self(event) is the entity whose behavior component is handling
        // this event. wz_other(event) is the collision partner.
        WzVec3 position{};
        if (wz_self_world_position(facts, event, &position)) {
            // Use position.x/y/z to decide what to do.
        }
        break;
    }
}

WZ_BEHAVIOR_MODULE("template", on_event)
```

Scene nodes bind the module name, and event listeners decide which events route
to that node's behavior component. The registered handler is shared code, but
each call is for one component instance.

The template includes only `engine/behavior/behavior_module_api.h`, so it is a
good starting point for project-authored behavior modules.

Transform commands are buffered during event dispatch and applied afterward.
`wz_self_set_local_rotation` takes `WzQuaternion {x, y, z, w}`; it replaces the
local rotation while preserving local translation and the current basis-column
scale. Additive local rotation is intentionally not part of this V1 API.
