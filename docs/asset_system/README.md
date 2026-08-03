# Asset System — Capability Index

This document lists every implemented asset capability in wozzits-window-engine.
Each capability is the full chain from a registered schema through CPU compilation
to an optional GPU upload path.

See [`capability_template.md`](capability_template.md) for the per-capability
documentation format. Capabilities marked **documented** have a dedicated file
in `docs/asset_system/capabilities/`.

---

## How to read this table

| Column | Meaning |
|--------|---------|
| Capability | Human name for the end product |
| Schema constant | The `kXxxSchema` ID registered with `AssetSystem` |
| Schema value | Low 24 bits of the 64-bit schema ID (hex) |
| Output AssetType | `kAssetTypeXxx` constant + numeric value |
| Module / creation API | Class and method that registers the asset |
| CPU storage | Where compiled data lives at runtime |

The schema value column omits the `0xF11ECA55E7` magic prefix shared by all
engine schemas.

---

## Authored Scene Components

Scene component descriptors live in `SceneAssetData` rather than as standalone
asset-system capabilities. They form the authored ECS-style scene language that
instantiates into `SceneInstance` runtime component tables.

See [`authored_scene_components.md`](authored_scene_components.md).

The transactional rebuild boundary for committed scene runtime state is tracked
in [`scene_runtime_bundle_contract.md`](scene_runtime_bundle_contract.md).

Draft key parity between the editor authoring graph and engine registration
paths is tracked in
[`asset_graph_draft_key_parity.md`](asset_graph_draft_key_parity.md).

---

## File Carriers and Source Nodes

These are leaf nodes in the asset graph. They carry raw bytes from the filesystem
into the system; other assets declare them as dependencies.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Raw file | `kRawFileSchema` | `0x000001` | `kAssetTypeRawFile` (64) | `FileCarrierAssetModule` |
| HLSL source file | `kHLSLFileSchema` | `0x000002` | `AssetType::ShaderSource` | Internal to `ShaderAssetModule` |
| Text file | `kTextFileSchema` | `0x000003` | `kAssetTypeTextFile` (65) | `FileCarrierAssetModule` |
| Binary blob file | `kBinaryBlobSchema` | `0x000004` | `kAssetTypeBinaryBlob` (66) | `FileCarrierAssetModule` |
| Imported source file | `kImportedSourceFileSchema` | `0x000008` | `kAssetTypeImportedSourceFile` (70) | `FileCarrierAssetModule` |
| Custom binary file | `kCustomBinaryFileSchema` | `0x00000F` | `kAssetTypeBinaryBlob` (66) | `FileCarrierAssetModule` |
| CSV file | `kCSVFileSchema` | `0x00000E` | `kAssetTypeRawFile` (64) | `FileCarrierAssetModule` |

---

## Shaders, Render Programs, And Compute Pipelines

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| HLSL shader pair | `kHLSLShaderSchema` | `0x000100` | `AssetType::Shader` | `ShaderAssetModule::create_shader_pair()` |
| Builtin render program | `kBuiltinRenderProgramSchema` | `0x000101` | `kAssetTypeRenderProgram` (1049) | `RenderProgramAssetModule::create_builtin()` |
| Compute pipeline declaration | `kComputePipelineSchema` | `0x000102` | `kAssetTypeComputePipeline` (1050) | `ComputePipelineAssetModule::create_compute_pipeline()` |
| Custom render program | `kCustomRenderProgramSchema` | `0x000103` | `kAssetTypeRenderProgram` (1049) | `RenderProgramAssetModule::create_custom()` |

`ShaderPairAsset` wraps two `AssetKey` values (vertex + pixel). Resolved to
`ShaderPairHandles` (GPU handles) via `ShaderAssetModule::get_shader_pair()`.

`kAssetTypeRenderProgram` is a CPU-side declaration: binding model, topology,
render domain/policy, and shader pair reference. It does not directly own a
backend PSO; that realization step is deferred.

`kAssetTypeComputePipeline` is also CPU-side metadata: one compiled compute
shader dependency, structured-buffer binding layout, root-constant count, and
thread-group size. The DX12 backend realizes this into a compute PSO when a
behavior GPU job or mesh compute field compile builds a kernel library.

Dedicated docs:

- [`capabilities/render_program.md`](capabilities/render_program.md)
- [`capabilities/compute_pipeline.md`](capabilities/compute_pipeline.md)

---

## Scalar Fields

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| File-backed scalar field | `kScalarFieldFromRawF32Schema` | `0x000200` | `kAssetTypeScalarField` (128) | `ScalarFieldAssetModule::create_scalar_field()` |
| Procedural scalar field | `kScalarFieldProceduralSchema` | `0x000201` | `kAssetTypeScalarField` (128) | `ScalarFieldAssetModule::create_procedural_scalar_field()` |

Both schemas produce the same `kAssetTypeScalarField` output stored in
`ScalarFieldTable`. The file-backed variant depends on a `kRawFileSchema` carrier.

---

## Vector Fields

Vector fields are sampled fields with named channels and a fixed component count
per channel. They are the first step toward a shared `Field<T, C>` model where
scalar fields are the `C = 1` specialization and vector fields cover `C = 2..4`.

V1 stores raw interleaved `f32` values in sample-major, channel, component order:

```text
sample_count * channel_count * components_per_channel
```

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| File-backed vector field | `kVectorFieldFromRawF32Schema` | `0x000202` | `kAssetTypeVectorField` (2258) | `VectorFieldAssetModule::create_vector_field()` |

The file-backed variant depends on a `kRawFileSchema` carrier and produces
`VectorFieldData` stored in `VectorFieldTable`. Current validation keeps
`components_per_channel` to 2, 3, or 4; the existing scalar-field asset remains
the active `1 x 1` path until the common `Field` representation lands.

Common intended uses include normal fields (`components_per_channel = 3`), flow
fields (`components_per_channel = 2`), and multi-layer vector data with named
channels over one spatial domain.

Scenes may carry an editor-only `VectorFieldSource` authoring recipe. The
materialization pass turns it into a `VectorFieldAsset` and stores the asset key
back on the recipe; runtime scene instantiation does not create a component
table for vector-field sources.

Scalar and vector fields can also be projected onto sky visuals. The GPU upload
path currently supports `ScalarFieldData` as an `R32_FLOAT` texture and
`VectorFieldData` as an `RGBA32_FLOAT` texture containing the first vector
channel. This is render-resource realization, not a separate texture asset
capability.

---

## Meshes

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Procedural triangle mesh | `kProceduralTriangleMeshSchema` | `0x000400` | `kAssetTypeMesh` (130) | `MeshAssetModule::create_procedural_mesh()` with `ProceduralMeshKind::Triangle` |
| Procedural quad mesh | `kProceduralQuadMeshSchema` | `0x000401` | `kAssetTypeMesh` (130) | `MeshAssetModule::create_procedural_mesh()` with `ProceduralMeshKind::Quad` |
| Procedural cube mesh | `kProceduralCubeMeshSchema` | `0x000402` | `kAssetTypeMesh` (130) | `MeshAssetModule::create_procedural_mesh()` with `ProceduralMeshKind::Cube` |
| GLB static mesh import | `kGLBMeshSchema` | `0x000403` | `kAssetTypeMesh` (130) | `MeshAssetModule::create_glb_mesh()` |
| Placeholder mesh | `kPlaceholderMeshSchema` | `0x000404` | `kAssetTypeMesh` (130) | `MeshAssetModule::create_placeholder_mesh()` |
| Mesh decimation | `kMeshDecimationSchema` | `0x000405` | `kAssetTypeMesh` (130) | `MeshAssetModule::create_decimated_mesh()` |

All mesh schemas produce `kAssetTypeMesh` stored in `MeshTable`.
`MeshAssetModule::get_mesh()` → `MeshHandle`; `get_mesh_data()` → `const MeshData*`.

The GLB importer depends on a `kRawFileSchema` file carrier. `GLBMeshDesc::mesh_index`
selects which mesh primitive inside the GLB to compile (default 0).
When present, GLB `NORMAL` and `TEXCOORD_0` attributes are preserved on
`MeshData` and exposed through `mesh.has_normals` / `mesh.has_uv0` so downstream
asset compilers can choose terrain/material sources explicitly.

`kMeshDecimationSchema` compiles one source mesh into another `kAssetTypeMesh`
using the mesh processing abstraction. The descriptor can target vertex count,
triangle count, or ratio and carries quality constraints such as boundary
preservation, aspect ratio, edge length, valence, normal deviation, and
Hausdorff error.

---

## Mesh Derived Fields And Sparse Operators

`MeshDerivedFieldData` stores typed channel values over a source mesh domain.
`MeshSparseOperatorData` stores CSR relationships between elements of a source
mesh domain. Together they form the current mesh signal-analysis path:

```text
MeshDerivedField Float1 channel
  + MeshSparseOperator NeighborWeights CSR
  -> behavior GPU sparse residual apply
  -> output MeshDerivedField channel
```

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Explicit mesh derived field | `kMeshDerivedFieldExplicitSchema` | `0x000406` | `kAssetTypeMeshDerivedField` (154) | `MeshDerivedFieldAssetModule::create_explicit_field()` |
| Mesh wavelet analysis field | `kMeshWaveletAnalysisSchema` | `0x000407` | `kAssetTypeMeshDerivedField` (154) | `MeshDerivedFieldAssetModule::create_wavelet_analysis()` |
| Behavior field placeholder | `kBehaviorFieldPlaceholderSchema` | `0x000408` | `kAssetTypeMeshDerivedField` (154) | `MeshDerivedFieldAssetModule::create_behavior_field_placeholder()` |
| Mesh compute derived field | `kMeshComputeDerivedFieldSchema` | `0x000409` | `kAssetTypeMeshDerivedField` (154) | `MeshDerivedFieldAssetModule::create_compute_derived_field()` |
| Builtin mesh derived field | `kBuiltinMeshDerivedFieldSchema` | `0x00040B` | `kAssetTypeMeshDerivedField` (154) | `MeshDerivedFieldAssetModule::create_builtin_field()` |
| Mesh sparse operator | `kMeshSparseOperatorSchema` | `0x00040A` | `kAssetTypeMeshSparseOperator` (157) | `MeshSparseOperatorAssetModule::create_sparse_operator()` |

Mesh-derived fields support `Vertex`, `Edge`, `Face`, and `Corner` domain names
in the data model, but the current behavior sparse-apply bridge is v0
vertex-domain and scalar-first. `MeshDerivedFieldValueType` currently includes
`Float1`, `Float2`, `Float3`, `Float4`, and `UInt1`; visualization and behavior
signal input are Float1-only today.

Scene-authored `MeshDerivedFieldSource` is the v0 editor bridge for the first
mesh-to-field visualization projects. It registers builtin Float1 vertex fields
from constant values, position gradients, vertex-index gradients, or
triangle-corner counts, then stores the materialized key on `resolved_field_asset`.
Mesh render styles can bind those fields explicitly with `field_ref`,
`channel_id`, `value_min`, `value_max`, and `gamma`.

The implemented sparse operator kind is `UniformVertexLaplacian` with
`NeighborWeights` convention. Rows store neighbor weights only:

```text
smooth[i] = sum_j w_ij f[j]
Lf[i]     = f[i] - smooth[i]
```

Rows with no neighbors are treated as zero detail by consumers. `vertex_mass`
exists as a companion array and is all 1 for the uniform v0 operator.

GPU residency tables now cover:

- source mesh positions/indices (`GpuResidentMeshDataTable`)
- Float1 mesh field visualization channels (`GpuResidentFieldTable`)
- CSR sparse operator buffers (`GpuResidentSparseOperatorTable`)

The behavior ABI exposes this path through ABI 24:

- `WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR`
- `WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR_INFO`
- `WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL`
- `WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION`
- helper `wz_gpu_set_structured_input_mesh_field_signal(...)`

The current limitation is intentional and tracked as follow-up work: behavior
field input and sparse-operator resolution still use the entity's
`mesh_field_visualization_targets` bridge to discover the source mesh/field.
That is fine for the editor visualization loop, but authored field chaining
should eventually bind explicit input field assets independent of what is
currently visualized.

Dedicated docs:

- [`capabilities/mesh_derived_field.md`](capabilities/mesh_derived_field.md)
- [`capabilities/mesh_sparse_operator.md`](capabilities/mesh_sparse_operator.md)

---

## Terrain

Terrain assets are semantic surface data products. They describe a reusable
surface domain and the source representation needed by render/query/collision
systems. A scene terrain component places a terrain asset in a scene; it does
not embed heightmap files, mesh import settings, or compiler recipes.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Terrain from height field | `kTerrainFromHeightFieldSchema` | `0x000A00` | `kAssetTypeTerrain` (149) | `TerrainAssetModule::create_from_height_field()` |
| Terrain from mesh | `kTerrainFromMeshSchema` | `0x000A01` | `kAssetTypeTerrain` (149) | `TerrainAssetModule::create_from_mesh()` |
| Terrain visual proxy | `kTerrainVisualProxySchema` | `0x000A02` | `kAssetTypeTerrainVisualProxy` (153) | `TerrainVisualProxyAssetModule::create_from_terrain()` |

The first two schemas produce `TerrainAssetData` stored in `TerrainAssetTable`.
The height-field variant depends on a compiled `kAssetTypeScalarField`; the mesh
variant depends on a compiled `kAssetTypeMesh`. V0 records domain bounds,
resolution/source metadata, basic render/collision policy, and query capability
flags. Height-field terrain supports height-query semantics; mesh terrain records
surface bounds/source metadata and leaves acceleration-backed ray/project queries
for a later compiler/runtime layer.

`TerrainVisualProxyData` is the separate compiled render representation for
terrain. It is CPU-side metadata for chunk records, per-chunk LOD records,
world-space geometric error, representation kind (`MeshChunks`, `GridTiles`, or
`SurfelCloud`), bounds, seam/boundary metadata, and per-LOD aggregates. It also
stores cache/version fields (`schema_version`, `compiler_version`,
`source_asset_key`, and `simplification_settings_hash`) plus stable
`TerrainProxyId`, `TerrainChunkId`, `TerrainLodId`, and
`TerrainRepresentationId` values used by downstream render IR and selector work.
GPU buffers are intentionally not owned by the proxy data model; the LOD selector
can read bounds, errors, triangle counts, representation IDs, and aggregate data
directly on the CPU.

`SceneTerrainAsset` keeps the semantic terrain asset reference and may also carry
an optional `visual_proxy_asset` key once a compiled proxy asset exists. The
lightweight `TerrainRenderable` value names the same pair for scene-facing
terrain renderables. The render proxy remains distinct from source/query terrain
data even when both are produced from the same authored terrain.

Longer-term terrain and mesh LOD research is tracked separately in
[`nanite_cluster_hierarchy.md`](nanite_cluster_hierarchy.md). That document
describes a terrain-independent meshlet/cluster hierarchy, why it should be a
future sibling asset rather than part of semantic terrain data, and the
validation prerequisites before GPU-driven selection is attempted.

Mesh terrain also records source-attribute availability and selected terrain
sources. If source mesh normals are present, `TerrainFromMesh` defaults to
`TerrainNormalSource::MeshVertexNormal`; otherwise it falls back to
`DerivedGeometry`. If UV0 is present, it defaults to `TerrainUVSource::MeshUV0`;
otherwise it records no UV source unless the caller explicitly requests a
generated source such as `PlanarXZ`. The mesh terrain compiler also records the
source triangle count and the number of triangles accepted by
`min_surface_normal_y` / `include_backfaces`, using geometric triangle normals
for terrain policy rather than smoothed/imported vertex normals.

A terrain node no longer registers a renderable of its own. The 0x703 debug
and 0x704 surface recipes were deleted along with the nine MeshIA builtin
programs they drew through; terrain renders as a 0x70A `CameraSnappedTerrain`
custom renderable (#234). Terrain, visual-proxy and constraint-collision
assets are still materialized -- they feed collision and the
authoring/inspector paths -- but `SceneNodeAsset::renderable_asset` is left
empty for a terrain node.

---

## Collision Assets

Collision assets are CPU-side query/occupancy data products derived from source
assets. They describe how something occupies space independently of its visual
representation, so a mesh or terrain can produce bounds, triangle, terrain
height, or future proxy collision without making the source asset itself
implicitly collide.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Collision from mesh | `kCollisionFromMeshSchema` | `0x000B00` | `kAssetTypeCollisionAsset` (150) | `CollisionAssetModule::create_from_mesh()` |
| Collision from terrain | `kCollisionFromTerrainSchema` | `0x000B01` | `kAssetTypeCollisionAsset` (150) | `CollisionAssetModule::create_from_terrain()` |

Both schemas produce `CollisionAssetData` stored in `CollisionAssetTable`.
The recipe carries occupancy semantics (`Solid`, `Surface`, `WalkableSurface`,
or `Sensor`) and behavior flags such as `blocks_movement` and `queryable`.
Mesh-derived collision can currently emit bounds or a triangle mesh copy.
Terrain-derived collision emits a heightfield shape for height-field terrain and
a terrain mesh surface descriptor for mesh terrain.

---

## Gaussian Splat Clouds

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| PLY file import | `kGaussianSplatFromPLYSchema` | `0x000500` | `kAssetTypeGaussianSplatCloud` (131) | `GaussianSplatAssetModule::create_from_ply()` |
| Scalar-field-derived cloud | `kGaussianSplatFromFieldSchema` | `0x000501` | `kAssetTypeGaussianSplatCloud` (131) | `GaussianSplatAssetModule::create_from_scalar_field()` |
| Procedural/debug cloud | `kProceduralGaussianSplatCloudSchema` | `0x000505` | `kAssetTypeGaussianSplatCloud` (131) | `GaussianSplatAssetModule::create_procedural_cloud()` |
| Terrain surface from height field | `kGaussianSplatTerrainSurfaceFromHeightFieldSchema` | `0x000503` | `kAssetTypeGaussianSplatCloud` (131) | `GaussianSplatAssetModule::create_terrain_surface_from_height_field()` |
| Gaea R32 + JSON sidecar recipe | `kTerrainSplatFromGaeaR32Schema` | `0x000504` | `kAssetTypeGaussianSplatCloud` (131) | `GaussianSplatAssetModule::create_terrain_splat_from_gaea_r32()` |

All five produce `GaussianSplatCloudData` in `GaussianSplatCloudTable`.
The PLY importer depends on a `kRawFileSchema` carrier.
The scalar-field variant depends on a compiled `kAssetTypeScalarField`.
The terrain-surface variant depends on a compiled `kAssetTypeScalarField` and
produces anisotropic, surface-tangent-aligned splats treating heightmap values as
raw world elevations (distinct from the simple scalar-field debug heightmap splatter).
The Gaea R32 recipe depends on a `kRawFileSchema` carrier for the .r32 bytes and
a compiled `kAssetTypeJSONDocument` sidecar; it builds a transient
`ScalarFieldData` internally and does not expose an intermediate scalar field
asset.

`GaussianSplatCloudData::splats` is the ordered list that defines upload index order.
The `cloud_local_index` field on `SplatDescriptor` / `SplatPrimitive` in
wozzits-scene-render must match position in this vector (see scene-render docs).

---

## Gaussian Splat Color LOD

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Color LOD from cloud | `kGaussianSplatColorLODSchema` | `0x000502` | `kAssetTypeGaussianSplatColorLOD` (4114) | `GaussianSplatColorLODAssetModule::create_from_cloud()` |

A derived, parallel-array companion to a `GaussianSplatCloudData`. The compiler
runs a uniform-grid neighbor search over the source cloud and produces per-splat
prefiltered neighborhood color (linear RGB) and confidence (0 = high local
variance, keep detail; 1 = low variance, safe to blend toward neighborhood color).

`GaussianSplatColorLODData` is stored in `GaussianSplatColorLODTable`.
Index `i` of the LOD arrays corresponds to index `i` of the source cloud's splats
vector. The compile descriptor (`GaussianSplatColorLODCompileDesc`) controls
neighbor radius, Gaussian sigma, self-inclusion, opacity weighting, and the
variance-to-confidence gain. All compile parameters are part of the asset key
identity.

The LOD data is a CPU-side derived asset; GPU packing happens later when the
renderable upload path reads the optional `companion_asset` on `RenderableAssetData`
and packs LOD color + confidence into the GPU vertex layout.

---

## Mesh Render Styles

Mesh render styles are CPU-side authored draw-style assets for mesh-backed
renderables. They are intentionally separate from mesh geometry and terrain
source data so one style can be shared by ordinary meshes and mesh-backed terrain
renderables.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Mesh render style | `kMeshRenderStyleSchema` | `0x000600` | `kAssetTypeMeshRenderStyle` (1071) | `MeshRenderStyleAssetModule::create_mesh_render_style()` |

V1 styles carry two authored layers: `wireframe` and `surface`. Each layer has an
enabled flag, RGBA color, and emissive strength. Shared flags carry depth
test/write, double-sided intent, and a hidden-line prepass hint for wireframe
rendering. Since issue #195, a style is consumed as an optional dependency of the
RHI pull-mesh renderable (`0x000706`): the style's shading constants bake into the
`RhiRenderableRecipe` and flow to shaders as the 28-dword `mesh_style` root-constant
block, while the style's pipeline-state intent (wireframe/solid raster, depth,
blend) derives the render program via
`ensure_mesh_style_pull_program()` (engine/assets/mesh_style_pull_program.h) —
styles sharing pipeline state dedup to one program asset. Scene mesh authoring
materialization creates the style asset from the node component before creating
the renderable (device required; deviceless materialize attaches no renderable).

---

## Render Binding Layouts

Render binding layouts (issue #227) are CPU-side authored SRG shapes for custom
render programs — the shared layouts that the numbered `binding_layout` presets
0–4 hard-coded, promoted to a zero-dep asset type on the mesh-render-style
pattern.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Render binding layout | `kRenderBindingLayoutSchema` | `0x000104` | `kAssetTypeRenderBindingLayout` (1052) | `RenderBindingLayoutAssetModule::create_render_binding_layout()` |
| HLSL binding prelude (issue #231) | `kHLSLBindingPreludeSchema` | `0x000105` | `AssetType::ShaderSource` (7) | graph-authored (`source` + `binding_layout` ports) |

A layout declares one optional root-constant block (`constants_semantic` +
visibility + a `constants_head` enum naming an existing renderer packer —
mvp16 / world_viewproj_camera36 / camera_snapped_terrain — plus authored tail
field declarations, name + type only), up to eight SRV binding rows (descriptor
semantic name, texture/structured kind, visibility), and up to two static
sampler rows. The `camera_snapped_terrain` head (issue #233) is the clipmap's
32-dword block generalized: the renderer fills it each frame from the live
camera + the recipe's height field / world footprint / lattice, so a custom
renderable can camera-follow-snap displaced terrain like the bespoke `0x708`
clipmap. A program using it needs a `scalar_field_texture` binding (the height
field) and an optional `placement` port (the world footprint). The tables are encoded as indexed scalar params
(`binding0_semantic`, `const0_name`, `sampler0_kind`, …). Registers are DERIVED
from row order (b0 / t0,t1… / s0,s1… in the object space 2) — never authored.
A custom render program (`0x000103`) consumes a layout through its optional
`binding_layout` input port; when wired, the layout defines the program's whole
SRG and the numbered presets are ignored.

Root-constant fields follow HLSL cbuffer packing, not a tight dword sum: a
vector may not straddle a 16-byte register, so a 2/3/4-dword field that would
cross one starts at the next register. `render_binding_constant_field_offset`
(in `render_binding_constants.h`) is the single rule shared by the block-size
derivation (`tail_dwords`), the custom renderable recipe's baked field offsets,
and the generated binding prelude's `packoffset` emission — the block is read
through a cbuffer, so all three must agree byte-for-byte.

The **HLSL binding prelude** (`0x000105`) is the shader-side twin of that
derivation: it prepends the layout's generated declarations — the `cbuffer`
with a `packoffset` on every member, the SRV rows in derived register order
(canonical element types where a semantic has one, a `WZ_BINDING_<SEMANTIC>`
register macro otherwise), and the static samplers — to an authored shader body,
outputting the combined `ShaderSource` a shader node then compiles. Routing the
include THROUGH the asset DAG (a node with a `source` body port and a
`binding_layout` port) instead of a `#include` resolved by the HLSL compiler
keeps it key-visible: a layout edit re-keys the prelude, the shader, and the
program, so the generated declarations can never go stale against the root
signature via a cached shader. `generate_hlsl_binding_prelude` is the pure
generator; shaders authored against a layout `#include` nothing and declare no
registers or offsets the layout owns.

---

## Renderables

Renderables bind a source data asset (mesh, splat cloud, scalar field, terrain)
to a render program and domain, producing an entry in `RenderableAssetTable`.

RHI-native renderables (the live path — each produces an `RhiRenderableRecipe`
drawn by `RhiSceneRenderer`):

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| RHI pull mesh (+ optional style) | `kRhiPullMeshRenderableSchema` | `0x000706` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_rhi_pull_mesh()` |
| GPU sparse mesh | `kGpuSparseMeshRenderableSchema` | `0x000707` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_gpu_sparse_mesh_renderable()` |
| Clipmap landscape | `kClipmapLandscapeRenderableSchema` | `0x000708` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_clipmap_landscape()` |
| Gaussian splat cloud (RHI) | `kGaussianSplatCloudRhiRenderableSchema` | `0x000709` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_gaussian_splat_cloud_rhi()` |
| Custom renderable (issue #228) | `kCustomRenderableSchema` | `0x00070A` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_custom_renderable()` or graph-authored |

The custom renderable (`0x00070A`) is the zero-per-look-C++ form: a pull mesh +
a custom render program whose SRG comes from an authored render binding layout
(#227), up to eight generic `Any`-typed optional binding ports mapped to the
layout's descriptor semantics by indexed params (`binding0_semantic`, …), and
authored values for the layout's declared constant tail fields (`const0_name` /
`const0_value` / `const0_w`, …). The compiler validates every authored binding
against the wired program's layout — unbound semantic, unknown semantic, kind
vs source asset type (`render_binding_sources.h`, THE source-type → published
GPU-resource map), unknown constant name — and surfaces failures on the node
via `compile_failed_node(input, reason)`. `RhiSceneRenderer` draws the compiled
recipe generically: it walks `recipe.bindings` (`rhi_asset_identity(key,
variant)`), binds the mesh-pull buffers only when the layout declares them, and
packs the root-constant block as the layout's head packer (mvp16 /
world_viewproj_camera36) followed by the authored tail values at their baked
offsets. The `Any` port sentinel (`wz::asset::AssetType::Any`) skips the
edge-time provider-type check; kind validation happens at compile instead.

Scene ingredients (issue #229) drive the same schema from a scene node: the
#213 `geometry`/`render_program` cluster grows `renderable_bindings[]`
(semantic + asset-graph anchor, key re-bridged on every bind) and
`renderable_constants[]` (per-instance values for the layout's declared tail
fields). When either is present on a Mesh-geometry node, the engine
synthesizes a `0x00070A` renderable per node
(`RenderableAssetModule::create_custom_renderable`,
`"render_binding/<node.id>"`) instead of the bare pull mesh. The recipe
carries EVERY declared tail field (authored default or zero), and the
renderer merges the node's constant overrides over the packet's copy of the
block at pack time — so editing an instance constant never re-keys or
recompiles anything (the look analog of transform-on-node).

Legacy renderables (produce `RenderableAssetData`; consumed only by the editor
field preview and the deprecated terrain path — retirement tracked by #222/#179):

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Scalar field preview | `kScalarFieldDebugRenderableSchema` | `0x000702` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_scalar_field_debug()` |

Schemas `0x000703` (terrain debug) and `0x000704` (terrain mesh surface) were
deleted with the nine MeshIA builtin programs they drew through; terrain is a
0x70A `CameraSnappedTerrain` custom renderable (#234).

Schemas `0x000700` (mesh wireframe), `0x000701` (gaussian splat debug), and
`0x000705` (mesh styled) were deleted by issue #195: a mesh renderable is one
recipe (`0x000706`) — mesh + render program + optional `MeshRenderStyle` — where
wireframe-vs-solid is a program `RasterMode` and style shading flows as data.
The splat-debug capability lives on as the RHI splat renderable (`0x000709`).
Splat color-LOD attachment (`companion_asset` + `color_lod`) was a `0x000701`
feature and currently has no RHI-path equivalent.

`BuiltinRenderProgram` is an enum in `engine/assets/renderable/renderable.h`:
- `GaussianSplatDebug` -> `RenderDomain::Splat`
- `ScalarFieldDebug` -> `RenderDomain::Debug`
- `GaussianSplatPullDebug` -> pull-based splat path (no IA, t0 SRV)
- `GaussianSplatNeighborhoodColorBlend` -> pull-based + LOD color blend modes
- `GaussianSplatTerrainCoverageDebug` -> pull-based + coverage modes (depth-writing)
- `SkySurface` -> `RenderDomain::Sky`, camera-relative sky surface visual path

`RenderableKind::VectorField` exists so editor/runtime realization can upload
vector fields for non-mesh visual consumers such as the sky path. There is no
dedicated vector-field renderable schema yet.

---

## Lighting And Environment

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Direct light | `kDirectLightSchema` | `0x001000` | `kAssetTypeDirectLight` (2268) | `LightAssetModule::create_direct_light()` |
| Ambient lighting | `kAmbientLightingSchema` | `0x001001` | `kAssetTypeAmbientLighting` (2269) | `LightAssetModule::create_ambient_lighting()` |
| HDRI environment | `kHDRIEnvironmentSchema` | `0x001002` | `kAssetTypeEnvironmentMap` (2273) | `LightAssetModule::create_hdri_environment()` |

`HDRIEnvironmentAsset` depends on an imported source file carrier and stores
environment-level controls: exposure, X/Y/Z rotation,
lighting/reflection/background intensity, diffuse environment metadata, and
optional dominant-light metadata. Scene components such as a sky surface can
reference or share source images with this asset while deciding independently
whether to render an image as a background, feed environment lighting, or
align/link authored directional lights.

`HDRIEnvironmentAsset` is radiance/lighting data. It is not the sky surface.
The current visible sky path can decode OpenEXR image data through the same HDR
image loader and upload it as an `RGBA32_FLOAT` GPU texture, but that is a
narrow sky visual realization path. It does not create a general `TextureAsset`
and does not make the sky visual affect terrain lighting.

---

## Scenes

Scene assets compile authored JSON scene documents into `SceneAssetData` stored
in `SceneAssetTable`. Authored component descriptors inside the scene are
covered separately in [`authored_scene_components.md`](authored_scene_components.md).

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Scene from JSON document | `kSceneFromJSONSchema` | `0x000710` | `kAssetTypeScene` (2049) | `SceneAssetModule::create_scene_from_json()` |

`SceneAssetModule::create_scene_from_json()` creates a JSON document asset first,
then registers a scene node depending on that parsed JSON document.

### Sky Visual Scene Components

Sky surface and sky visual records are authored scene components, not standalone
asset-system capabilities yet. They materialize into `SceneSkyDrawAsset` records
on `SceneAssetData` / `SceneInstance`, and the scene editor converts those into
scene-render `SkyDrawCommand` values.

Current visual kinds:

- `SolidColor`
- `DirectionDebug`
- `Gradient`
- `ScalarField`
- `VectorField`
- `EquirectangularTexture`

The sky surface is the presentation canvas: camera-relative, unlit, no depth
write, and drawn through `BuiltinRenderProgram::SkySurface`. The sky visual is
the content projected onto that canvas. HDRI environment assets remain separate
radiance/lighting sources. They may share image files with sky visuals, but
neither owns the other.

`EquirectangularTexture` currently supports OpenEXR image paths through
`texture_path` / `texture_format`. This is a compatibility bridge while the
general texture asset pipeline is still reserved but unimplemented.

---

## Parsed Data Documents

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| CSV table | `kCSVTableSchema` | `0x000D00` | `kAssetTypeCSVTable` (156) | `CSVAssetModule::create_csv()` |
| JSON document | `kJSONDocumentSchema` | `0x000012` | `kAssetTypeJSONDocument` (151) | `JSONAssetModule::create_json()` |
| TOML document | `kTOMLDocumentSchema` | `0x000013` | `kAssetTypeTOMLDocument` (152) | `TOMLAssetModule::create_toml()` |

CSV depends on a `kCSVFileSchema` carrier (header mode is encoded in the key, so
the same file compiled with different header modes yields distinct asset keys).
JSON and TOML documents currently depend on `kTextFileSchema` carriers.
`kJSONFileSchema` and `kTOMLFileSchema` are reserved schema IDs, but the current
module APIs do not register source carriers with those schemas.

---

## Diagnostics and Tooling Data

These capabilities are in the editor/tooling AssetType range (4096–8191) and
exist primarily to support diagnostics pipelines and CSV report generation.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Inline data table | `kInlineDataTableSchema` | `0x001200` | `kAssetTypeDataTable` (4110) | `DataTableAssetModule` |
| Diagnostic resampled time series | `kDiagnosticTableResampleTimeSeriesSchema` | `0x001201` | `kAssetTypeDiagnosticResampledTimeSeries` (4111) | `DiagnosticResampledTimeSeriesAssetModule` |
| CSV export | `kCSVExportSchema` | `0x001202` | `kAssetTypeCSVExport` (4112) | `CSVExportAssetModule` |
| Time series → DataTable bridge | `kDiagnosticResampledTimeSeriesToDataTableSchema` | `0x001203` | `kAssetTypeDataTable` (4110) | `DiagnosticResampledTimeSeriesAssetModule` |
| Timeframe summary | `kDiagnosticTimeframeSummarySchema` | `0x001204` | `kAssetTypeDiagnosticTimeframeSummary` (4113) | `DiagnosticTimeframeSummaryAssetModule` |
| Timeframe summary → DataTable bridge | `kDiagnosticTimeframeSummaryToDataTableSchema` | `0x001205` | `kAssetTypeDataTable` (4110) | `DiagnosticTimeframeSummaryAssetModule` |

`kAssetTypeDataTable` (4110) is a generic rectangular string table used as input
for `kCSVExportSchema` and other DataTable consumers. Multiple schemas can produce it.

The CSV export compiler builds deterministic CSV text and stores it in
`CSVExportTable`. File writing is an explicit follow-up call through
`CSVExportAssetModule::write_export_to_file()`, not a compiler side effect.

The timeframe summary filters a DataTable by a frame-index column, splits into
fixed-size frame buckets, and computes per-metric min/max/mean/delta/first/last
statistics with a deterministic `summary_text` per bucket.

---

## Terrain Support Types

These are not asset capabilities (they have no schema ID and are not registered
with `AssetSystem`), but they are key types in the terrain splat pipeline that
live in the asset layer.

### ScalarFieldSet

**Header:** `engine/assets/scalar_field/scalar_field_set.h`

A named, co-registered collection of `ScalarFieldData` objects. All fields in a
set share the same (width, height, depth) dimensions, validated at insertion time.
Used by the multi-field terrain recipe compiler to resolve named field references
(e.g. "height", "curvature", "normal_x") to their data.

### TerrainSplatFieldRecipe

**Header:** `engine/assets/gaussian_splat/gaussian_splat_terrain_recipe.h`

A recipe struct describing how a `ScalarFieldSet` maps to a `GaussianSplatCloud`.
Separates geometry (height field name + compile desc) from color (three modes:
slope/height debug, RGB fields, or grayscale field, each using `FieldMap` routing),
from optional imported normals (`NormalFieldConfig`), curvature
(`CurvatureFieldConfig`), and peaks (`PeaksFieldConfig`). This is the primary
configuration for multi-field terrain rendering.

### TerrainCoverageKernel

**Header:** `engine/assets/gaussian_splat/terrain_coverage_kernel.h`

Four kernel modes (Gaussian, SmoothDisc, PolynomialDisc, HardDisc) that define
how a normalized splat-local radius maps to a coverage weight in [0,1]. Used by
CPU-side terrain reconstruction, GPU coverage shaders, and toolhost UI. The GPU
shader code mirrors these formulas.

### TerrainReconstruction

**Header:** `engine/assets/gaussian_splat/terrain_reconstruction.h`

CPU-side terrain surface reconstruction from Gaussian splats. Treats splats as
radial basis functions over the (x, z) domain and evaluates weighted-average
height, color, and normal on a regular grid. Produces `TerrainReconVertex` /
index arrays for GPU upload as a triangle mesh. Independent of the GPU backend.

### TerrainSplatCompileService

**Header:** `engine/assets/gaussian_splat/terrain_splat_compile_service.h`

Pure CPU compilation service wrapping the two compile paths (simple heightfield
and multi-field recipe) plus optional color-LOD pass into a single
`compile_terrain_splat_surface()` call. Value-type input/output, no GPU, no
asset system, no side effects. Used by the toolhost for live tuning recompiles.

### DecodedGaussianSplat

**Header:** `engine/assets/gaussian_splat/gaussian_splat_decode.h`

CPU-side decode of a `GaussianSplat` from its stored encoding (log-scale,
logit-opacity, SH-DC color, PLY-convention quaternion) into world-space values.
Centralizes the decode steps previously duplicated across the GPU upload path,
terrain compiler, and toolhost.

### TerrainSplatPresets

**Header:** `engine/assets/gaussian_splat/gaussian_splat_terrain_preset.h`

Named bundles of known-good parameter values for terrain surface compilation
(`TerrainSplatSurfacePreset`) and coverage rendering
(`TerrainSplatCoveragePreset`). The `kSmoothTerrainSurface` and
`kSmoothTerrainCoverage` presets capture the baseline configuration for
continuous terrain surface rendering.

---

## Summary Count

| Category | Implemented capabilities |
|----------|--------------------------|
| File carriers | 7 |
| Shaders / render programs / compute pipelines | 4 |
| Scalar fields | 2 |
| Vector fields | 1 |
| Meshes | 6 |
| Mesh derived fields | 5 |
| Mesh sparse operators | 1 |
| Terrain | 3 |
| Collision assets | 2 |
| Gaussian splat clouds | 5 |
| Gaussian splat color LOD | 1 |
| Mesh render styles | 1 |
| Renderables | 6 |
| Lighting / environment | 3 |
| Scenes | 1 |
| Parsed data documents | 3 |
| Diagnostics / tooling data | 6 |
| **Total** | **57** |

---

## What is not yet implemented

The following AssetType constants are reserved in `type_extensions.h` but have no
schema, compiler, module API, or runtime table:

- Textures (`kAssetTypeTexture`, all general GPU texture variants). The sky path
  has a narrow OpenEXR float-image upload for visible equirectangular sky
  visuals, but that is not a reusable `TextureAsset`.
- Graph-addressable GPU asset types (`kAssetTypeGPUShader`,
  `kAssetTypeGPUPipeline`, etc.). Runtime GPU residency tables exist for
  realized meshes, mesh fields, sparse operators, shaders, and pipelines, but
  those handles are not standalone asset-system DAG nodes.
- Material definitions, instances, graphs
- Prefab / world / level assets beyond the implemented JSON scene asset
- Animation clips, skeletons, blend trees
- General physics simulation shapes beyond the implemented CPU-side
  `CollisionAsset` mesh/terrain descriptors
- Audio clips, sound cues
- UI layouts, font atlases
- All gameplay data types (items, quests, dialogue, etc.)
- All AI behavior types (behavior trees, GOAP, etc.)
- Remaining VFX / particle types other than `kAssetTypeVectorField`
- Remaining lighting environment types (probes, skybox assets, lightmaps, etc.)
- All cinematic types (timelines, cutscenes, camera paths)

See `type_extensions.h` for the full reserved range with numeric values.
