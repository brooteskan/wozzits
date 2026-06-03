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
WZ_EVENT_PROXIMITY_ENTER
WZ_EVENT_PROXIMITY_STAY
WZ_EVENT_PROXIMITY_EXIT
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

Proximity events are routed through the same Event Listener component. Add a
Proximity component to define the node's radius and masks, then subscribe with:

```text
proximity.enter
proximity.stay
proximity.exit
proximity.*
```

`proximity.*` is a hardcoded "all proximity events" token, not a general glob
system. Proximity events are radius based and separate from collision overlap:
they are intended for "close enough to matter" behavior such as starting a
terrain snap/orient query before actual collision. Proximity radius is authored
in world units and does not inherit scale from the node's transform hierarchy.

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

## Reading Input

Input is exposed as a frame snapshot through `facts->input`. It is not delivered
as a separate event stream. Behaviors that respond to input usually read it
during `WZ_EVENT_FRAME_UPDATE`:

```cpp
if (wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
    WzVec3 axis{};
    if (wz_input_wasd_axis(facts, &axis)) {
        wz_self_set_linear_velocity(
            facts,
            event,
            axis.x * 4.0f,
            0.0f,
            axis.z * 4.0f);
    }
}
```

Keyboard helpers:

```cpp
wz_key_down(facts, WZ_KEY_W);
wz_key_pressed(facts, WZ_KEY_SPACE);
wz_key_released(facts, WZ_KEY_ESCAPE);
```

Keyboard indices currently match Windows virtual-key codes. Common constants
are defined in the ABI header; other keys can be read by numeric code until the
named key set is expanded.

Mouse and window helpers:

```cpp
wz_mouse_button_down(facts, WZ_MOUSE_BUTTON_LEFT);
wz_mouse_x(facts);
wz_mouse_y(facts);
wz_mouse_dx(facts);
wz_mouse_dy(facts);
wz_window_focused(facts);
wz_window_width(facts);
wz_window_height(facts);
```

Controller helpers read indexed controller slots:

```cpp
wz_controller_count(facts);
wz_controller_connected(facts, 0);
wz_controller_connected_pressed(facts, 0);
wz_controller_connected_released(facts, 0);
wz_controller_axis(facts, 0, WZ_CONTROLLER_AXIS_LEFT_X);
wz_controller_button_down(facts, 0, WZ_CONTROLLER_BUTTON_A);
wz_controller_button_pressed(facts, 0, WZ_CONTROLLER_BUTTON_A);
wz_controller_button_released(facts, 0, WZ_CONTROLLER_BUTTON_A);
```

Controller slots are indexed from `0` to `wz_controller_count(facts) - 1`.
Axes use `LEFT_X`, `LEFT_Y`, `RIGHT_X`, `RIGHT_Y`, `LEFT_TRIGGER`, and
`RIGHT_TRIGGER`. Button constants map to the XInput layout: d-pad directions,
start, back, left/right thumb, left/right shoulder, A, B, X, Y.

`wz_input_wasd_axis` returns a world-style horizontal axis where A/D map to X
and W/S map to Z. Diagonal input is normalized. It returns `1` when input facts
are available even if no WASD keys are pressed, in which case the output axis
is `{0, 0, 0}`.

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

## Direction To Another Entity

Collision and proximity events provide `self` and `other`. The helper layer can
turn those entities into a world-space vector, distance, or normalized
direction:

```cpp
WzVec3 vector{};
WzVec3 direction{};
float distance = 0.0f;

if (wz_vector_self_to_other(facts, event, &vector)
    && wz_distance_self_to_other(facts, event, &distance)
    && wz_direction_self_to_other(facts, event, &direction))
{
    wz_self_set_linear_velocity(
        facts,
        event,
        direction.x * 2.0f,
        direction.y * 2.0f,
        direction.z * 2.0f);
}
```

The vector convention is `other_world_position - self_world_position`.
Direction helpers return `0` when either entity cannot be read or the two
positions are effectively the same.

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

## Scene Lookup And Config

Scene-authored behaviors can carry a small primitive config object:

```json
{
  "behavior": {
    "module": "gameplay",
    "name": "move_cube",
    "enabled": true,
    "config": {
      "terrain_id": "terrain",
      "speed": 4.5,
      "snap_to_ground": true
    }
  }
}
```

Config values may be booleans, numbers, or strings. Arrays, objects, and nulls
are rejected by the scene compiler.

Behavior modules read authored config during dispatch:

```cpp
float speed = 1.0f;
uint8_t snap_to_ground = 0;
char terrain_id[64]{};

wz_config_float(facts, "speed", &speed);
wz_config_bool(facts, "snap_to_ground", &snap_to_ground);
if (wz_config_string(
        facts,
        "terrain_id",
        terrain_id,
        sizeof(terrain_id),
        nullptr))
{
    WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
    wz_find_entity_by_authored_id(facts, terrain_id, &terrain);
}
```

`wz_find_entity_by_authored_id` resolves stable scene node ids.
`wz_find_entity_by_name` is also available for display names, but authored ids
are usually better for gameplay bindings.

`wz_config_string` writes the required byte count, including the null terminator,
when `out_required_size` is non-null. A null or zero-sized output buffer can be
used as a size query and returns `1` when the key exists and is a string. A
non-empty buffer is always null-terminated when the key exists; the helper
returns `0` if the value had to be truncated.

## Writing Commands

Behaviors do not mutate the scene graph directly. They write commands into the
frame command buffer. The engine applies those commands after behavior dispatch
and then propagates scene transforms.

Common helpers target `self`:

```cpp
wz_self_add_local_translation(facts, event, 0.0f, 1.0f, 0.0f);
wz_self_set_local_translation(facts, event, 0.0f, 2.0f, 0.0f);
wz_self_add_world_translation(facts, event, 0.0f, 1.0f, 0.0f);
wz_self_set_world_translation(facts, event, 0.0f, 2.0f, 0.0f);

wz_self_add_local_scale(facts, event, 0.1f, 0.1f, 0.1f);
wz_self_set_local_scale(facts, event, 2.0f, 2.0f, 2.0f);

wz_self_set_local_rotation(
    facts,
    event,
    WzQuaternion{ 0.0f, 0.0f, 0.70710677f, 0.70710677f });

wz_self_set_linear_velocity(facts, event, 0.0f, 1.0f, 0.0f);
```

Generic entity command helpers are also available:

```cpp
wz_write_add_local_translation(facts, entity, x, y, z);
wz_write_set_local_translation(facts, entity, x, y, z);
wz_write_add_world_translation(facts, entity, x, y, z);
wz_write_set_world_translation(facts, entity, x, y, z);
wz_write_add_local_scale(facts, entity, x, y, z);
wz_write_set_local_scale(facts, entity, x, y, z);
wz_write_set_local_rotation(facts, entity, rotation);
wz_write_set_linear_velocity(facts, entity, x, y, z);
```

Command order is deterministic. Multiple commands targeting the same entity are
legal and are applied in command-buffer order.

World translation commands require an invertible parent transform. If an
entity's parent has a degenerate transform, such as a zero scale axis, the
world translation command is ignored during command application.

World translation commands are applied before scene transform propagation runs.
`set_world_translation` is absolute, but `add_world_translation` computes its
target from the entity's world transform as it existed when command application
began. Earlier commands in the same buffer that change the same entity's local
transform are not visible to that world-space add until the next propagation.

Linear velocity is motion in units per second. Angular velocity is an
axis-angle vector whose direction is the rotation axis and whose magnitude is
radians per second. Motion defaults to world space, but a behavior can switch a
node to local-space motion with
`wz_self_set_motion_space(facts, event, WZ_BEHAVIOR_MOTION_SPACE_LOCAL)`.
Set linear velocity with `wz_self_set_linear_velocity` or
`wz_write_set_linear_velocity`; set angular velocity with
`wz_self_set_angular_velocity` or `wz_write_set_angular_velocity`. The engine
stores those values as runtime motion state, then integrates them once per frame
after behavior commands are applied and before render prep. Setting velocity
does not immediately move or rotate the entity; the frame integration step
applies `velocity * delta_seconds`.

Local-space angular velocity composes in the node's local frame. World-space
angular velocity composes in the world frame; for parented nodes the runtime
converts through the parent's world rotation before writing the node's local
transform. If the local or parent/world transform cannot be decomposed as safe
TRS, angular integration for that node is skipped for the frame.

For a minimal working module, see
[`examples/behavior_plugin_template/angular_motion_plugin.cpp`](../../examples/behavior_plugin_template/angular_motion_plugin.cpp).

Frame timing is available through:

```cpp
const float dt = wz_delta_seconds(facts);
const uint64_t frame = wz_frame_index(facts);
```

`delta_seconds` is the current frame interval. `elapsed_seconds` in
`facts->timing` is the engine monotonic clock value at the frame end; it is not
reset per scene or per behavior module.

## Rotation And Scale Semantics

The scene graph stores local transforms as matrices. The V1 behavior API uses
these rules:

- Local translation writes the matrix translation column directly.
- World translation writes are converted into the entity's parent-local
  translation before scene transforms are propagated.
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
