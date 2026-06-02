# Behavior Plugins

Behavior plugins let a scene node run project-authored C++ code in response to
engine events. They are meant to stay small and explicit: a project builds a
DLL, the scene editor loads that DLL from the project folder, and scene nodes
bind their Behavior component to a registered module name.

The public authoring header is:

```cpp
#include <engine/behavior/behavior_module_api.h>
```

Use this header from plugin code. It wraps the C-compatible ABI in a simpler
event-handler style.

## Project Layout

A scene editor project can provide behavior source and compiled behavior
modules beside the scene:

```text
my_project/
  my_project.project.json
  my_scene.scene.json
  behavior/
    CMakeLists.txt
    sample_behavior_plugin.cpp
    build/
      clang-debug/
        my_project_behaviors.dll
```

The project file tells the editor where behavior source and compiled modules
live:

```json
{
  "schema": "wozzits.scene_editor.project.v1",
  "version": 1,
  "name": "my_project",
  "scene": "my_scene.scene.json",
  "behavior_project_folder": "behavior",
  "behavior_module_folder": "behavior/build/clang-debug"
}
```

The editor loads compiled DLLs from `behavior_module_folder`. Scene nodes bind
to the module names those DLLs register.

## Minimal Module

A behavior module exports `wz_register_behaviors`. The `WZ_BEHAVIOR_MODULE`
macro does that for the common case:

```cpp
#include <engine/behavior/behavior_module_api.h>

namespace
{
    void on_event(
        const WzBehaviorFrameFacts* facts,
        const WzBehaviorEvent* event,
        void*)
    {
        if (!facts || !event) {
            return;
        }

        switch (wz_event_kind(event)) {
        case WZ_EVENT_FRAME_UPDATE:
            break;

        case WZ_EVENT_COLLISION_ENTER:
            wz_self_add_local_translation(facts, event, 0.0f, 1.0f, 0.0f);
            break;

        default:
            break;
        }
    }
}

WZ_BEHAVIOR_MODULE("template", on_event)
```

This registers a module named `template`. A scene node's Behavior component can
then select `template` from the editor once the DLL is loaded.

## Events

The event handler is called once per routed event for each component instance.
The current event kinds are:

```text
WZ_EVENT_FRAME_UPDATE
WZ_EVENT_SCENE_LOADED
WZ_EVENT_COLLISION_ENTER
WZ_EVENT_COLLISION_STAY
WZ_EVENT_COLLISION_EXIT
```

`WZ_EVENT_FRAME_UPDATE` is sent every behavior dispatch for enabled behavior
components whose module is registered.

Collision events are routed through the node's Event Listener component. For a
node to receive collision enter events, add an Event Listener component with a
channel such as:

```text
collision.enter
```

or:

```text
collision.*
```

`collision.*` is a hardcoded "all collision events" token, not a general glob
system.

## Self And Other

Each event has a receiving entity and, for pair events, an optional other
entity:

```cpp
WzBehaviorEntityId self = wz_self(event);
WzBehaviorEntityId other = wz_other(event);
```

`self` is the scene node whose Behavior component is handling the event.

`other` is the collision partner for collision events. For `frame.update`,
`other` is `WZ_INVALID_BEHAVIOR_ENTITY`. Query helpers return `0` for invalid
entities, so this is safe:

```cpp
WzVec3 other_position{};
if (wz_other_world_position(facts, event, &other_position)) {
    // Valid collision partner position.
}
```

## Reading Transforms

Behavior code reads stable frame state during dispatch. Commands written by one
behavior are not visible to transform queries until the engine applies the
command buffer after dispatch.

Available transform queries:

```cpp
WzMat4 local{};
WzMat4 world{};
WzVec3 local_position{};
WzVec3 world_position{};

wz_self_local_transform(facts, event, &local);
wz_self_world_transform(facts, event, &world);
wz_self_local_position(facts, event, &local_position);
wz_self_world_position(facts, event, &world_position);

wz_other_world_position(facts, event, &world_position);
```

Generic entity queries are also available:

```cpp
wz_read_local_transform(facts, entity, &local);
wz_read_world_transform(facts, entity, &world);
wz_read_local_position(facts, entity, &local_position);
wz_read_world_position(facts, entity, &world_position);
```

All query helpers return `1` on success and `0` on failure. Failure is normal
for invalid entities, missing scene context, or null output pointers.

`WzMat4` uses column-major storage. Translation lives in `m[12]`, `m[13]`, and
`m[14]`.

## Querying Collision Surfaces

Behavior code can ray-query a specific queryable collision surface entity.
Collision events tell you `self` and `other`; your behavior decides whether
`other` is a surface worth querying and what ray to cast:

```cpp
WzVec3 self_world{};
WzSurfaceSample surface{};

if (wz_self_world_position(facts, event, &self_world)
    && wz_query_collision_surface_ray(
        facts,
        wz_other(event),
        self_world,
        WzVec3{ 0.0f, -1.0f, 0.0f },
        100.0f,
        &surface))
{
    // surface.surface_entity is the queried surface entity.
    // surface.position is the world-space hit point.
    // surface.normal is the world-space surface normal.
}
```

The query form is:

```cpp
wz_query_collision_surface_ray(
    facts,
    surface_entity,
    origin,
    direction,
    max_distance,
    &surface);
```

The ray uses world-space `origin` and `direction` and returns the nearest hit
within `max_distance`. It reads from `FrameStorage::collision.world`, so it
uses collision assets already resolved for the frame. V1 supports queryable
`TerrainMeshSurface` collision assets. Height-field collision assets are
intentionally not sampled by this helper yet.

## Writing Commands

Behaviors do not mutate the scene graph directly. They write commands into the
frame command buffer. The engine applies those commands after behavior dispatch
and then propagates scene transforms.

Common helpers target `self`:

```cpp
wz_self_add_local_translation(facts, event, 0.0f, 1.0f, 0.0f);
wz_self_set_local_translation(facts, event, 0.0f, 2.0f, 0.0f);

wz_self_add_local_scale(facts, event, 0.1f, 0.1f, 0.1f);
wz_self_set_local_scale(facts, event, 2.0f, 2.0f, 2.0f);

wz_self_set_local_rotation(
    facts,
    event,
    WzQuaternion{ 0.0f, 0.0f, 0.70710677f, 0.70710677f });
```

Generic entity command helpers are also available:

```cpp
wz_write_add_local_translation(facts, entity, x, y, z);
wz_write_set_local_translation(facts, entity, x, y, z);
wz_write_add_local_scale(facts, entity, x, y, z);
wz_write_set_local_scale(facts, entity, x, y, z);
wz_write_set_local_rotation(facts, entity, rotation);
```

Command order is deterministic. Multiple commands targeting the same entity are
legal and are applied in command-buffer order.

## Rotation And Scale Semantics

The scene graph stores local transforms as matrices. The V1 behavior API uses
these rules:

- Local translation writes the matrix translation column directly.
- Local scale changes local basis-column lengths and preserves basis direction.
- Local rotation uses `WzQuaternion {x, y, z, w}`.
- `wz_self_set_local_rotation` replaces local rotation.
- Set rotation preserves local translation and current basis-column scale.
- Additive local rotation is intentionally not part of V1.

If a behavior needs additive rotation later, that should be added with an
explicit matrix decomposition policy rather than guessed in plugin code.

## Logging

Use the frame facts logger callback through the helper:

```cpp
wz_log_info(facts, "hello from behavior");
```

The template module shows a collision log that includes `self`, `other`, and
self world position.

## Building The Template

The repository includes a standalone template at:

```text
examples/behavior_plugin_template
```

It is also mirrored into the terrain collision project under:

```text
window_engine/resources/projects/terrain_collision/behavior
```

Build the project-local behavior DLL, then load modules from the scene editor.
The terrain collision project expects its compiled DLLs in:

```text
behavior/build/clang-debug
```

## Current Limits

The V1 API intentionally does not include:

- Additive local rotation
- Physics forces or velocities
- Collision contact normals or penetration depth
- Script runtime integration
- Editable behavior parameters
- Hot reload guarantees

Those can be added later without changing the basic event-handler shape.
