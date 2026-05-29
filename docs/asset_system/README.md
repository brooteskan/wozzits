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

## Renderables

Renderables bind a source data asset (mesh, splat cloud, scalar field) to a
render program and domain, producing an entry in `RenderableAssetTable`.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Mesh wireframe debug | `kMeshWireframeRenderableSchema` | `0x000700` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_mesh_wireframe()` |
| Gaussian splat debug | `kGaussianSplatDebugRenderableSchema` | `0x000701` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_gaussian_splat_debug()` |
| Scalar field debug | `kScalarFieldDebugRenderableSchema` | `0x000702` | `kAssetTypeRenderable` (1048) | `RenderableAssetModule::create_scalar_field_debug()` |

All three produce `RenderableAssetData` in `RenderableAssetTable`.
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
- `MeshWireframeDebug` → `RenderDomain::Debug`
- `GaussianSplatDebug` → `RenderDomain::Splat`
- `ScalarFieldDebug` → `RenderDomain::Debug`
- `GaussianSplatPullDebug` → pull-based splat path (no IA, t0 SRV)
- `GaussianSplatNeighborhoodColorBlend` → pull-based + LOD color blend modes
- `GaussianSplatTerrainCoverageDebug` → pull-based + coverage modes (depth-writing)

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
| Meshes | 5 |
| Terrain | 2 |
| Gaussian splat clouds | 5 |
| Gaussian splat color LOD | 1 |
| Renderables | 3 |
| Scenes | 1 |
| Parsed data documents | 3 |
| Diagnostics / tooling data | 6 |
| **Total** | **37** |

---

## What is not yet implemented

The following AssetType constants are reserved in `type_extensions.h` but have no
schema, compiler, module API, or runtime table:

- Textures (`kAssetTypeTexture`, all GPU texture variants)
- All GPU-resident types (`kAssetTypeGPUShader`, `kAssetTypeGPUPipeline`, etc.)
- Material definitions, instances, graphs
- Prefab / world / level assets beyond the implemented JSON scene asset
- Animation clips, skeletons, blend trees
- Physics shapes and collision meshes
- Audio clips, sound cues
- UI layouts, font atlases
- All gameplay data types (items, quests, dialogue, etc.)
- All AI behavior types (behavior trees, GOAP, etc.)
- All VFX / particle types
- All lighting environment types (probes, skybox, lightmaps, etc.)
- All cinematic types (timelines, cutscenes, camera paths)

See `type_extensions.h` for the full reserved range with numeric values.
