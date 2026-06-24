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

Helpers that return `uint8_t` use `1` for success/true and `0` for
failure/false unless that helper's section says otherwise.

### Registration And Scene Binding

- Register one module with no default subscriptions:
  [`WZ_BEHAVIOR_MODULE(module_name, handler_fn)`](#minimal-module).
- Register one module with default event subscriptions:
  [`WZ_BEHAVIOR_MODULE_EVENTS(module_name, handler_fn, event_channel_array)`](#raw-abi-registration).
- Register one module with an init callback and default event subscriptions:
  [`WZ_BEHAVIOR_MODULE_INIT(module_name, init_fn, handler_fn, event_channel_array)`](#init-and-behavior-state).
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
  `WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED`, and
  `WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED`.
- Event helper signatures and examples are in
  [Event Routing](#event-routing).
- Event channel tokens: `frame.update`, `scene.loaded`, `collision.enter`,
  `collision.stay`, `collision.exit`, `collision.*`, `proximity.enter`,
  `proximity.stay`, `proximity.exit`, `proximity.*`,
  `input.key.pressed`, `input.key.released`,
  `input.mouse_button.pressed`, `input.mouse_button.released`,
  `input.controller_button.pressed`, `input.controller_button.released`,
  `input.controller_axis.changed`, and
  [`input.*`](#event-channel-tokens).

### Input Dispatch And Snapshot Reads

- Edge-event payload helper signatures and examples are in
  [Input Events](#input-events).
- Keyboard, mouse, window, controller, and WASD snapshot helper signatures are
  in [Reading Input](#reading-input).

### Things To Do With Motion And Transforms

- Move, rotate, scale, and velocity command signatures are in
  [Writing Commands](#writing-commands).
- Motion spaces: `WZ_BEHAVIOR_MOTION_SPACE_WORLD` and
  [`WZ_BEHAVIOR_MOTION_SPACE_LOCAL`](#motion-rotation-and-scale-semantics).
- Engine terrain movement constraints are described in
  [Motion, Rotation, And Scale Semantics](#motion-rotation-and-scale-semantics).

### Authoring The Running Scene

- Spawn, remove, reparent, set the renderable on, and add or remove components
  of scene nodes from a behavior, as deferred frame-boundary requests, in
  [Runtime Scene Authoring](#runtime-scene-authoring).

### Reading Scene State

- Transform and position read signatures are in
  [Reading Transforms](#reading-transforms).
- Entity lookup signatures are in
  [Scene Lookup And Config](#scene-lookup-and-config).

### Init And Behavior State

- [Init callbacks and state helper signatures](#init-and-behavior-state) are in
  the Init And Behavior State section.
- Per-binding state helpers:
  [`void* wz_alloc_instance_state(...)`](#init-and-behavior-state),
  [`void* wz_alloc_instance_state_desc(...)`](#init-and-behavior-state), and
  [`void* wz_get_instance_state(...)`](#init-and-behavior-state).
- Shared state helpers:
  [`void* wz_create_shared_state(...)`](#init-and-behavior-state),
  [`void* wz_create_shared_state_desc(...)`](#init-and-behavior-state), and
  [`void* wz_find_shared_state(...)`](#init-and-behavior-state).

### Spatial Relationship And Collision Surface Queries

- Vector, distance, and direction helper signatures are in
  [Direction And Distance Helpers](#direction-and-distance-helpers).
- Collision surface query signatures are in
  [Querying Collision Surfaces](#querying-collision-surfaces).
- Terrain height/normal sample signatures and terrain-following examples are in
  [Sampling Terrain Surfaces](#sampling-terrain-surfaces).

### Authored Config, Timing, And Diagnostics

- Config helper signatures are in
  [Scene Lookup And Config](#scene-lookup-and-config).
- Frame timing helper signatures are in [Frame Timing](#frame-timing).
- Logging helper signatures are in [Logging](#logging).

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
WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED
```

`WZ_EVENT_SCENE_LOADED` is reserved in the ABI and channel table; current
runtime dispatch sends frame, collision, proximity, and input events.

Event helper signatures:

```cpp
WzBehaviorEventKind wz_event_kind(const WzBehaviorEvent* event);
uint8_t wz_is_event(
    const WzBehaviorEvent* event,
    WzBehaviorEventKind kind);
const char* wz_event_name(WzBehaviorEventKind kind);
WzBehaviorEntityId wz_self(const WzBehaviorEvent* event);
WzBehaviorEntityId wz_other(const WzBehaviorEvent* event);
uint8_t wz_self_is_trigger(const WzBehaviorEvent* event);
```

`wz_self(event)` is the scene node whose Behavior component is handling the
event. `wz_other(event)` is the collision or proximity partner for pair events.
For frame and scene-loaded events, `other` is `WZ_INVALID_BEHAVIOR_ENTITY`.

`wz_self_is_trigger(event)` is set for collision/proximity events when the
receiving side is a trigger participant.

For a single event check, `wz_is_event` is the shortest guard:

```cpp
if (wz_is_event(event, WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED)) {
    uint32_t controller = wz_input_event_controller(facts);
    uint32_t axis = wz_input_event_controller_axis(facts);
    float value = wz_input_event_controller_axis_value(facts);
}
```

For a behavior that handles several event kinds, switch on
`wz_event_kind(event)`:

```cpp
switch (wz_event_kind(event)) {
case WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED: {
    uint32_t controller = wz_input_event_controller(facts);
    uint32_t axis = wz_input_event_controller_axis(facts);
    float value = wz_input_event_controller_axis_value(facts);
    break;
}
case WZ_EVENT_COLLISION_ENTER: {
    WzVec3 direction{};
    if (wz_direction_self_to_other(facts, event, &direction)) {
        // React to the collision partner.
    }
    break;
}
case WZ_EVENT_FRAME_UPDATE: {
    float dt = wz_delta_seconds(facts);
    break;
}
default:
    break;
}
```

## Event Channel Tokens

[Back to Behavior API Inventory](#behavior-api-inventory)

Scene behavior bindings and plugin defaults use the same channel tokens:

| Token | Event kind | How to inspect it |
| --- | --- | --- |
| [`frame.update`](#frame-update-events) | `WZ_EVENT_FRAME_UPDATE` | See [Event Routing](#event-routing) and [Frame Timing](#frame-timing). |
| [`scene.loaded`](#event-routing) | `WZ_EVENT_SCENE_LOADED` | Reserved in the ABI and channel table; current runtime dispatch does not send it yet. |
| [`collision.enter`](#collision-events) | `WZ_EVENT_COLLISION_ENTER` | See [Event Routing](#event-routing), [Reading Transforms](#reading-transforms), and [Direction And Distance Helpers](#direction-and-distance-helpers). |
| [`collision.stay`](#collision-events) | `WZ_EVENT_COLLISION_STAY` | Same inspection pattern as collision enter. |
| [`collision.exit`](#collision-events) | `WZ_EVENT_COLLISION_EXIT` | Same inspection pattern as collision enter. |
| [`collision.*`](#collision-events) | All collision events | See [Event Routing](#event-routing) for filtering inside the handler. |
| [`proximity.enter`](#proximity-events) | `WZ_EVENT_PROXIMITY_ENTER` | See [Event Routing](#event-routing), [Reading Transforms](#reading-transforms), and [Direction And Distance Helpers](#direction-and-distance-helpers). |
| [`proximity.stay`](#proximity-events) | `WZ_EVENT_PROXIMITY_STAY` | Same inspection pattern as proximity enter. |
| [`proximity.exit`](#proximity-events) | `WZ_EVENT_PROXIMITY_EXIT` | Same inspection pattern as proximity enter. |
| [`proximity.*`](#proximity-events) | All proximity events | See [Event Routing](#event-routing) for filtering inside the handler. |
| [`input.key.pressed`](#input-events) | `WZ_EVENT_INPUT_KEY_PRESSED` | See [Input Events](#input-events). |
| [`input.key.released`](#input-events) | `WZ_EVENT_INPUT_KEY_RELEASED` | See [Input Events](#input-events). |
| [`input.mouse_button.pressed`](#input-events) | `WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED` | See [Input Events](#input-events). |
| [`input.mouse_button.released`](#input-events) | `WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED` | See [Input Events](#input-events). |
| [`input.controller_button.pressed`](#input-events) | `WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED` | See [Input Events](#input-events). |
| [`input.controller_button.released`](#input-events) | `WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED` | See [Input Events](#input-events). |
| [`input.controller_axis.changed`](#input-events) | `WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED` | See [Input Events](#input-events). |
| [`input.*`](#input-events) | All input events | See [Event Routing](#event-routing) and [Input Events](#input-events). |

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
input.controller_axis.changed
input.*
```

`input.*` is a hardcoded "all input events" token, not a general glob system.
The engine derives these events from the current frame input snapshot, so a
pressed or released edge is delivered for the frame in which it appears.

Input event payload helpers read the active routed input event:

```cpp
uint32_t wz_input_event_key(const WzBehaviorFrameFacts* facts);
uint32_t wz_input_event_mouse_button(const WzBehaviorFrameFacts* facts);
uint32_t wz_input_event_controller(const WzBehaviorFrameFacts* facts);
uint32_t wz_input_event_controller_button(
    const WzBehaviorFrameFacts* facts);
uint32_t wz_input_event_controller_axis(
    const WzBehaviorFrameFacts* facts);
float wz_input_event_controller_axis_value(
    const WzBehaviorFrameFacts* facts);
```

Payload helpers return `WZ_INPUT_EVENT_INVALID_VALUE` when the current dispatch
is not an input event or when that field does not apply. During an input event,
the normal frame snapshot helpers still read `facts->input`, so a behavior can
combine the routed edge with held-state or axis checks.

Example input event filters:

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

if (wz_is_event(event, WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED)) {
    uint32_t controller = wz_input_event_controller(facts);
    uint32_t axis = wz_input_event_controller_axis(facts);
    float value = wz_input_event_controller_axis_value(facts);
}
```

## Reading Input

[Back to Behavior API Inventory](#behavior-api-inventory)

Input is exposed in two forms:

- Edge events for key, mouse button, and controller button pressed/released
  transitions, plus controller axis changed events.
- A frame snapshot through `facts->input` for held state, mouse position/delta,
  window state, controller connection state, and controller axes.

Use event channels when behavior should only run on a transition. Use
`WZ_EVENT_FRAME_UPDATE` when behavior must continuously react to held state,
analog axes, timers, or per-frame motion.

Keyboard helpers:

```cpp
uint8_t wz_key_down(const WzBehaviorFrameFacts* facts, uint32_t key);
uint8_t wz_key_pressed(const WzBehaviorFrameFacts* facts, uint32_t key);
uint8_t wz_key_released(const WzBehaviorFrameFacts* facts, uint32_t key);
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
uint8_t wz_mouse_button_down(
    const WzBehaviorFrameFacts* facts,
    uint32_t button);
uint8_t wz_mouse_button_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t button);
uint8_t wz_mouse_button_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t button);
int32_t wz_mouse_x(const WzBehaviorFrameFacts* facts);
int32_t wz_mouse_y(const WzBehaviorFrameFacts* facts);
int32_t wz_mouse_dx(const WzBehaviorFrameFacts* facts);
int32_t wz_mouse_dy(const WzBehaviorFrameFacts* facts);
uint8_t wz_window_focused(const WzBehaviorFrameFacts* facts);
int32_t wz_window_width(const WzBehaviorFrameFacts* facts);
int32_t wz_window_height(const WzBehaviorFrameFacts* facts);
```

Mouse button constants:

```text
WZ_MOUSE_BUTTON_LEFT
WZ_MOUSE_BUTTON_RIGHT
WZ_MOUSE_BUTTON_MIDDLE
```

Controller helpers:

```cpp
uint8_t wz_controller_count(const WzBehaviorFrameFacts* facts);
uint8_t wz_controller_connected(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller);
uint8_t wz_controller_connected_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller);
uint8_t wz_controller_connected_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller);
float wz_controller_axis(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t axis);
uint8_t wz_controller_button_down(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button);
uint8_t wz_controller_button_pressed(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button);
uint8_t wz_controller_button_released(
    const WzBehaviorFrameFacts* facts,
    uint32_t controller,
    uint32_t button);
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

Controller axis changed events are generated when a sampled axis value changes
from the previous frame by more than a small epsilon. They are useful for
event-driven analog input, including sticks and analog triggers. For continuous
movement, the frame snapshot helpers remain the most direct way to read the
current axis value.

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

`wz_input_wasd_axis` writes a horizontal movement axis:

```cpp
uint8_t wz_input_wasd_axis(
    const WzBehaviorFrameFacts* facts,
    WzVec3* out_axis);

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
uint8_t wz_self_local_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform);
uint8_t wz_self_world_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform);
uint8_t wz_self_local_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position);
uint8_t wz_self_world_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position);
uint8_t wz_other_local_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform);
uint8_t wz_other_world_transform(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzMat4* out_transform);
uint8_t wz_other_local_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position);
uint8_t wz_other_world_position(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_position);
```

Generic entity queries:

```cpp
uint8_t wz_read_local_transform(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzMat4* out_transform);
uint8_t wz_read_world_transform(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzMat4* out_transform);
uint8_t wz_read_local_position(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzVec3* out_position);
uint8_t wz_read_world_position(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzVec3* out_position);
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
uint8_t wz_vector_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    WzVec3* out_vector);
uint8_t wz_distance_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    float* out_distance);
uint8_t wz_direction_between_world_positions(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId from_entity,
    WzBehaviorEntityId to_entity,
    WzVec3* out_direction);
uint8_t wz_vector_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_vector);
uint8_t wz_distance_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float* out_distance);
uint8_t wz_direction_self_to_other(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzVec3* out_direction);
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
uint8_t wz_query_collision_surface_ray(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId surface_entity,
    WzVec3 origin,
    WzVec3 direction,
    float max_distance,
    WzSurfaceSample* out_sample);
```

The ray uses world-space `origin` and `direction` and returns the nearest hit
within `max_distance`. It reads from `FrameStorage::collision.world`, so it
uses collision assets already resolved for the frame. V1 supports queryable
`TerrainMeshSurface` collision assets. Use
[`wz_sample_terrain_surface`](#sampling-terrain-surfaces) for routine terrain
height/normal reads, including height-field terrain.

For `TerrainMeshSurface` collision assets with a compiled surface grid, the
engine restricts ray tests to nearby grid cells and triangle bounds before
testing triangles. Ungridded surface data falls back to a full triangle scan.
For routine ground-following movement, prefer the terrain sampling API; ray
queries are still a collision-surface query, not a dedicated terrain height API.

## Sampling Terrain Surfaces

[Back to Behavior API Inventory](#behavior-api-inventory)

Behavior code can sample a specific queryable terrain entity at a world-space
horizontal location:

```cpp
uint8_t wz_sample_terrain_surface(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId terrain_entity,
    float world_x,
    float world_z,
    WzSurfaceSample* out_sample);
```

`world_x` and `world_z` are the location to sample. On success, the helper
returns `1`, sets `out_sample->hit` to `1`, and writes:

- `out_sample->surface_entity`: the terrain entity that was sampled.
- `out_sample->position`: the sampled world-space surface point.
- `out_sample->normal`: the sampled world-space surface normal.

The helper returns `0` and leaves `out_sample->hit` as `0` when the entity is
missing, disabled, not queryable, not terrain, outside the terrain extent, or
when `out_sample` is null.

Height-field terrain uses the compiled height samples directly and bilinearly
interpolates height. Mesh-surface terrain uses the compiled surface grid when
available and falls back to a local triangle scan only for ungridded data.

For routine tank or character ground following, prefer the engine terrain
movement constraint when the actor can be authored with Motion
`terrain_constrained = true` and the terrain can be authored with Terrain
`constrain_movement = true`. That runtime step samples eligible terrain once
after behavior commands and velocity integration. Use
`wz_sample_terrain_surface` when the behavior needs explicit surface data, such
as reading the normal for custom orientation, effects, or gameplay logic.

For a tank or character, resolve the terrain entity once during init, store it
in per-binding state, and sample during `frame.update`:

```cpp
struct TankState
{
    WzBehaviorEntityId terrain = WZ_INVALID_BEHAVIOR_ENTITY;
    float ride_height = 0.35f;
};

void tank_init(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId,
    void*)
{
    auto* state = static_cast<TankState*>(
        wz_alloc_instance_state(facts, sizeof(TankState), alignof(TankState)));
    if (!state) {
        return;
    }

    *state = TankState{};
    wz_find_entity_by_authored_id(facts, "terrain", &state->terrain);
    wz_config_float(facts, "ride_height", &state->ride_height);
}

void tank_on_event(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    void*)
{
    if (!wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
        return;
    }

    auto* state = static_cast<TankState*>(wz_get_instance_state(facts));
    if (!state || state->terrain == WZ_INVALID_BEHAVIOR_ENTITY) {
        return;
    }

    WzVec3 tank_position{};
    WzSurfaceSample terrain{};
    if (wz_self_world_position(facts, event, &tank_position)
        && wz_sample_terrain_surface(
            facts,
            state->terrain,
            tank_position.x,
            tank_position.z,
            &terrain)
        && terrain.hit)
    {
        wz_self_set_world_translation(
            facts,
            event,
            tank_position.x,
            terrain.position.y + state->ride_height,
            tank_position.z);
    }
}

const char* tank_events[] = { "frame.update" };

WZ_BEHAVIOR_MODULE_INIT(
    "tank_terrain_follow",
    tank_init,
    tank_on_event,
    tank_events)
```

The example writes the actor's world Y while preserving its current world X/Z.
The returned normal is available for later pitch/roll alignment.

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

Read authored config during dispatch or init:

```cpp
uint8_t wz_config_bool(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    uint8_t* out_value);
uint8_t wz_config_number(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    double* out_value);
uint8_t wz_config_float(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    float* out_value);
uint8_t wz_config_string(
    const WzBehaviorFrameFacts* facts,
    const char* key,
    char* out_buffer,
    uint32_t buffer_size,
    uint32_t* out_required_size);
```

The same helpers are overloaded for `const WzBehaviorInitFacts*`, so init
callbacks can read authored config before creating or joining state.

Example:

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
uint8_t wz_find_entity_by_authored_id(
    const WzBehaviorFrameFacts* facts,
    const char* authored_id,
    WzBehaviorEntityId* out_entity);
uint8_t wz_find_entity_by_name(
    const WzBehaviorFrameFacts* facts,
    const char* name,
    WzBehaviorEntityId* out_entity);
```

`wz_find_entity_by_authored_id` resolves stable scene node ids. Prefer authored
ids for gameplay bindings. `wz_find_entity_by_name` resolves display names and
is useful for prototypes, but names are easier to change accidentally.

## Init And Behavior State

[Back to Behavior API Inventory](#behavior-api-inventory)

Use an init callback when a behavior needs state that should live with the
scene instance instead of in a C++ `static`. Init runs after scene
materialization and before event dispatch. Parent/root node bindings initialize
before child bindings.

There are three practical state scopes:

- Module/global state is ordinary C or C++ data in the plugin DLL. It is useful
  for constants and caches, but it is lost on hot reload and is shared by every
  scene and every node using that DLL.
- Per-binding state belongs to one authored behavior binding. Use it for data
  such as a tank's local throttle smoothing, cooldowns, counters, or cached
  target entity IDs. The scene editor gives each binding a durable `id`; the
  human-facing `label` can change without changing the state identity.
- Shared state is created with an authored key such as `"tank_group.main"` and
  then found by any behavior that knows that key. Use it for coordinator /
  participant patterns where several entities intentionally share one object.

The scene editor exposes behavior `config` rows. A common pattern is to add a
string config key such as `shared_state_key` and read it in init before calling
`wz_create_shared_state` or `wz_find_shared_state`.

Register a module with both init and event callbacks:

```cpp
static const char* kTankEvents[] = { "input.*", "frame.update" };

WZ_BEHAVIOR_MODULE_INIT(
    "tank_controller",
    tank_init,
    tank_event,
    kTankEvents)
```

The init callback shape is:

```cpp
void tank_init(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId entity,
    void* user_data);
```

Per-binding state gives each behavior binding its own persistent block:

```cpp
void* wz_alloc_instance_state(
    const WzBehaviorInitFacts* facts,
    uint32_t size,
    uint32_t alignment);
void* wz_alloc_instance_state_desc(
    const WzBehaviorInitFacts* facts,
    const WzBehaviorStateDesc* desc);
void* wz_get_instance_state(const WzBehaviorInitFacts* facts);
void* wz_get_instance_state(const WzBehaviorFrameFacts* facts);
```

Call `wz_alloc_instance_state` from init. A later `wz_get_instance_state` in
init or event dispatch returns the same block for that behavior binding. If
init runs again and the requested size/alignment still match, the existing
block is reused.

Use the descriptor form when a state layout changes over time:

```cpp
typedef struct WzBehaviorStateDesc
{
    uint32_t size;
    uint32_t alignment;
    uint32_t layout_version;
} WzBehaviorStateDesc;
```

`layout_version` is plugin-authored. Matching size, alignment, and
`layout_version` preserve an existing block across repeated init or hot reload.
Changing any of them resets the block and logs a warning. Version `0` means
the simple size/alignment compatibility behavior used by
`wz_alloc_instance_state`.

Shared state lets several behavior bindings deliberately use the same block:

```cpp
void* wz_create_shared_state(
    const WzBehaviorInitFacts* facts,
    const char* key,
    uint32_t size,
    uint32_t alignment);
void* wz_create_shared_state_desc(
    const WzBehaviorInitFacts* facts,
    const char* key,
    const WzBehaviorStateDesc* desc);
void* wz_find_shared_state(
    const WzBehaviorInitFacts* facts,
    const char* key);
void* wz_find_shared_state(
    const WzBehaviorFrameFacts* facts,
    const char* key);
```

`wz_create_shared_state` is init-only. It creates or reuses a block keyed by an
authored string such as `"tank_group.main"`. `wz_find_shared_state` can be used
from init and event dispatch; it returns `nullptr` if the key has not been
created. Use `wz_create_shared_state_desc` when shared-state layout changes
should reset by explicit version rather than by size/alignment only.

Descriptor example:

```cpp
struct TankStateV2
{
    float throttle;
    float turn;
};

const WzBehaviorStateDesc desc{
    sizeof(TankStateV2),
    alignof(TankStateV2),
    2u,
};

auto* state = static_cast<TankStateV2*>(
    wz_alloc_instance_state_desc(facts, &desc));
```

Coordinator/participant sketch:

```cpp
struct TankGroup
{
    float throttle;
    uint32_t members;
};

void read_group_key(
    const WzBehaviorInitFacts* facts,
    char (&out_key)[64])
{
    std::snprintf(out_key, sizeof(out_key), "%s", "tank_group.main");
    wz_config_string(
        facts,
        "shared_state_key",
        out_key,
        sizeof(out_key),
        nullptr);
}

void group_init(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId,
    void*)
{
    char group_key[64]{};
    read_group_key(facts, group_key);
    auto* group = static_cast<TankGroup*>(
        wz_create_shared_state(
            facts,
            group_key,
            sizeof(TankGroup),
            alignof(TankGroup)));
    if (group && group->members == 0) {
        group->throttle = 0.0f;
    }
}

void tank_init(
    const WzBehaviorInitFacts* facts,
    WzBehaviorEntityId,
    void*)
{
    char group_key[64]{};
    read_group_key(facts, group_key);
    auto* group = static_cast<TankGroup*>(
        wz_find_shared_state(facts, group_key));
    if (group) {
        ++group->members;
    }
}

void tank_event(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    void*)
{
    auto* group = static_cast<TankGroup*>(
        wz_find_shared_state(facts, "tank_group.main"));
    if (group && wz_is_event(event, WZ_EVENT_FRAME_UPDATE)) {
        // Read or update the shared group state here.
    }
}
```

Init facts intentionally do not expose command writing, input state, or active
events. Use init for state setup, config checks, entity lookup, transform reads,
and logging. Use event callbacks for frame/input/collision behavior.

When a behavior DLL is reloaded, the host re-registers modules into the same
registry. Call `initialize_behaviors` again after a successful reload. Compatible
per-binding and shared state blocks are reused; incompatible size/alignment
requests reset the block and log a warning. Avoid storing DLL-owned pointers,
function pointers, vtables, or plugin-allocated STL objects in engine-owned
state.

## Writing Commands

[Back to Behavior API Inventory](#behavior-api-inventory)

Behaviors do not mutate the scene graph directly. They write commands into the
frame command buffer. The engine applies those commands after behavior dispatch
and then propagates scene transforms.

All command helpers return `1` if the command was accepted by the command
writer and `0` if there was no writer or the target entity was invalid.

Self command helpers:

```cpp
uint8_t wz_self_add_local_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_self_set_local_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_self_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_self_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_self_add_local_scale(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_self_set_local_scale(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_self_set_local_rotation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzQuaternion rotation);
uint8_t wz_self_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_self_set_angular_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_self_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorMotionSpace space);
```

Other command helpers are useful in pair events:

```cpp
uint8_t wz_other_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_other_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_other_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_other_set_angular_velocity(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    float x,
    float y,
    float z);
uint8_t wz_other_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorMotionSpace space);
```

Generic entity command helpers:

```cpp
uint8_t wz_write_add_local_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z);
uint8_t wz_write_set_local_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z);
uint8_t wz_write_add_world_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z);
uint8_t wz_write_set_world_translation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z);
uint8_t wz_write_add_local_scale(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z);
uint8_t wz_write_set_local_scale(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z);
uint8_t wz_write_set_local_rotation(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzQuaternion rotation);
uint8_t wz_write_set_linear_velocity(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z);
uint8_t wz_write_set_angular_velocity(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    float x,
    float y,
    float z);
uint8_t wz_write_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzBehaviorMotionSpace space);
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
uint8_t wz_self_set_motion_space(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorMotionSpace space);
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

Scene-authored Motion can opt into runtime terrain height ownership with:

```json
"motion": {
  "linear_velocity": [0.0, 0.0, 0.0],
  "terrain_constrained": true,
  "terrain_ride_height": 0.35,
  "terrain_footprint_radius": 0.0,
  "terrain_align_to_surface": true,
  "terrain_alignment_strength": 1.0
}
```

The terrain node must also opt in:

```json
"terrain": {
  "asset": "asset-key:...",
  "constraint_surface": {
    "asset": "asset-key:..."
  },
  "constrain_movement": true
}
```

When both sides opt in, the runtime samples every eligible terrain surface at
the actor's current world X/Z, chooses the highest hit, and writes world Y to
`surface_height + terrain_ride_height`. X/Z are preserved. If exact mesh
sampling misses a small gap in an accepted terrain surface, the movement
constraint can use nearby terrain triangle planes to estimate height at the
actor's X/Z.

`constraint_surface` is optional. Use it when the visual terrain is dense or
adaptive and movement only needs a cheaper projection surface. It references a
regular collision asset in the terrain node's local space. The movement
constraint samples that proxy instead of the terrain node's ordinary collision
entry, and the proxy does not create broadphase collision pairs or collision
events.

For adaptive mesh terrain, build the constraint surface as a regular terrain
projection heightfield. The projection resolution is authored separately from
the visual mesh density, so the render terrain can keep millions of adaptive
triangles while movement samples a predictable X/Z grid. Higher resolutions
preserve more surface detail but cost more memory and longer build time; query
cost stays independent of the visual mesh triangle count. Heightfield sampling
uses smooth interpolation for both height and normal so actors do not inherit
the adaptive mesh's triangle-density steps or flat face-normal jumps.

`terrain_footprint_radius` is optional and defaults to `0`, which means the
actor is constrained as a point at its pivot. When it is positive, the runtime
also samples a fixed ring around the actor and uses the highest support height.
Use this for vehicle-sized actors that should ride over terrain detail under
their body instead of letting small raised areas clip through the mesh.

`terrain_align_to_surface` is optional. When it is true, the runtime also
orients the actor so its local Y axis follows the sampled terrain normal. The
actor's current local Z/forward direction is projected onto the terrain tangent
plane, so heading is preserved as much as the surface allows.
`terrain_alignment_strength` is clamped to `[0, 1]`; `1` snaps to the surface
orientation this frame, and values below `1` blend from the current rotation
toward the terrain orientation. This is a first smoothing control, not a full
temporal smoothing policy.

The step runs after behavior commands and motion integration, before render
prep. V1 leaves vertical velocity unchanged, so terrain-constrained behaviors
should set horizontal velocity and let the runtime constraint own the actor's
ground height.

Local-space angular velocity composes in the node's local frame. World-space
angular velocity composes in the world frame. For parented nodes, the runtime
converts through the parent's world rotation before writing the node's local
transform. If the local or parent/world transform cannot be decomposed as safe
TRS, angular integration for that node is skipped for the frame.

## Runtime Scene Authoring

[Back to Behavior API Inventory](#behavior-api-inventory)

Beyond moving entities, a behavior can author the running scene's structure:
spawn and remove nodes, reparent them, set a node's renderable, and add or
remove optional components. These are the same operations the scene editor's
host performs, routed through the same engine apply path.

Like motion commands, scene-authoring requests are **deferred**. They are queued
during dispatch and applied at the frame boundary, after every behavior has run,
never mid-dispatch, so they cannot invalidate the scene while the engine is
iterating it. A request is therefore not visible to transform or scene queries
until the next frame.

Each helper is **fire-and-forget** and returns `1` if the request was accepted
(the target entity resolved to a scene node) and `0` otherwise. No node id is
returned; address a node you spawned by giving it state or by finding it by
authored id on a later frame.

Self forms act on the handling node (`wz_self(event)`); generic forms take an
explicit entity.

```cpp
uint8_t wz_spawn_child(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId parent_entity);
uint8_t wz_remove_node(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity);
uint8_t wz_reparent_node(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    WzBehaviorEntityId new_parent_entity);
uint8_t wz_set_renderable_asset(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    uint64_t asset_graph_node_id);
uint8_t wz_add_node_component(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* kind);
uint8_t wz_remove_node_component(
    const WzBehaviorFrameFacts* facts,
    WzBehaviorEntityId entity,
    const char* kind);
```

Self convenience forms:

```cpp
uint8_t wz_self_spawn_child(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event);
uint8_t wz_self_remove_node(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event);
uint8_t wz_self_reparent_node(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    WzBehaviorEntityId new_parent_entity);
uint8_t wz_self_detach_to_top_level(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event);
uint8_t wz_self_set_renderable_asset(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    uint64_t asset_graph_node_id);
uint8_t wz_self_add_node_component(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    const char* kind);
uint8_t wz_self_remove_node_component(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    const char* kind);
```

Notes:

- `wz_spawn_child` adds a child node under `parent_entity`. `wz_remove_node`
  removes a node and its subtree.
- `wz_reparent_node` moves a node under `new_parent_entity`. Pass
  `WZ_INVALID_BEHAVIOR_ENTITY` as the new parent (or use
  `wz_self_detach_to_top_level`) to detach the node to the top level.
- `wz_set_renderable_asset` binds the node's preferred asset-graph renderable to
  `asset_graph_node_id`; `0` clears it. It references a node the asset graph has
  already compiled; it never compiles anything.
- Component `kind` is one of `"camera"`, `"proximity"`, `"collision"`, or
  `"motion"`. The renderable is authored with `wz_set_renderable_asset`, not the
  component helpers.

Example — a pickup that despawns when a player gets close:

```cpp
void pickup_on_event(
    const WzBehaviorFrameFacts* facts,
    const WzBehaviorEvent* event,
    void*)
{
    if (wz_is_event(event, WZ_EVENT_PROXIMITY_ENTER)) {
        wz_self_remove_node(facts, event);
    }
}
```

These requests are limited to cheap, live scene-graph edits. A behavior cannot
author behavior bindings, edit the asset graph, or do anything that recompiles
an asset — those operations stay with the editor host. This is by construction:
only the cheap edits above are exposed to behaviors at all.

## Frame Timing

[Back to Behavior API Inventory](#behavior-api-inventory)

Frame timing helpers:

```cpp
float wz_delta_seconds(const WzBehaviorFrameFacts* facts);
uint64_t wz_frame_index(const WzBehaviorFrameFacts* facts);
```

`delta_seconds` is the current frame interval.

`facts->timing->elapsed_seconds` is also available when `facts->timing` is
non-null. It is the engine monotonic clock value at the frame end; it is not
reset per scene or per behavior module.

## Logging

[Back to Behavior API Inventory](#behavior-api-inventory)

Use the frame facts logger callback through the helper:

```cpp
void wz_log_info(
    const WzBehaviorFrameFacts* facts,
    const char* message);
void wz_log_infof(
    const WzBehaviorFrameFacts* facts,
    const char* format,
    ...);
```

Example:

```cpp
wz_log_info(facts, "hello from behavior");
wz_log_infof(facts, "axis %u value %.2f", axis, value);
```

`wz_log_info` ignores null facts, missing logger callbacks, and null messages.
`wz_log_infof` formats into a fixed local buffer, then forwards the result to
`wz_log_info`.

Common `wz_log_infof` format codes:

| Code | Use for | Example |
| --- | --- | --- |
| `%s` | Null-terminated text | `wz_log_infof(facts, "node=%s", name);` |
| `%d` or `%i` | Signed integers | `wz_log_infof(facts, "count=%d", count);` |
| `%u` | Unsigned integers, including most behavior ids and constants | `wz_log_infof(facts, "entity=%u", wz_self(event));` |
| `%f` | Floating-point values | `wz_log_infof(facts, "speed=%f", speed);` |
| `%.2f` | Floating-point values with two digits after the decimal | `wz_log_infof(facts, "axis=%.2f", value);` |
| `%c` | A single character | `wz_log_infof(facts, "key=%c", 'W');` |
| `%%` | A literal percent sign | `wz_log_infof(facts, "progress=50%%");` |

The value type must match the format code. For example, use `%u` for
`uint32_t`, `%d` for `int32_t`, and `%f` for `float` or `double`.

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
