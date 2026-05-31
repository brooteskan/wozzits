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

## Shaders and Render Programs

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| HLSL shader pair | `kHLSLShaderSchema` | `0x000100` | `AssetType::Shader` | `ShaderAssetModule::create_shader_pair()` |
| Builtin render program | `kBuiltinRenderProgramSchema` | `0x000101` | `kAssetTypeRenderProgram` (1049) | `RenderProgramAssetModule::create_builtin()` |

`ShaderPairAsset` wraps two `AssetKey` values (vertex + pixel). Resolved to
`ShaderPairHandles` (GPU handles) via `ShaderAssetModule::get_shader_pair()`.

`kAssetTypeRenderProgram` is a CPU-side declaration: binding model, topology,
render domain/policy, and shader pair reference. It does not directly own a
backend PSO; that realization step is deferred.

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

All mesh schemas produce `kAssetTypeMesh` stored in `MeshTable`.
`MeshAssetModule::get_mesh()` → `MeshHandle`; `get_mesh_data()` → `const MeshData*`.

The GLB importer depends on a `kRawFileSchema` file carrier. `GLBMeshDesc::mesh_index`
selects which mesh primitive inside the GLB to compile (default 0).
When present, GLB `NORMAL` and `TEXCOORD_0` attributes are preserved on
`MeshData` and exposed through `mesh.has_normals` / `mesh.has_uv0` so downstream
asset compilers can choose terrain/material sources explicitly.

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

Both schemas produce `TerrainAssetData` stored in `TerrainAssetTable`.
The height-field variant depends on a compiled `kAssetTypeScalarField`; the mesh
variant depends on a compiled `kAssetTypeMesh`. V0 records domain bounds,
resolution/source metadata, basic render/collision policy, and query capability
flags. Height-field terrain supports height-query semantics; mesh terrain records
surface bounds/source metadata and leaves acceleration-backed ray/project queries
for a later compiler/runtime layer.

Mesh terrain also records source-attribute availability and selected terrain
sources. If source mesh normals are present, `TerrainFromMesh` defaults to
`TerrainNormalSource::MeshVertexNormal`; otherwise it falls back to
`DerivedGeometry`. If UV0 is present, it defaults to `TerrainUVSource::MeshUV0`;
otherwise it records no UV source unless the caller explicitly requests a
generated source such as `PlanarXZ`. The mesh terrain compiler also records the
source triangle count and the number of triangles accepted by
`min_surface_normal_y` / `include_backfaces`, using geometric triangle normals
for terrain policy rather than smoothed/imported vertex normals.

Terrain visibility is expressed through a renderable asset, not editor preview
state. `RenderableAssetModule::create_terrain_debug()` registers a renderable
that depends on a compiled `TerrainAsset`. The compiler adapts mesh terrain to
the mesh debug path and keeps the terrain key as
`RenderableAssetData::companion_asset`. Height-field terrain renderables keep
the height field as their source asset and are realized by the GPU scene
resolver as a bounded wireframe preview mesh until a terrain-specific renderer
is available.

Mesh-backed terrain can also register a surface renderable through
`RenderableAssetModule::create_terrain_surface()`. This remains a
`kAssetTypeRenderable` recipe: the renderable source is the terrain mesh, the
terrain asset is preserved in `RenderableAssetData::companion_asset`, and the
selected program is `BuiltinRenderProgram::TerrainMeshSurface`. V1 consumes
mesh normals and UV0 when present on the mesh asset. Height-field terrain still
uses the existing debug/preview path until a generated surface mesh path is
introduced.

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
rendering. `RenderableAssetModule::create_mesh_styled()` chooses the surface
shader when the surface layer is enabled and the mesh has normals; otherwise it
uses the wireframe path and logs a normals-specific fallback when needed. Scene
mesh authoring materialization creates the style asset from the node component
before creating the renderable.

---

## Renderables

Renderables bind a source data asset (mesh, splat cloud, scalar field, terrain)
to a render program and domain, producing an entry in `RenderableAssetTable`.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Mesh wireframe debug | `kMeshWireframeRenderableSchema` | `0x000700` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_mesh_wireframe()` |
| Gaussian splat debug | `kGaussianSplatDebugRenderableSchema` | `0x000701` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_gaussian_splat_debug()` |
| Scalar field debug | `kScalarFieldDebugRenderableSchema` | `0x000702` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_scalar_field_debug()` |
| Terrain debug | `kTerrainDebugRenderableSchema` | `0x000703` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_terrain_debug()` |
| Terrain mesh surface | `kTerrainSurfaceRenderableSchema` | `0x000704` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_terrain_surface()` |
| Mesh styled | `kMeshStyledRenderableSchema` | `0x000705` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_mesh_styled()` |

All five produce `RenderableAssetData` in `RenderableAssetTable`.
`RenderableAssetData` carries `RenderableKind`, `RenderDomain`, `BuiltinRenderProgram`,
policy flags, spatial bounds, and an optional `companion_asset` key (used to attach
a `GaussianSplatColorLOD` to a splat renderable).

`GaussianSplatDebugRenderableDesc` accepts an optional `color_lod` field
(`GaussianSplatColorLODAsset`). When set, the renderable's `companion_asset` is
populated, and the GPU upload path packs per-splat neighborhood color + confidence
into the structured buffer alongside the base splat data. When unset, the LOD
slot falls back to base color with confidence = 0 (no behavioral change versus the
pre-LOD renderer).

`BuiltinRenderProgram` is an enum in `engine/assets/renderable/renderable.h`:
- `MeshWireframeDebug` -> `RenderDomain::Debug`
- `MeshWireframeDepthDebug` -> depth-tested/depth-writing mesh wireframe debug
- `MeshDepthPrepassDebug` -> depth-only mesh prepass debug
- `TerrainMeshSurface` -> opaque mesh terrain surface path consuming position/normal/UV
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
| CSV table | `kCSVTableSchema` | `0x000D00` | `kAssetTypeCSVTable` (150) | `CSVAssetModule::create_csv()` |
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

### LandscapeDocument

**Header:** `engine/assets/landscape/landscape_document.h`

A portable, versioned JSON sidecar (`.wzlandscape.json`) that captures the
user-authored configuration for a terrain surface. This is a recipe document, not
a baked asset: it stores source references (field file paths and formats),
compile descriptors (`TerrainSplatFieldRecipe`), render settings (coverage,
field accumulation, surface reconstruction, LOD, density), and toolhost
preview state. Serialized via yyjson in `landscape_document_json.cpp`.

---

## Summary Count

| Category | Implemented capabilities |
|----------|--------------------------|
| File carriers | 7 |
| Shaders / render programs | 2 |
| Scalar fields | 2 |
| Vector fields | 1 |
| Meshes | 5 |
| Terrain | 2 |
| Gaussian splat clouds | 5 |
| Gaussian splat color LOD | 1 |
| Renderables | 5 |
| Lighting / environment | 3 |
| Scenes | 1 |
| Parsed data documents | 3 |
| Diagnostics / tooling data | 6 |
| **Total** | **43** |

---

## What is not yet implemented

The following AssetType constants are reserved in `type_extensions.h` but have no
schema, compiler, module API, or runtime table:

- Textures (`kAssetTypeTexture`, all general GPU texture variants). The sky path
  has a narrow OpenEXR float-image upload for visible equirectangular sky
  visuals, but that is not a reusable `TextureAsset`.
- All GPU-resident types (`kAssetTypeGPUShader`, `kAssetTypeGPUPipeline`, etc.)
- Material definitions, instances, graphs
- Prefab / world / level assets beyond the implemented JSON scene asset
- Animation clips, skeletons, blend trees
- Physics shapes and collision meshes
- Audio clips, sound cues
- UI layouts, font atlases
- All gameplay data types (items, quests, dialogue, etc.)
- All AI behavior types (behavior trees, GOAP, etc.)
- Remaining VFX / particle types other than `kAssetTypeVectorField`
- Remaining lighting environment types (probes, skybox assets, lightmaps, etc.)
- All cinematic types (timelines, cutscenes, camera paths)

See `type_extensions.h` for the full reserved range with numeric values.
