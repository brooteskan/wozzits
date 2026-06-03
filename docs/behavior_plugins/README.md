# Behavior Plugins

Behavior plugins let a scene node run project-authored C++ code in response to
engine events. A project builds a DLL, the scene editor loads that DLL from the
project folder, and scene nodes bind their Behavior component to a registered
module name.

Plugin authors should include the authoring header:

```cpp
#include <engine/behavior/behavior_module_api.h>
```

That header wraps the C-compatible ABI in a small event-handler API. Most
behavior code should use the helpers in this document instead of touching the
raw callback fields in `WzBehaviorFrameFacts`.

## Project Layout

A scene editor project can keep behavior source and compiled behavior modules
beside the scene:

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

[Back to Behavior API Inventory](#behavior-api-inventory)

For the common case, define one event handler and register it with
`WZ_BEHAVIOR_MODULE`:

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

        if (wz_is_event(event, WZ_EVENT_PROXIMITY_ENTER)) {
            float speed = 3.0f;
            wz_config_float(facts, "speed", &speed);
            wz_self_set_linear_velocity(
                facts,
                event,
                0.0f,
                0.0f,
                speed);
        }
    }
}

WZ_BEHAVIOR_MODULE("proximity_boost", on_event)
```

This registers a module named `proximity_boost`. A scene node's Behavior
component can select `proximity_boost` after the DLL is loaded.

## Attaching Behavior To A Scene Node

[Back to Behavior API Inventory](#behavior-api-inventory)

In authored scene JSON, a node selects a behavior module by name:

```json
{
  "id": "player",
  "name": "Player",
  "proximity": {
    "radius": 3.0
  },
  "behavior": {
    "label": "Boost on proximity",
    "module": "proximity_boost",
    "enabled": true,
    "events": [ "proximity.enter" ],
    "config": {
      "speed": 3.0
    }
  }
}
```

`module` must match the string passed to `WZ_BEHAVIOR_MODULE`.

`label` is optional editor-facing text. Use it to give repeated or specialized
bindings a human-readable name such as "Door proximity trigger" or "Player
movement input".

`name` is optional legacy metadata. Module-event plugins normally key behavior
off `module`, not `name`.

`enabled` defaults to true when omitted.

`events` is optional. When present, it lists the event channels this behavior
binding receives. When omitted, the binding uses the module's default event
channels if the plugin registered any defaults.

`config` is optional. It may contain booleans, numbers, and strings. Arrays,
objects, and null values are rejected by the scene compiler.

Nodes may also use plural `behaviors` to attach multiple behavior modules:

```json
{
  "id": "player",
  "behaviors": [
    {
      "label": "Player movement input",
      "module": "player_move",
      "events": [ "input.*", "frame.update" ]
    },
    {
      "label": "Footstep collision audio",
      "module": "footstep_audio",
      "events": [ "collision.enter" ]
    }
  ]
}
```

Each behavior binding has its own event list. Multiple behavior bindings on the
same node may subscribe to the same event; each matching binding is called.

The current API is event-driven for scene events such as collision, proximity,
and input button/key edges. Continuous input state, such as held keys, mouse
position, and controller axes, is also available as a frame snapshot.

## Behavior API Inventory

This is the current authoring surface exposed by
`engine/behavior/behavior_module_api.h`, grouped by what a behavior author is
usually trying to do.

### Registration And Scene Binding

- Register one module with no default subscriptions:
  [`WZ_BEHAVIOR_MODULE(module_name, handler_fn)`](#minimal-module).
- Register one module with default event subscriptions:
  [`WZ_BEHAVIOR_MODULE_EVENTS(module_name, handler_fn, event_channel_array)`](#raw-abi-registration).
- Register multiple modules manually with
  [`wz_register_behaviors`](#raw-abi-registration) and
  [`api->register_module_desc`](#raw-abi-registration).
- Bind modules in scene data with
  [`behavior` or plural `behaviors`](#attaching-behavior-to-a-scene-node).
- Give each binding a human-readable `label`, an engine-facing `module`, an
  optional legacy `name`, an `enabled` flag, an `events` list, and primitive
  [`config`](#scene-lookup-and-config).

### Event Dispatch

- Event kinds: `WZ_EVENT_FRAME_UPDATE`, `WZ_EVENT_SCENE_LOADED`,
  `WZ_EVENT_COLLISION_ENTER`, `WZ_EVENT_COLLISION_STAY`,
  `WZ_EVENT_COLLISION_EXIT`, `WZ_EVENT_PROXIMITY_ENTER`,
  `WZ_EVENT_PROXIMITY_STAY`, `WZ_EVENT_PROXIMITY_EXIT`,
  `WZ_EVENT_INPUT_KEY_PRESSED`, `WZ_EVENT_INPUT_KEY_RELEASED`,
  `WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED`,
  `WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED`,
  `WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED`, and
  `WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED`.
- Event helpers: [`wz_event_kind`](#event-routing),
  [`wz_is_event`](#event-routing), [`wz_event_name`](#event-routing),
  [`wz_self`](#event-routing), [`wz_other`](#event-routing), and
  [`wz_self_is_trigger`](#event-routing).
- Event channel tokens: `frame.update`, `scene.loaded`, `collision.enter`,
  `collision.stay`, `collision.exit`, `collision.*`, `proximity.enter`,
  `proximity.stay`, `proximity.exit`, `proximity.*`,
  `input.key.pressed`, `input.key.released`,
  `input.mouse_button.pressed`, `input.mouse_button.released`,
  `input.controller_button.pressed`, `input.controller_button.released`, and
  [`input.*`](#event-channel-tokens).

### Input Dispatch And Snapshot Reads

- Edge-event payload helpers: [`wz_input_event_key`](#input-events),
  [`wz_input_event_mouse_button`](#input-events),
  [`wz_input_event_controller`](#input-events), and
  [`wz_input_event_controller_button`](#input-events).
- Keyboard snapshot helpers: [`wz_key_down`](#reading-input),
  [`wz_key_pressed`](#reading-input), and
  [`wz_key_released`](#reading-input).
- Mouse snapshot helpers: [`wz_mouse_button_down`](#reading-input),
  [`wz_mouse_button_pressed`](#reading-input),
  [`wz_mouse_button_released`](#reading-input),
  [`wz_mouse_x`](#reading-input), [`wz_mouse_y`](#reading-input),
  [`wz_mouse_dx`](#reading-input), and [`wz_mouse_dy`](#reading-input).
- Window snapshot helpers: [`wz_window_focused`](#reading-input),
  [`wz_window_width`](#reading-input), and
  [`wz_window_height`](#reading-input).
- Controller snapshot helpers: [`wz_controller_count`](#reading-input),
  [`wz_controller_connected`](#reading-input),
  [`wz_controller_connected_pressed`](#reading-input),
  [`wz_controller_connected_released`](#reading-input),
  [`wz_controller_axis`](#reading-input),
  [`wz_controller_button_down`](#reading-input),
  [`wz_controller_button_pressed`](#reading-input), and
  [`wz_controller_button_released`](#reading-input).
- Convenience axis helper: [`wz_input_wasd_axis`](#reading-input).

### Things To Do With Motion And Transforms

- Move or scale self: [`wz_self_add_local_translation`](#writing-commands),
  [`wz_self_set_local_translation`](#writing-commands),
  [`wz_self_add_world_translation`](#writing-commands),
  [`wz_self_set_world_translation`](#writing-commands),
  [`wz_self_add_local_scale`](#writing-commands), and
  [`wz_self_set_local_scale`](#writing-commands).
- Rotate self: [`wz_self_set_local_rotation`](#writing-commands).
- Set runtime motion for self:
  [`wz_self_set_linear_velocity`](#writing-commands),
  [`wz_self_set_angular_velocity`](#writing-commands), and
  [`wz_self_set_motion_space`](#writing-commands).
- Affect the paired entity in collision/proximity events:
  [`wz_other_add_world_translation`](#writing-commands),
  [`wz_other_set_world_translation`](#writing-commands),
  [`wz_other_set_linear_velocity`](#writing-commands),
  [`wz_other_set_angular_velocity`](#writing-commands), and
  [`wz_other_set_motion_space`](#writing-commands).
- Write commands to any entity id:
  [`wz_write_add_local_translation`](#writing-commands),
  [`wz_write_set_local_translation`](#writing-commands),
  [`wz_write_add_world_translation`](#writing-commands),
  [`wz_write_set_world_translation`](#writing-commands),
  [`wz_write_add_local_scale`](#writing-commands),
  [`wz_write_set_local_scale`](#writing-commands),
  [`wz_write_set_local_rotation`](#writing-commands),
  [`wz_write_set_linear_velocity`](#writing-commands),
  [`wz_write_set_angular_velocity`](#writing-commands), and
  [`wz_write_set_motion_space`](#writing-commands).
- Motion spaces: `WZ_BEHAVIOR_MOTION_SPACE_WORLD` and
  [`WZ_BEHAVIOR_MOTION_SPACE_LOCAL`](#motion-rotation-and-scale-semantics).

### Reading Scene State

- Read self transforms and positions:
  [`wz_self_local_transform`](#reading-transforms),
  [`wz_self_world_transform`](#reading-transforms),
  [`wz_self_local_position`](#reading-transforms), and
  [`wz_self_world_position`](#reading-transforms).
- Read paired-entity transforms and positions:
  [`wz_other_local_transform`](#reading-transforms),
  [`wz_other_world_transform`](#reading-transforms),
  [`wz_other_local_position`](#reading-transforms), and
  [`wz_other_world_position`](#reading-transforms).
- Read any entity by id: [`wz_read_local_transform`](#reading-transforms),
  [`wz_read_world_transform`](#reading-transforms),
  [`wz_read_local_position`](#reading-transforms), and
  [`wz_read_world_position`](#reading-transforms).
- Find entities by authored scene data:
  [`wz_find_entity_by_authored_id`](#scene-lookup-and-config) and
  [`wz_find_entity_by_name`](#scene-lookup-and-config).

### Spatial Relationship And Collision Surface Queries

- Compute vectors, distances, and normalized directions between entities:
  [`wz_vector_between_world_positions`](#direction-and-distance-helpers),
  [`wz_distance_between_world_positions`](#direction-and-distance-helpers),
  [`wz_direction_between_world_positions`](#direction-and-distance-helpers),
  [`wz_vector_self_to_other`](#direction-and-distance-helpers),
  [`wz_distance_self_to_other`](#direction-and-distance-helpers), and
  [`wz_direction_self_to_other`](#direction-and-distance-helpers).
- Query a collision surface ray:
  [`wz_query_collision_surface_ray`](#querying-collision-surfaces).

### Authored Config, Timing, And Diagnostics

- Read primitive behavior config:
  [`wz_config_bool`](#scene-lookup-and-config),
  [`wz_config_number`](#scene-lookup-and-config),
  [`wz_config_float`](#scene-lookup-and-config), and
  [`wz_config_string`](#scene-lookup-and-config).
- Read frame timing: [`wz_delta_seconds`](#frame-timing) and
  [`wz_frame_index`](#frame-timing).
- Write editor/runtime diagnostics: [`wz_log_info`](#logging).

## Event Routing

[Back to Behavior API Inventory](#behavior-api-inventory)

The event handler is called once per routed event for each enabled behavior
component whose module is registered.

Current event kinds:

```text
WZ_EVENT_FRAME_UPDATE
WZ_EVENT_SCENE_LOADED
WZ_EVENT_COLLISION_ENTER
WZ_EVENT_COLLISION_STAY
WZ_EVENT_COLLISION_EXIT
WZ_EVENT_PROXIMITY_ENTER
WZ_EVENT_PROXIMITY_STAY
WZ_EVENT_PROXIMITY_EXIT
WZ_EVENT_INPUT_KEY_PRESSED
WZ_EVENT_INPUT_KEY_RELEASED
WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED
WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED
WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED
WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED
```

`WZ_EVENT_SCENE_LOADED` is reserved in the ABI and channel table; current
runtime dispatch sends frame, collision, proximity, and input events.

Helper functions:

```cpp
WzBehaviorEventKind kind = wz_event_kind(event);
uint8_t is_update = wz_is_event(event, WZ_EVENT_FRAME_UPDATE);
const char* name = wz_event_name(kind);
WzBehaviorEntityId self = wz_self(event);
WzBehaviorEntityId other = wz_other(event);
uint8_t trigger = wz_self_is_trigger(event);
```

`wz_self(event)` is the scene node whose Behavior component is handling the
event. `wz_other(event)` is the collision or proximity partner for pair events.
For frame and scene-loaded events, `other` is `WZ_INVALID_BEHAVIOR_ENTITY`.

`wz_self_is_trigger(event)` is set for collision/proximity events when the
receiving side is a trigger participant.

## Event Channel Tokens

[Back to Behavior API Inventory](#behavior-api-inventory)

Scene behavior bindings and plugin defaults use the same channel tokens:

```text
frame.update
scene.loaded
collision.enter
collision.stay
collision.exit
collision.*
proximity.enter
proximity.stay
proximity.exit
proximity.*
input.key.pressed
input.key.released
input.mouse_button.pressed
input.mouse_button.released
input.controller_button.pressed
input.controller_button.released
input.*
```

`collision.*`, `proximity.*`, and `input.*` are hardcoded group tokens, not a
general glob system.

`event_listener` still exists for generic/non-behavior event participation,
such as collision or proximity entities that need to appear in routed event
storage even when they do not have behavior bindings. Normal scene-authored
behavior should put subscriptions on the behavior binding instead.
`event_listener` does not restrict which events a behavior binding receives.

## Frame Update Events

[Back to Behavior API Inventory](#behavior-api-inventory)

`WZ_EVENT_FRAME_UPDATE` is sent only to behavior bindings whose `events` list,
or plugin default event list, includes:

```text
frame.update
```

Use frame update for continuous behavior such as held input checks, timers, AI
state machines, and velocity control.

Migration note: older behavior modules received `WZ_EVENT_FRAME_UPDATE`
unconditionally. Add `frame.update` to the behavior binding's `events` list for
any behavior that still needs a per-frame callback.

## Collision Events

[Back to Behavior API Inventory](#behavior-api-inventory)

To receive collision events, add one or more collision channels to the behavior
binding's `events` list:

```text
collision.enter
collision.stay
collision.exit
collision.*
```

`collision.*` is a hardcoded "all collision events" token, not a general glob
system.

Collision pair events provide both `self` and `other`. Query helpers return `0`
for invalid entities, so this pattern is safe:

```cpp
if (wz_is_event(event, WZ_EVENT_COLLISION_ENTER)) {
    WzVec3 other_position{};
    if (wz_other_world_position(facts, event, &other_position)) {
        wz_log_info(facts, "collision partner has a world position");
    }
}
```

## Proximity Events

[Back to Behavior API Inventory](#behavior-api-inventory)

Add a Proximity component to define radius and masks, then subscribe the
behavior binding with:

```text
proximity.enter
proximity.stay
proximity.exit
proximity.*
```

`proximity.*` is a hardcoded "all proximity events" token, not a general glob
system. Proximity events are radius based and separate from collision overlap.
They are intended for "close enough to matter" behavior such as starting a
terrain snap/orient query before actual collision. Proximity radius is authored
in world units and does not inherit scale from the node's transform hierarchy.

## Input Events

[Back to Behavior API Inventory](#behavior-api-inventory)

Input edge events are routed to behavior bindings that subscribe with one or
more of:

```text
input.key.pressed
input.key.released
input.mouse_button.pressed
input.mouse_button.released
input.controller_button.pressed
input.controller_button.released
input.*
```

`input.*` is a hardcoded "all input events" token, not a general glob system.
The engine derives these events from the current frame input snapshot, so a
pressed or released edge is delivered for the frame in which it appears.

Input event payload helpers read the active routed input event:

```cpp
if (wz_is_event(event, WZ_EVENT_INPUT_KEY_PRESSED)) {
    uint32_t key = wz_input_event_key(facts);
}

if (wz_is_event(event, WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED)) {
    uint32_t button = wz_input_event_mouse_button(facts);
}

if (wz_is_event(event, WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED)) {
    uint32_t controller = wz_input_event_controller(facts);
    uint32_t button = wz_input_event_controller_button(facts);
}
```

Payload helpers return `WZ_INPUT_EVENT_INVALID_VALUE` when the current dispatch
is not an input event or when that field does not apply. During an input event,
the normal frame snapshot helpers still read `facts->input`, so a behavior can
combine the routed edge with held-state or axis checks.

## Reading Input

[Back to Behavior API Inventory](#behavior-api-inventory)

Input is exposed in two forms:

- Edge events for key, mouse button, and controller button pressed/released
  transitions.
- A frame snapshot through `facts->input` for held state, mouse position/delta,
  window state, controller connection state, and controller axes.

Use event channels when behavior should only run on a transition. Use
`WZ_EVENT_FRAME_UPDATE` when behavior must continuously react to held state,
analog axes, timers, or per-frame motion.

Keyboard helpers:

```cpp
wz_key_down(facts, WZ_KEY_W);
wz_key_pressed(facts, WZ_KEY_SPACE);
wz_key_released(facts, WZ_KEY_ESCAPE);
```

Defined key constants:

```text
WZ_KEY_A
WZ_KEY_D
WZ_KEY_S
WZ_KEY_W
WZ_KEY_SPACE
WZ_KEY_ESCAPE
WZ_KEY_SHIFT
WZ_KEY_CONTROL
```

Keyboard indices currently match Windows virtual-key codes. Other keys can be
read by numeric code until the named key set is expanded.

Mouse and window helpers:

```cpp
wz_mouse_button_down(facts, WZ_MOUSE_BUTTON_LEFT);
wz_mouse_button_pressed(facts, WZ_MOUSE_BUTTON_RIGHT);
wz_mouse_button_released(facts, WZ_MOUSE_BUTTON_MIDDLE);
wz_mouse_x(facts);
wz_mouse_y(facts);
wz_mouse_dx(facts);
wz_mouse_dy(facts);
wz_window_focused(facts);
wz_window_width(facts);
wz_window_height(facts);
```

Mouse button constants:

```text
WZ_MOUSE_BUTTON_LEFT
WZ_MOUSE_BUTTON_RIGHT
WZ_MOUSE_BUTTON_MIDDLE
```

Controller helpers:

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
Invalid controller, axis, or button indices return neutral values.

Axis constants:

```text
WZ_CONTROLLER_AXIS_LEFT_X
WZ_CONTROLLER_AXIS_LEFT_Y
WZ_CONTROLLER_AXIS_RIGHT_X
WZ_CONTROLLER_AXIS_RIGHT_Y
WZ_CONTROLLER_AXIS_LEFT_TRIGGER
WZ_CONTROLLER_AXIS_RIGHT_TRIGGER
```

Button constants:

```text
WZ_CONTROLLER_BUTTON_DPAD_UP
WZ_CONTROLLER_BUTTON_DPAD_DOWN
WZ_CONTROLLER_BUTTON_DPAD_LEFT
WZ_CONTROLLER_BUTTON_DPAD_RIGHT
WZ_CONTROLLER_BUTTON_START
WZ_CONTROLLER_BUTTON_BACK
WZ_CONTROLLER_BUTTON_LEFT_THUMB
WZ_CONTROLLER_BUTTON_RIGHT_THUMB
WZ_CONTROLLER_BUTTON_LEFT_SHOULDER
WZ_CONTROLLER_BUTTON_RIGHT_SHOULDER
WZ_CONTROLLER_BUTTON_A
WZ_CONTROLLER_BUTTON_B
WZ_CONTROLLER_BUTTON_X
WZ_CONTROLLER_BUTTON_Y
```

`wz_input_wasd_axis` returns a horizontal movement axis:

```cpp
WzVec3 axis{};
if (wz_input_wasd_axis(facts, &axis)) {
    // A/D map to axis.x. W/S map to axis.z. axis.y is always 0.
}
```

Diagonal WASD input is normalized. The helper returns `1` when input facts are
available even if no WASD keys are pressed, in which case the output axis is
`{0, 0, 0}`.

## Reading Transforms

[Back to Behavior API Inventory](#behavior-api-inventory)

Behavior code reads stable frame state during dispatch. Commands written by one
behavior are not visible to transform queries until the engine applies the
command buffer after dispatch.

Self and other transform queries:

```cpp
WzMat4 local{};
WzMat4 world{};
WzVec3 position{};

wz_self_local_transform(facts, event, &local);
wz_self_world_transform(facts, event, &world);
wz_self_local_position(facts, event, &position);
wz_self_world_position(facts, event, &position);

wz_other_local_transform(facts, event, &local);
wz_other_world_transform(facts, event, &world);
wz_other_local_position(facts, event, &position);
wz_other_world_position(facts, event, &position);
```

Generic entity queries:

```cpp
wz_read_local_transform(facts, entity, &local);
wz_read_world_transform(facts, entity, &world);
wz_read_local_position(facts, entity, &position);
wz_read_world_position(facts, entity, &position);
```

All query helpers return `1` on success and `0` on failure. Failure is normal
for invalid entities, missing scene context, or null output pointers.

`WzMat4` uses column-major storage. Translation lives in `m[12]`, `m[13]`, and
`m[14]`.

## Direction And Distance Helpers

[Back to Behavior API Inventory](#behavior-api-inventory)

Collision and proximity events provide `self` and `other`. The helper layer can
turn any two entities into a world-space vector, distance, or normalized
direction:

```cpp
WzVec3 vector{};
WzVec3 direction{};
float distance = 0.0f;

wz_vector_between_world_positions(facts, from_entity, to_entity, &vector);
wz_distance_between_world_positions(facts, from_entity, to_entity, &distance);
wz_direction_between_world_positions(facts, from_entity, to_entity, &direction);

wz_vector_self_to_other(facts, event, &vector);
wz_distance_self_to_other(facts, event, &distance);
wz_direction_self_to_other(facts, event, &direction);
```

The vector convention is `to_world_position - from_world_position`.
`self_to_other` therefore means `other_world_position - self_world_position`.
Direction helpers return `0` when either entity cannot be read or the two
positions are effectively the same.

## Querying Collision Surfaces

[Back to Behavior API Inventory](#behavior-api-inventory)

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
        &surface)
    && surface.hit)
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

[Back to Behavior API Inventory](#behavior-api-inventory)

Scene-authored behaviors can carry a small primitive config object:

```json
{
  "behavior": {
    "module": "terrain_snap",
    "enabled": true,
    "config": {
      "terrain_id": "terrain",
      "speed": 4.5,
      "snap_to_ground": true
    }
  }
}
```

Read authored config during dispatch:

```cpp
double speed_number = 0.0;
float speed = 1.0f;
uint8_t snap_to_ground = 0;
char terrain_id[64]{};

wz_config_number(facts, "speed", &speed_number);
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

`wz_config_number` reads a JSON number as `double`.
`wz_config_float` reads the same value and narrows it to `float`.
`wz_config_bool` reads a boolean into `uint8_t`.

`wz_config_string` writes the required byte count, including the null
terminator, when `out_required_size` is non-null. A null or zero-sized output
buffer can be used as a size query and returns `1` when the key exists and is a
string. A non-empty buffer is always null-terminated when the key exists; the
helper returns `0` if the value had to be truncated.

Scene lookup helpers:

```cpp
wz_find_entity_by_authored_id(facts, "terrain", &entity);
wz_find_entity_by_name(facts, "Terrain Display Name", &entity);
```

`wz_find_entity_by_authored_id` resolves stable scene node ids. Prefer authored
ids for gameplay bindings. `wz_find_entity_by_name` resolves display names and
is useful for prototypes, but names are easier to change accidentally.

## Writing Commands

[Back to Behavior API Inventory](#behavior-api-inventory)

Behaviors do not mutate the scene graph directly. They write commands into the
frame command buffer. The engine applies those commands after behavior dispatch
and then propagates scene transforms.

All command helpers return `1` if the command was accepted by the command
writer and `0` if there was no writer or the target entity was invalid.

Self command helpers:

```cpp
wz_self_add_local_translation(facts, event, x, y, z);
wz_self_set_local_translation(facts, event, x, y, z);
wz_self_add_world_translation(facts, event, x, y, z);
wz_self_set_world_translation(facts, event, x, y, z);
wz_self_add_local_scale(facts, event, x, y, z);
wz_self_set_local_scale(facts, event, x, y, z);
wz_self_set_local_rotation(facts, event, rotation);
wz_self_set_linear_velocity(facts, event, x, y, z);
wz_self_set_angular_velocity(facts, event, x, y, z);
wz_self_set_motion_space(facts, event, WZ_BEHAVIOR_MOTION_SPACE_WORLD);
```

Other command helpers are useful in pair events:

```cpp
wz_other_add_world_translation(facts, event, x, y, z);
wz_other_set_world_translation(facts, event, x, y, z);
wz_other_set_linear_velocity(facts, event, x, y, z);
wz_other_set_angular_velocity(facts, event, x, y, z);
wz_other_set_motion_space(facts, event, WZ_BEHAVIOR_MOTION_SPACE_LOCAL);
```

Generic entity command helpers:

```cpp
wz_write_add_local_translation(facts, entity, x, y, z);
wz_write_set_local_translation(facts, entity, x, y, z);
wz_write_add_world_translation(facts, entity, x, y, z);
wz_write_set_world_translation(facts, entity, x, y, z);
wz_write_add_local_scale(facts, entity, x, y, z);
wz_write_set_local_scale(facts, entity, x, y, z);
wz_write_set_local_rotation(facts, entity, rotation);
wz_write_set_linear_velocity(facts, entity, x, y, z);
wz_write_set_angular_velocity(facts, entity, x, y, z);
wz_write_set_motion_space(facts, entity, WZ_BEHAVIOR_MOTION_SPACE_WORLD);
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

## Motion, Rotation, And Scale Semantics

[Back to Behavior API Inventory](#behavior-api-inventory)

The scene graph stores local transforms as matrices. The V1 behavior API uses
these rules:

- Local translation writes the matrix translation column directly.
- World translation writes are converted into the entity's parent-local
  translation before scene transforms are propagated.
- Local scale changes local basis-column lengths and preserves basis direction.
- Local rotation uses `WzQuaternion {x, y, z, w}` and replaces rotation.
- Set rotation preserves local translation and current basis-column scale.
- Additive local rotation is intentionally not part of V1.

Linear velocity is motion in units per second. Angular velocity is an
axis-angle vector whose direction is the rotation axis and whose magnitude is
radians per second. Motion defaults to world space. Switch to local-space
motion with:

```cpp
wz_self_set_motion_space(facts, event, WZ_BEHAVIOR_MOTION_SPACE_LOCAL);
```

Motion space constants:

```text
WZ_BEHAVIOR_MOTION_SPACE_WORLD
WZ_BEHAVIOR_MOTION_SPACE_LOCAL
```

The engine stores velocities as runtime motion state, then integrates them once
per frame after behavior commands are applied and before render prep. Setting
velocity does not immediately move or rotate the entity; the frame integration
step applies `velocity * delta_seconds`.

Local-space angular velocity composes in the node's local frame. World-space
angular velocity composes in the world frame. For parented nodes, the runtime
converts through the parent's world rotation before writing the node's local
transform. If the local or parent/world transform cannot be decomposed as safe
TRS, angular integration for that node is skipped for the frame.

## Frame Timing

[Back to Behavior API Inventory](#behavior-api-inventory)

Frame timing helpers:

```cpp
float dt = wz_delta_seconds(facts);
uint64_t frame = wz_frame_index(facts);
```

`delta_seconds` is the current frame interval.

`facts->timing->elapsed_seconds` is also available when `facts->timing` is
non-null. It is the engine monotonic clock value at the frame end; it is not
reset per scene or per behavior module.

## Logging

[Back to Behavior API Inventory](#behavior-api-inventory)

Use the frame facts logger callback through the helper:

```cpp
wz_log_info(facts, "hello from behavior");
```

`wz_log_info` ignores null facts, missing logger callbacks, and null messages.

## Raw ABI Registration

[Back to Behavior API Inventory](#behavior-api-inventory)

Most modules should use `WZ_BEHAVIOR_MODULE` or
`WZ_BEHAVIOR_MODULE_EVENTS`. The `_EVENTS` form declares default channel
subscriptions for behavior bindings that omit an authored `events` list:

```cpp
static const char* kEvents[] = {
    "input.*",
    "frame.update",
};

WZ_BEHAVIOR_MODULE_EVENTS("player_move", on_event, kEvents)
```

If a DLL needs to register multiple modules or pass module-specific user data,
implement `wz_register_behaviors` directly and call
`api->register_module_desc`:

```cpp
extern "C" WZ_BEHAVIOR_MODULE_EXPORT uint8_t wz_register_behaviors(
    WzBehaviorPluginApi* api)
{
    if (!api || api->version != WZ_BEHAVIOR_ABI_VERSION
        || !api->register_module_desc)
    {
        return 0;
    }

    static const char* move_events[] = { "input.*", "frame.update" };
    const WzBehaviorModuleDesc move{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "move",
        .on_event = move_event,
        .event_channels = move_events,
        .event_channel_count = 2,
        .module_user_data = nullptr,
    };

    static const char* snap_events[] = { "proximity.enter" };
    const WzBehaviorModuleDesc snap{
        .size = sizeof(WzBehaviorModuleDesc),
        .module = "snap",
        .on_event = snap_event,
        .event_channels = snap_events,
        .event_channel_count = 1,
        .module_user_data = nullptr,
    };

    uint8_t ok = 1;
    ok &= api->register_module_desc(api->user, &move);
    ok &= api->register_module_desc(api->user, &snap);
    return ok;
}
```

`api->register_module` is still available for modules with no declared default
subscriptions.

The older `api->register_behavior` callback is still present for legacy
behavior functions with the signature:

```cpp
void legacy_behavior(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    void* user_data);
```

New scene-authored behavior should prefer module event handlers because they
receive the full `WzBehaviorEvent`, including event kind, `self`, `other`, and
trigger state.

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

For a minimal angular motion module, see
[`examples/behavior_plugin_template/angular_motion_plugin.cpp`](../../examples/behavior_plugin_template/angular_motion_plugin.cpp).

## Current Limits

The V1 API intentionally does not include:

- Additive local rotation
- Physics forces
- Collision contact normals or penetration depth
- Script runtime integration
- Hot reload guarantees

Those can be added later without changing the basic event-handler shape.
