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
