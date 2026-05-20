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

## File Carriers and Source Nodes

These are leaf nodes in the asset graph. They carry raw bytes from the filesystem
into the system; other assets declare them as dependencies.

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| Raw file | `kRawFileSchema` | `0x000001` | `kAssetTypeRawFile` (64) | `FileCarrierAssetModule` |
| HLSL source file | `kHLSLFileSchema` | `0x000002` | `AssetType::ShaderSource` | Internal to `ShaderAssetModule` |
| JSON file | `kJSONFileSchema` | `0x00000A` | — (carrier only) | `FileCarrierAssetModule` |
| TOML file | `kTOMLFileSchema` | `0x00000C` | — (carrier only) | `FileCarrierAssetModule` |
| CSV file | `kCSVFileSchema` | `0x00000E` | — (carrier only) | `FileCarrierAssetModule` |

---

## Shaders and Render Programs

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| HLSL shader pair | `kHLSLShaderSchema` | `0x000100` | `AssetType::Shader` | `ShaderAssetModule::create_shader_pair()` |
| Builtin render program | `kBuiltinRenderProgramSchema` | `0x000101` | `kAssetTypeRenderProgram` (1049) | Compiled as part of the renderable subsystem |

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

All mesh schemas produce `kAssetTypeMesh` stored in `MeshTable`.
`MeshAssetModule::get_mesh()` → `MeshHandle`; `get_mesh_data()` → `const MeshData*`.

The GLB importer depends on a `kRawFileSchema` file carrier. `GLBMeshDesc::mesh_index`
selects which mesh primitive inside the GLB to compile (default 0).

---

## Gaussian Splat Clouds

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| PLY file import | `kGaussianSplatFromPLYSchema` | `0x000500` | `kAssetTypeGaussianSplatCloud` (131) | `GaussianSplatAssetModule::create_from_ply()` |
| Scalar-field-derived cloud | `kGaussianSplatFromFieldSchema` | `0x000501` | `kAssetTypeGaussianSplatCloud` (131) | `GaussianSplatAssetModule::create_from_scalar_field()` |
| Procedural/debug cloud | `kProceduralGaussianSplatCloudSchema` | `0x000502` | `kAssetTypeGaussianSplatCloud` (131) | `GaussianSplatAssetModule::create_procedural_cloud()` |

All three produce `GaussianSplatCloudData` in `GaussianSplatCloudTable`.
The PLY importer depends on a `kRawFileSchema` carrier.
The scalar-field variant depends on a compiled `kAssetTypeScalarField`.

`GaussianSplatCloudData::splats` is the ordered list that defines upload index order.
The `cloud_local_index` field on `SplatDescriptor` / `SplatPrimitive` in
wozzits-scene-render must match position in this vector (see scene-render docs).

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
policy flags, and spatial bounds.

`BuiltinRenderProgram` is an enum in `engine/assets/renderable/renderable.h`:
- `MeshWireframeDebug` → `RenderDomain::Debug`
- `GaussianSplatDebug` → `RenderDomain::Splat`
- `ScalarFieldDebug` → `RenderDomain::Debug`
- `GaussianSplatPullDebug` → pull-based splat path (no IA, t0 SRV)

---

## Parsed Data Documents

| Capability | Schema constant | Schema value | Output AssetType | Module / API |
|---|---|---|---|---|
| CSV table | `kCSVTableSchema` | `0x000D00` | `kAssetTypeCSVTable` (150) | `CSVAssetModule::create_csv()` |
| JSON document | `kJSONDocumentSchema` | `0x000012` | `kAssetTypeJSONDocument` (151) | `JSONAssetModule::create_json()` |
| TOML document | `kTOMLDocumentSchema` | `0x000013` | `kAssetTypeTOMLDocument` (152) | `TOMLAssetModule::create_toml()` |

CSV depends on a `kCSVFileSchema` carrier (header mode is encoded in the key, so
the same file compiled with different header modes yields distinct asset keys).
JSON depends on `kJSONFileSchema`; TOML depends on `kTOMLFileSchema`.

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

The CSV export compiler writes to the output path as a side effect of compilation;
file writing is explicit, not a hidden compiler side effect on the asset object itself.

The timeframe summary filters a DataTable by a frame-index column, splits into
fixed-size frame buckets, and computes per-metric min/max/mean/delta/first/last
statistics with a deterministic `summary_text` per bucket.

---

## Summary Count

| Category | Implemented capabilities |
|----------|--------------------------|
| File carriers | 5 |
| Shaders / render programs | 2 |
| Scalar fields | 2 |
| Meshes | 4 |
| Gaussian splat clouds | 3 |
| Renderables | 3 |
| Parsed data documents | 3 |
| Diagnostics / tooling data | 6 |
| **Total** | **28** |

---

## What is not yet implemented

The following AssetType constants are reserved in `type_extensions.h` but have no
schema, compiler, module API, or runtime table:

- Textures (`kAssetTypeTexture`, all GPU texture variants)
- All GPU-resident types (`kAssetTypeGPUShader`, `kAssetTypeGPUPipeline`, etc.)
- Material definitions, instances, graphs
- Scene / prefab / world / level
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
