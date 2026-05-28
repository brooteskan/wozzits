# Authored Scene Components

> Authored scene components are the scene-language records stored on
> `SceneAssetData` / `SceneNodeAsset`. They describe entity composition for
> scenes, then instantiate into `SceneInstance` runtime component tables.

## Relationship To The Asset System

The asset system owns resource identity, dependency resolution, compilation, and
compiled resource tables.

The authored scene component model owns entity/component composition for scenes.
It can reference asset-system resources, but it is not itself a generic
asset-system capability table.

The short version:

```text
AssetSystem
  describes resources and dependencies

SceneAssetData
  describes authored scene entities and components

SceneInstance
  is the runtime projection of authored scene data
```

For example, a `Renderable` component may reference a `RenderableAssetData`
asset. The asset system owns the renderable resource. The scene component owns
the fact that a particular authored entity has a renderable.

## Source To Runtime Flow

```text
SceneAssetData
  -> SceneNodeAsset authored entity records
  -> instantiate_scene(...)
  -> SceneInstance
  -> scene-render compile path
  -> runtime preview / app systems
```

`SceneAssetData` is authored source data. `instantiate_scene(...)` is the first
compiler from authored scene data into runtime scene data.

`SceneInstance` contains:

- runtime scene graph/storage
- renderable descriptor slots
- lights
- non-render runtime component tables
- authored-to-runtime identity maps

Scene-render then compiles the runtime scene graph, renderables, and lights into
render-oriented storage.

## Identity

Authored identity:

```cpp
wz::scene::AuthoredEntityId
SceneNodeAsset::id
```

Runtime identity:

```cpp
wz::scene::RuntimeEntityId
wz::core::graph::NodeHandle
```

Runtime mappings:

```cpp
SceneInstance::authored_to_runtime
SceneInstance::runtime_to_authored
```

`scene_asset_fingerprint(...)` describes authored source identity. It should not
depend on runtime owner objects, runtime pointers, GPU handles, or editor preview
storage addresses.

## Component Categories

The component vocabulary lives in `wozzits-scene-render`:

```text
scene_render/scene/scene_ecs.h
```

The current high-level categories are:

| Category | Components |
|---|---|
| Core node | `Transform`, `Visibility`, `MotionType`, `ParentLink` |
| Exportable/render | `Renderable`, `Camera`, `Light`, `AuxiliaryVisual` |
| Runtime relevant | `InputReceiver`, `FlyingCameraController`, `ActorMovementController`, `AudioListener`, `EventListener` |
| Editor only | `EditorHandle` |

These categories are descriptive. They do not imply a generic ECS storage model,
an archetype system, or a scheduler.

## Current Authored Components

### Transform

Core node transform stored as `AuthoredTransform`.

Fields:

- `translation`
- `rotation_quat`
- `scale`

Instantiation composes this into a scene-render `TransformNode::local` matrix.

### Visibility

Core authored visibility flag stored on `SceneNodeAsset::visible`.

Visibility participates in renderable realization and preview/runtime behavior
decisions such as active camera selection.

### MotionType

Core authored motion type stored on `SceneNodeAsset::motion_type`.

This currently maps to scene-render `TransformNode::MotionType`.

### ParentLink

Authored parent relationship stored on `SceneNodeAsset::parent_id`.

Instantiation validates parent existence and builds scene graph edges.

### Renderable

Renderable component data can currently be expressed in two ways:

- legacy embedded `SceneRenderableBinding`
- preferred `renderable_asset` reference to a `RenderableAssetData` asset

The asset-backed path is the preferred shape for new authored scenes.

### Camera

Authored camera intrinsics stored as `SceneCameraAsset`.

Runtime preview currently chooses the first visible authored camera in scene
order. If that camera also has appropriate runtime behavior components, the
camera transform can be driven during preview.

### Light

Lights are currently scene-level `SceneLightAsset` records linked by authored
node id, rather than components stored directly on `SceneNodeAsset`.

This is a known transitional shape.

### InputReceiver

Marks an authored entity as participating in input.

Fields:

- `input_map`
- `log_input`

`InputReceiver` does not define behavior by itself. It answers:

```text
Can this entity receive input?
```

Behavior components answer:

```text
What does input do to this entity?
```

### FlyingCameraController

Runtime behavior component for camera-style movement and look behavior.

Current runtime preview execution is scoped to the active authored camera. It
requires:

```text
Camera
InputReceiver
FlyingCameraController
```

### ActorMovementController

First simple non-camera behavior component.

Current runtime preview execution requires:

```text
InputReceiver
ActorMovementController
```

It updates the runtime node transform with simple WASD movement and optional
boost. It does not perform physics, collision, pathfinding, animation, or
authored save-back.

Fields:

- `move_speed`
- `boost_multiplier`
- `movement_space`

### AudioListener

Runtime-relevant authored audio listener marker.

Current behavior is data-only.

### EventListener

Runtime-relevant authored event listener marker.

Current behavior is data-only.

### AuxiliaryVisual

Exportable authored visual helper. The legacy JSON field and some compatibility
aliases still use `debug_visual`.

This is not intended to mean "debugger-only visual style." Prefer
`AuxiliaryVisual` for new terminology.

### EditorHandle

Editor-only component used by scene editor tooling.

It should not be treated as runtime gameplay/app data.

## Runtime Preview Rules

The scene editor runtime preview consumes a snapshot of authored scene data:

```text
authored SceneAssetData
  -> snapshot copy
  -> SceneRuntimePreviewApp
  -> SceneInstance
```

Runtime preview systems update only runtime state:

```text
SceneInstance::storage
SceneInstance component tables
preview metrics / render frame data
```

They must not write runtime movement or controller state back into authored
`SceneAssetData`.

Stopping and restarting preview should rebuild from the authored snapshot source
and reset runtime-only transform changes.

## Asset References From Components

Scene components may reference assets, but the reference does not make the
component itself an asset-system node.

Examples:

- `renderable_asset` references `RenderableAssetData`
- future material, input-map, animation, or script components may reference
  asset-system resources

The asset-system resource remains owned by the asset system. The component owns
the authored relationship between an entity and that resource.

## Current Inventory Helpers

Authored helpers:

```cpp
authored_components_for_node(...)
summarize_authored_scene_components(...)
has_authored_renderable_component(...)
has_authored_camera_component(...)
has_runtime_relevant_components(...)
```

Runtime helpers:

```cpp
summarize_scene_instance_components(...)
```

These helpers inventory the shape that exists today. They are not a query API,
archetype system, or generic ECS framework.

## Tests To Maintain

Component additions should normally update or add tests for:

- JSON parse/export
- authored component inventory
- runtime component summary
- instantiation into `SceneInstance`
- fingerprint changes when authored component data changes
- runtime preview behavior, if the component has runtime behavior

Good current test areas:

```text
tests/asset_scene/scene_asset_module.cpp
tests/scene_compile/scene_ecs_boundary_test_1.cpp
```

## Non-Goals For Now

- No generic ECS storage.
- No archetypes.
- No signatures.
- No query API.
- No scheduler/system graph.
- No input-map asset system yet.
- No physics or collision system.
- No save-back from runtime preview into authored scene data.
- No `AppAsset` or serialized app definition.

## Direction

The component model should evolve as a small authored scene language:

```text
authored entity/component source
  -> runtime scene projection
  -> scene-render/app/editor consumers
```

That makes it parallel to the asset system, and sometimes a consumer of asset
system resources, without collapsing scene composition into generic asset graph
plumbing.
