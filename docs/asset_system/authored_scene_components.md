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

## Asset Reference Rules

Authored scene components may reference asset-system resources, but they should
not duplicate asset definitions or compiler inputs.

Use an asset reference when component data needs to point at reusable or
compiled resource data:

- renderable, material, mesh, texture, animation, input-map, script, or audio
  resources that need identity, dependencies, validation, compilation, sharing,
  caching, or hot reload

Keep data in the component when it describes the authored entity relationship or
runtime role:

- transform, parent, visibility, active camera role, listener marker,
  controller tuning, editor-handle settings, and similar per-entity state

Rules of thumb:

- Components may hold `AssetKey` references or future symbolic asset references.
- Components should not own GPU handles, renderer table handles, or resource
  compiler descriptors.
- Components should not copy source paths, compiler settings, shader choices,
  material definitions, or other data that belongs to an asset recipe.
- If a component field becomes reusable across entities, needs validation, has
  dependencies, or should participate in asset identity, promote it to an asset
  and let the component reference it.
- Legacy compatibility fields may violate these rules, but new component work
  should not expand those paths.

## Scene Editor Asset Authoring Layer

The current `mesh_source`, `mesh_render_style`, `scalar_field_source`,
`vector_field_source`, `terrain_mesh_source`, and
`terrain_height_field_source` fields are compatibility fields for the scene
editor's first asset-authoring workflow. They let an editor document reopen and
rebuild asset-system nodes, but they should not be treated as the long-term home
for reusable resource recipes.

The intended long-term split is:

| Layer | Owns | Does not own |
|---|---|---|
| `SceneAssetData` | authored entities, hierarchy, transforms, component composition, per-entity asset references | source paths, compiler settings, reusable resource recipes, materialization policy |
| asset-authoring documents | mesh/scalar/vector/terrain/renderable recipes, source files, generator settings, compiler options, dependency identity | entity hierarchy, runtime component tables |
| scene-editor document layer | bindings between scene entities/components and authored asset recipes, editor selections, rebuild state | runtime app state, compiled resource tables |
| `SceneInstance` | runtime projection of scene components | editor/import recipes, asset compiler inputs |

A new editor workflow should create asset-authoring recipe records outside
`SceneAssetData`, materialize those records into normal asset-DAG nodes, then
write only explicit component references such as `renderable_asset`,
`terrain.asset`, or future material/input/script asset references into the
runtime-ready scene data.

The compatibility source fields may continue to parse, export, fingerprint, and
materialize so existing editor scenes can reopen. New source-like fields should
not be added to `SceneNodeAsset` unless they are deliberately compatibility
bridges with a migration path. Prefer one of these shapes instead:

- an authored asset document that owns the reusable recipe and dependencies
- a scene-editor document section that binds an entity/component slot to an
  authored asset recipe
- a scene component that stores only the resolved asset reference and
  per-entity participation flags

Migration should be additive:

1. Keep existing source fields readable and exportable.
2. Add external recipe documents and symbolic editor bindings for new workflows.
3. Materialize editor bindings into asset-DAG nodes before runtime preview.
4. Save runtime-ready scene assets with explicit asset references and no
   dependency on editor/import recipe fields.
5. Eventually treat source fields as import compatibility rather than primary
   authoring state.

## Asset / Component Authoring Checklist

Classify every new authored scene concept before adding fields or schemas.

Ask whether it is an asset:

```text
Reusable across scenes or entities?
Needs stable identity, dependencies, validation, compilation, caching, or hot reload?
Represents source data, a recipe, or a built resource?
```

If yes, make or reuse an asset-system resource and let scene components refer to
it.

Ask whether it is a component:

```text
Describes what this authored entity has, does, or means in the scene?
Per-entity relationship, role, marker, placement, or behavior tuning?
Needs to instantiate into SceneInstance component/runtime tables?
```

If yes, keep it in the authored scene component model.

Ask whether it is runtime-only:

```text
Derived from authored data during preview/runtime?
Changes frame to frame?
Represents live state such as contacts, trigger occupancy, playback cursors,
movement integration, or diagnostics?
```

If yes, store it in runtime systems/tables, not in `SceneAssetData`.

Short rule:

```text
Components describe what an entity has or does.
Assets describe reusable resources and recipes.
Runtime systems own derived/live state.
```

## Issue 71 Candidate Classification

This table is a starting point for the basic scene asset/component library work.
It should be updated as decisions become concrete.

| Concept | Current classification | Notes |
|---|---|---|
| Ground / boundary | Component now; maybe asset later | Start as authored space/boundary descriptor. Promote reusable landscapes or terrain recipes to assets later. |
| BoxCollider | Component descriptor + runtime collider record | Do not make collision contacts or broadphase state assets. |
| TriggerVolume | Component descriptor + runtime event state | Authored shape/role lives on the entity; enter/exit occupancy is runtime-only. |
| Pushable / MovableBody | Component descriptor + runtime motion state | Keep live velocity, push resolution, and contacts out of authored data. |
| AutonomousMover | Component descriptor | If paths become reusable named data, promote path data to an asset later. |
| AudioSource | Component referencing future `AudioClipAsset` | Component owns placement, looping/volume/play policy; audio clip data should be an asset. |
| AudioListener | Component | Existing marker-style component; live listener/mixing state belongs to runtime audio. |
| InputMap | Unresolved: label now or future asset | Keep as string only if it is a routing label. Promote to asset if it owns reusable bindings. |
| RenderStyle / Material | Asset | Should be referenced by renderable/material components, not copied into scene nodes. |
| Light | Component or documented scene-level bridge | Current code stores scene-level light records linked by node id; choose whether to migrate to node component. |

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
| Runtime relevant | `InputReceiver`, `FlyingCameraController`, `ActorMovementController`, `GroundBoundary`, `Terrain`, `AudioListener`, `EventListener` |
| Editor authoring drafts | `MeshSource`, `MeshRenderStyle`, `ScalarFieldSource`, `VectorFieldSource`, `TerrainMeshSource`, `TerrainHeightFieldSource` |
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
`SceneRenderableBinding` exists for legacy/debug compatibility only. It embeds
scene-render details such as handles, node classification, and bounds directly
in the scene component, so it should not receive new features. New authoring
paths should create or reference a `RenderableAssetData` asset and store that
relationship through `renderable_asset`.

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

`ActorMovementController` should consume constraint data from other components,
such as `GroundBoundary`, rather than embedding terrain or landscape data in the
controller itself.

### GroundBoundary

Runtime-relevant authored constraint component for surfaces or terrain-like
entities that can bound actor motion.

Fields:

- `min`
- `max`
- `constrain_vertical`
- `enabled`

`GroundBoundary` describes the local-space traversable bounds on the entity that
owns the component. It is intentionally a simple descriptor: it does not own
terrain source data, height fields, collision meshes, broadphase state, or a
motion solver. Future reusable landscapes should be assets; this component can
then reference or annotate those assets while runtime movement systems consume
the projected boundary table.

### Terrain

Runtime-relevant authored placement component for semantic terrain assets.

Fields:

- `asset`
- `visible`
- `queryable`
- `constrain_movement`

`Terrain` attaches a reusable `TerrainAsset` to a scene node. The asset defines
the surface representation and policies; the component selects participation in
scene-level systems. Authored scene JSON should reference terrain assets rather
than embedding heightmap paths, mesh import settings, material graphs, or LOD
recipes directly.

### MeshSource / MeshRenderStyle

Editor/import authoring drafts for building asset-system nodes from scene-editor
controls. They may exist in saved editor scene JSON, but they are not runtime app
components. Before preview/runtime instantiation, the editor must materialize
them into asset-DAG nodes and attach the resulting `renderable_asset`.

The asset DAG owns source paths, mesh indices, render program choices, and render
policy flags. The scene keeps these records only so the editor can re-open and
rebuild the asset graph.

### ScalarFieldSource

Editor/import authoring draft for building a scalar-field asset.

Fields:

- `kind`
- `asset`
- `path`
- `width`
- `height`
- `depth`
- `frequency`
- `amplitude`

`ScalarFieldSource` mirrors `MeshSource`: it lets the editor keep enough source
recipe data to rebuild a scalar-field asset DAG node. It may describe a raw F32
file or a procedural field. Before another editor recipe consumes it, the editor
materializes it into a `ScalarFieldAsset` and stores that key on `asset`.
`ScalarFieldSource` does not instantiate into runtime scene component tables.

### VectorFieldSource

Editor/import authoring draft for building a vector-field asset.

Fields:

- `kind`
- `asset`
- `path`
- `width`
- `height`
- `depth`
- `components_per_channel`
- `channels`

`VectorFieldSource` is the vector counterpart to `ScalarFieldSource`. It lets
the editor keep enough source recipe data to rebuild a vector-field asset DAG
node for imported normal maps, flow fields, or other sampled vector data. V1
supports raw interleaved F32 data with named channels and 2, 3, or 4 components
per channel.

Before another editor recipe consumes it, the editor materializes this draft
into a `VectorFieldAsset` and stores that key on `asset`. `VectorFieldSource`
does not instantiate into runtime scene component tables.

### TerrainMeshSource

Editor/import authoring draft for building a terrain asset from one mesh asset.

Fields:

- `asset`
- `mode`
- `source_node`
- `height_policy`
- `min_surface_normal_y`
- `include_backfaces`

`TerrainMeshSource` is an explicit editor authoring recipe attached to a terrain
node. It can reference a direct mesh asset or a `source_node` selected in the
editor. Scene-node selection is intentionally scoped to mesh-source nodes
parented directly under the terrain node. This keeps source meshes authored in
terrain space and makes the terrain node transform the shared placement/scale
for both source data and derived terrain. The default height policy is
`highest_accepted_surface`: when several mesh hits exist at the same `(x, z)`,
the terrain build should choose the highest hit whose normal passes
`min_surface_normal_y`. This keeps rock-like or closed meshes from silently
becoming arbitrary multi-layer terrain while still giving the editor a simple
first mesh-to-terrain workflow.

Before preview/runtime instantiation, the editor must resolve this draft into a
`TerrainAsset` and store that key on the `Terrain` component. `TerrainMeshSource`
does not instantiate into `SceneInstance` runtime component tables.

### TerrainHeightFieldSource

Editor/import authoring draft for building a terrain asset from one scalar
field asset.

Fields:

- `asset`
- `mode`
- `source_node`
- `origin`
- `size`
- `vertical_scale`
- `base_height`

`TerrainHeightFieldSource` is the height-field counterpart to
`TerrainMeshSource`. It can reference a direct scalar field asset or a
`source_node` selected in the editor. Scene-node selection is intentionally
scoped to scalar-field-source nodes parented directly under the terrain node.

Before preview/runtime instantiation, the editor must resolve this draft into a
`TerrainAsset` through `TerrainAssetModule::create_from_height_field()` and
store that key on the `Terrain` component. `TerrainHeightFieldSource` does not
instantiate into `SceneInstance` runtime component tables.

When a terrain node is visible, the editor should also register a terrain debug
renderable through `RenderableAssetModule::create_terrain_debug()` and attach the
resulting `RenderableAsset` to the node. That renderable remains part of the
asset DAG: it depends on the compiled `TerrainAsset`, adapts mesh terrain to the
mesh debug path, and adapts height-field terrain to a bounded wireframe preview
mesh in the GPU scene resolver until a terrain-specific renderer is available.

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
- `terrain` references `TerrainAssetData`
- `terrain_mesh_source` may either reference a `MeshAsset` directly or name a
  `source_node` with a mesh-producing scene component. The editor can then
  resolve that scene node to the produced mesh asset during rebuild while
  preserving the asset-system dependency path.
- visible terrain nodes may reference a terrain debug `RenderableAsset` derived
  from the node's `TerrainAsset`
- future material, input-map, animation, or script components may reference
  asset-system resources

The asset-system resource remains owned by the asset system. The component owns
the authored relationship between an entity and that resource.

Scene asset registration must also encode those references as asset-DAG
dependencies. A runtime-ready scene asset should be resolvable from the DAG
alone: resolving the scene must compile the referenced renderable, terrain, mesh,
and other resource assets without requiring editor-only component state.

The current raw `AssetKey` spelling in scene JSON is an implementation detail of
the first asset-backed renderable path. Authored JSON may now use symbolic
renderable references that resolve to `AssetKey` values during scene asset
compilation/import. Runtime scene data still stores the resolved key, not the
symbolic name. Export currently writes the concrete `asset-key:` form because
`SceneAssetData` does not preserve the original symbolic reference.

## Current Inventory Helpers

Authored helpers:

```cpp
authored_components_for_node(...)
summarize_authored_scene_components(...)
has_authored_renderable_component(...)
has_authored_camera_component(...)
has_runtime_relevant_components(...)
```

Asset-authoring recipe helpers:

```cpp
has_asset_authoring_recipes(...)
summarize_scene_asset_authoring_recipes(...)
```

These helpers intentionally track the editor/import recipe records separately
from normal scene composition. They are a compatibility inventory for the
current shadow-DAG fields and a migration point for moving reusable asset
recipes out of `SceneAssetData` later.

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
tests/asset_scene/scene_authoring_materialize.cpp
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
