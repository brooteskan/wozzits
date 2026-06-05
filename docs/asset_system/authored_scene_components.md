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

The current `scene_import_source`, `mesh_source`, `mesh_render_style`,
`scalar_field_source`, `vector_field_source`, `sky_visual.texture_path`,
`terrain_render_style`, `terrain_mesh_source`, and
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

Current compatibility materialization lives in window-engine:

```cpp
SceneAuthoringMaterializeReport materialize_scene_authoring_components(
    SceneAssetData& scene,
    EngineAssetLibrary& assets,
    const SceneAuthoringMaterializeOptions& options = {});
```

That pass owns the editor/import semantics for the compatibility source fields.
It materializes mesh, scalar-field, vector-field, light, HDRI environment, sky,
terrain, and terrain-preview resources into asset-DAG nodes, writes the derived
asset keys back onto authored scene components, and reports renderable assets
that the editor or importer should realize after committing and resolving the
asset library. UI layers should edit authored data, call this shared pass, then
instantiate the scene from the materialized asset references.

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
| Light | Asset-backed component | `DirectLightSource` stores authored component placement/participation and materializes a `DirectLightAsset`; legacy scene-level `SceneLightAsset` records remain as the scene-render bridge. |
| AmbientLighting | Asset-backed component | Stores ambient participation on a node and materializes an `AmbientLightingAsset`; field modulation can reference scalar/vector field assets. |
| HDRIEnvironment | Asset-backed component | Stores authored environment-map participation and materializes an `HDRIEnvironmentAsset`; sky surfaces may share image sources with it, but HDRI environment remains radiance/lighting data rather than the visible sky surface. |
| SkyVisual | Component / visual-content bridge | Describes what appears on a sky surface: solid color, direction debug, gradient, scalar/vector field projection, or equirectangular OpenEXR texture. Future general texture/field material assets should replace source-path compatibility fields. |
| SkySurface | Component | Describes the camera-relative sky presentation surface. It does not own lighting and does not affect terrain lighting. |

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
| Exportable/render | `Renderable`, `Camera`, `Light`, `AmbientLighting`, `HDRIEnvironment`, `SkyVisual`, `SkySurface`, `AuxiliaryVisual` |
| Runtime relevant | `InputReceiver`, `FlyingCameraController`, `ActorMovementController`, `GroundBoundary`, `Terrain`, `AudioListener`, `EventListener` |
| Editor authoring drafts | `SceneImportSource`, `MeshSource`, `MeshRenderStyle`, `ScalarFieldSource`, `VectorFieldSource`, `TerrainRenderStyle`, `TerrainMeshSource`, `TerrainHeightFieldSource` |
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

Direct lights are authored as node components and materialized into
`DirectLightAsset` nodes in the asset DAG.

Fields:

- `asset`
- `kind`
- `color`
- `intensity`
- `range`
- `inner_cone_radians`
- `outer_cone_radians`

`DirectLightAsset` owns the reusable light definition. The scene component owns
the authored relationship between that light and a node transform. During
materialization, the component's draft fields create/update the asset DAG node
and the component stores the resolved asset key.

For the current renderer bridge, materialization also projects direct light
components into legacy scene-level `SceneLightAsset` records linked by authored
node id. That bridge keeps `scene-render` consuming its existing `LightRecord`
span while the authored source moves to asset-backed components. Directional
light records take their world-space direction from the node transform's local
`-Y` axis.

### AmbientLighting

Ambient lighting is authored as a node component and materialized into an
`AmbientLightingAsset`.

Fields:

- `asset`
- `mode`
- `color`
- `intensity`
- `intensity_field`
- `color_field`
- `domain_mapping`

V1 supports constant ambient lighting and an explicit `field_modulated` mode for
the direction we discussed: terrain or world-space ambient properties can be
driven by scalar/vector fields. The asset stores those field dependencies as
normal asset references. Materialization also projects constant ambient
lighting into a scene light record with type `Ambient`; the terrain surface
renderer consumes that record as ambient RGB. Field-modulated ambient assets are
preserved in the asset DAG, but shader-side field sampling is still future work.

### HDRIEnvironment

HDRI environments are authored as node components and materialized into
`HDRIEnvironmentAsset` nodes in the asset DAG.

Fields:

- `asset`
- `path`
- `format`
- `exposure`
- `rotation_x_radians`
- `rotation_y_radians`
- `rotation_z_radians`
- `lighting_intensity`
- `reflection_intensity`
- `background_intensity`
- `lighting_sample_resolution`
- `environment_light_color`
- `environment_light_intensity`
- `dominant_light_direction`
- `dominant_light_color`
- `dominant_light_intensity`
- `dominant_light_confidence`

The component is intentionally data-only for now: it gives the scene a stable
reference to an HDRI-backed environment asset without deciding whether that HDRI
is rendered as a background. Later sky/environment behavior can consume the same
component to feed ambient lighting, reflection probes, or editor-driven
directional-light alignment.

### SkyVisual

Sky visuals are authored visual-content components for the sky surface path.
They do not own environment lighting or terrain lighting policy.

Fields:

- `kind`
- `solid_color`
- `gradient_top_color`
- `gradient_bottom_color`
- `texture_asset`
- `texture_path`
- `texture_format`
- `scalar_field_asset`
- `scalar_field_node`
- `vector_field_asset`
- `vector_field_node`
- `exposure`
- `rotation_x_radians`
- `rotation_y_radians`
- `rotation_z_radians`

Current visual kinds:

- `none`
- `solid_color`
- `direction_debug`
- `gradient`
- `equirectangular_texture`
- `scalar_field`
- `vector_field`

`SkyVisual` is deliberately broader than "skybox." It is visual content that can
be projected onto an encompassing sky canvas. `ScalarField` and `VectorField`
let authored field data be drawn on that canvas for debugging, painting, masks,
normal/flow fields, or later procedural sky effects. The current vector-field
GPU realization uploads the first vector channel as `RGBA32_FLOAT` and colors it
as signed direction/magnitude data in the sky shader.

`EquirectangularTexture` currently supports OpenEXR image paths through
`texture_path` and `texture_format`. This is a scene-editor compatibility bridge
until a real `TextureAsset` / image asset pipeline exists. `texture_asset` is
reserved in the authored shape for that future pipeline, but the working visible
sky path is the OpenEXR path today. Radiance `.hdr` decode is not implemented
for visible sky textures yet.

### SkySurface

Sky surfaces are authored presentation components for drawing a camera-relative,
unlit sky.

Fields:

- `visual_node`
- `projection`
- `radius`
- `visible_to_camera`

V0 supports `sphere` as the authored projection. The implementation renders a
special sky pass rather than treating the sky as ordinary scene geometry: it is
camera-relative, unlit, does not write depth, and draws through
`BuiltinRenderProgram::SkySurface`.

The split is:

```text
SkySurface
  where/how sky content is presented

SkyVisual
  what appears on the sky

HDRIEnvironment
  radiance/lighting/environment metadata
```

This means these are all valid authored choices:

- HDRI lights terrain, but the visible sky is a stylized gradient.
- An EXR is visible as the sky, while lighting comes from explicit scene lights.
- A scalar or vector field is projected onto the sky for debugging/authoring.
- The sky surface is invisible while environment lighting still exists.

Sky surfaces do not affect terrain lighting directly. Any future
sky-to-lighting relationship should be expressed through a scene lighting context
or environment resolver, not by making the drawable surface own lighting.

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
- `constraint_surface`
- `visible`
- `queryable`
- `constrain_movement`

`Terrain` attaches a reusable `TerrainAsset` to a scene node. The asset defines
the surface representation and policies; the component selects participation in
scene-level systems. Authored scene JSON should reference terrain assets rather
than embedding heightmap paths, mesh import settings, material graphs, or LOD
recipes directly.

`queryable` controls whether behavior API terrain sampling can query this
terrain as an explicit authored target. `constrain_movement` controls whether
runtime movement constraints may use this terrain to place constrained actors.
Those flags are intentionally separate so a terrain can constrain movement
without exposing itself to behavior queries, or vice versa.

`constraint_surface` is optional. When present, it references a `CollisionAsset`
used only by the runtime movement constraint. The terrain can render with a
detailed adaptive visual mesh while constrained actors ride on a simpler
projection surface. The proxy surface is interpreted in the terrain node's local
space and does not participate in normal collision broadphase pairs or collision
events.

For adaptive mesh terrain, the recommended proxy is a regular terrain projection
heightfield. Its X/Z resolution is an authored choice independent of render mesh
density: the terrain can keep an adaptive visual mesh, while constrained motion
uses a predictable grid. Runtime sampling is smooth for height and normal, so
terrain alignment does not pop at adaptive triangle boundaries or half-cell
normal snaps.

### Motion

Runtime-relevant authored motion state for an entity.

Fields:

- `linear_velocity`
- `angular_velocity`
- `space`
- `terrain_constrained`
- `terrain_ride_height`
- `terrain_footprint_radius`
- `terrain_align_to_surface`
- `terrain_alignment_strength`
- `enabled`

`Motion` stores velocity state that the runtime integrates once per frame.
`space` is `world` or `local`.

When `terrain_constrained` is true, the runtime terrain constraint step samples
all active `Terrain` components with `constrain_movement = true`, picks the
highest surface at the actor's current world X/Z, and writes the actor's world
Y to `surface_height + terrain_ride_height`. The step runs after behavior
commands and velocity integration, before render prep. It preserves X/Z and
does not currently modify vertical velocity, so constrained actors should drive
horizontal velocity and let the runtime constraint own ground height.

`terrain_footprint_radius` defaults to `0`, meaning point support at the actor
pivot. When positive, the runtime samples a fixed ring around the actor and uses
the highest support height, which is useful for vehicle-sized actors moving over
terrain detail smaller than the actor.

When `terrain_align_to_surface` is true, the same step also rotates the actor so
its local Y axis follows the sampled terrain normal. The actor's local Z axis is
projected onto the terrain tangent plane to preserve heading as much as possible.
`terrain_alignment_strength` is clamped to `[0, 1]`; values below `1` blend
toward the surface orientation instead of snapping fully in one frame.

### SceneImportSource

Editor/import authoring draft for expanding a source scene file into authored
scene nodes and asset-system recipes.

Fields:

- `kind`
- `path`
- `import_prefix`
- `scene_index`

`SceneImportSource` is a bridge for the editor/import workflow. It may create
child nodes with `MeshSource` records during materialization, but it does not
instantiate into `SceneInstance` runtime component tables. Runtime-ready scene
data should contain explicit component asset references produced from the
import, not the import source recipe itself.

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

The scene draft owns only the source selection and policy. During
materialization those fields are copied into the `TerrainFromMesh` asset recipe.
The terrain compiler then inspects the compiled `MeshData`: imported GLB normals
and UV0 are preserved when available, geometric triangle normals drive terrain
surface policy, and the resulting `TerrainAssetData` records which normal and UV
sources were selected for later query/render/material systems.

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

When a terrain node is visible, the editor should also register a terrain
renderable and attach the resulting `RenderableAsset` to the node.
`TerrainRenderStyle` controls that materialization choice per node:
`auto`, `surface`, `debug_wireframe`, or `none`. Mesh-backed terrain uses
`RenderableAssetModule::create_terrain_surface()` under `auto`; height-field
terrain uses `RenderableAssetModule::create_terrain_debug()` under `auto` and
adapts the height field to a bounded wireframe preview mesh in the GPU scene
resolver until a generated surface mesh path is available. Explicit `none`
leaves the terrain role/query data without attaching a renderable.

`TerrainRenderStyle` can also declare a lighting consumption policy. The style
does not own HDRI/environment work; it points at scene lighting inputs that a
materialization/render-resource resolver can translate into concrete terrain
lighting constants and, later, GPU environment resources.

- `lighting_source` (`explicit_nodes`, `scene_default`, `environment_node`, `hybrid`)
- `directional_light_node`
- `ambient_light_node`
- `environment_node`
- `ambient_strength`
- `sky_visibility_strength`
- `normal_lighting_strength`
- `terrain_bounce_strength`

`lighting_sample_resolution` controls how many pixels wide the metadata
derivation pass samples from large OpenEXR source images. The decoded OpenEXR
image is cached by file identity during the editor process, so repeated metadata
derivation can share decoded pixels. The control affects ambient and
dominant-light metadata only; visible sky texture resolution remains independent.

The node fields are authored scene-node ids. Empty values mean automatic
selection. In the current renderer bridge, materialization prioritizes selected
explicit light records in the projected scene light list so the terrain surface
renderer consumes those lights instead of whichever compatible record appears
first. `environment_node`, `scene_default`, and `hybrid` are authored policy
hooks for the future `SceneLightingContext`/`ResolvedTerrainLighting` boundary;
they round-trip today but do not introduce a new terrain shader path.

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
- `direct_light_source` references `DirectLightAsset`
- `ambient_lighting` references `AmbientLightingAsset`, which may depend on
  scalar/vector fields
- `hdri_environment` references `HDRIEnvironmentAsset`, which owns
  radiance/lighting metadata
- `sky_visual` may reference scalar/vector fields or, temporarily, an OpenEXR
  texture path for visible sky content
- `terrain_mesh_source` may either reference a `MeshAsset` directly or name a
  `source_node` with a mesh-producing scene component. The editor can then
  resolve that scene node to the produced mesh asset during rebuild while
  preserving the asset-system dependency path.
- visible terrain nodes may reference a terrain surface or terrain debug
  `RenderableAsset` derived from the node's `TerrainAsset`
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
- materialized records such as `SceneSkyDrawAsset`, generated lights, terrain
  assets, or renderables when a component participates in authoring
  materialization
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
