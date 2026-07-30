# Gaussian Splat Terrain Assets

> Two asset schemas that produce `GaussianSplatCloudData` from 2D heightfield
> sources, generating anisotropic surface-tangent-aligned splats suitable for
> continuous terrain rendering.

## Identity

### Terrain surface from height field

| Field | Value |
|-------|-------|
| Schema ID | `kGaussianSplatTerrainSurfaceFromHeightFieldSchema` = `0xF11ECA55E7_000503` |
| Output AssetType | `kAssetTypeGaussianSplatCloud` (131) |
| CPU storage | `GaussianSplatCloudTable` |
| GPU realization | Upload via `RenderableGpuCache` (same as other splat clouds) |

### Gaea R32 + JSON sidecar recipe

| Field | Value |
|-------|-------|
| Schema ID | `kTerrainSplatFromGaeaR32Schema` = `0xF11ECA55E7_000504` |
| Output AssetType | `kAssetTypeGaussianSplatCloud` (131) |
| CPU storage | `GaussianSplatCloudTable` |
| GPU realization | Upload via `RenderableGpuCache` (same as other splat clouds) |

Both schemas produce the same `kAssetTypeGaussianSplatCloud` output as PLY import
and procedural clouds.

## Module API

**Header:** `engine/assets/gaussian_splat_asset_module.h`
**Class:** `GaussianSplatAssetModule`

```cpp
// Terrain surface from a compiled ScalarField.
GaussianSplatCloudAsset create_terrain_surface_from_height_field(
    const GaussianSplatTerrainSurfaceFromHeightFieldDesc& desc);

// Gaea R32 + compiled JSON sidecar recipe.
GaussianSplatCloudAsset create_terrain_splat_from_gaea_r32(
    const TerrainSplatFromGaeaR32Desc& desc);

// Common resolve/inspect (shared with all splat cloud schemas).
GaussianSplatCloudHandle get_cloud(const GaussianSplatCloudAsset& asset) const;
const GaussianSplatCloudData* get_cloud_data(GaussianSplatCloudHandle handle) const;
```

## Key Factories

**Terrain surface:**
`engine/assets/key_factories/gaussian_splat_terrain_surface.h`

```cpp
wz::asset::AssetKey make_gaussian_splat_terrain_surface_from_height_field_key(
    const wz::asset::AssetKey& scalar_field_key,
    const GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc& desc) noexcept;
```

All compile parameters (height_scale, step_x/z, overlap_factor, thickness,
subsample_step, opacity, luminance, normal smoothing) are hashed into the key.

**Gaea R32:**
`engine/assets/key_factories/terrain_splat_from_gaea_r32.h`

```cpp
wz::asset::AssetKey make_terrain_splat_from_gaea_r32_key(
    const wz::asset::AssetKey& r32_file_key,
    const wz::asset::AssetKey& sidecar_json_key) noexcept;
```

Identity is fully determined by the .r32 file key, the sidecar JSON document key,
and the compiler version. The sidecar JSON document key is currently derived from
its text-file carrier key, so editing the .json sidecar without changing its path
does not automatically invalidate the cache.

## Dependencies

### Terrain surface from height field

| Dependency | How required |
|------------|--------------|
| `kAssetTypeScalarField` | Source height field; always required |

### Gaea R32 recipe

| Dependency | How required |
|------------|--------------|
| `kRawFileSchema` (.r32 bytes) | Heightmap data; always required |
| `kAssetTypeJSONDocument` (.json sidecar) | World-space interpretation parameters; always required |

## Compile Descriptor

`GaussianSplatTerrainSurfaceFromHeightFieldCompileDesc` (in `gaussian_splat.h`):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `height_scale` | float | 1.0 | Exaggeration multiplier on raw field values |
| `step_x` | float | 1.0 | World-space X distance between grid columns |
| `step_z` | float | 1.0 | World-space Z distance between grid rows |
| `overlap_factor` | float | 1.25 | Tangent-plane extent multiplier; 1.2-1.5 = neighbours overlap |
| `thickness` | float | 0.0 | Normal-axis extent (world units); 0 = auto |
| `subsample_step` | uint32 | 1 | Emit every Nth cell; extents scale to keep coverage |
| `opacity` | float | 0.95 | Splat opacity; encoded as logit on output |
| `flat_luminance` | float | 0.55 | Display luminance at slope = 0 |
| `steep_luminance` | float | 0.30 | Display luminance at slope = 1 |
| `max_slope_stretch` | float | 2.0 | Clamp on per-splat tangent-plane scale from slope |
| `normal_smoothing_enabled` | bool | false | Enable Gaussian blur of surface normals |
| `normal_smoothing_radius_cells` | uint32 | 2 | Kernel radius in grid cells |
| `normal_smoothing_sigma_cells` | float | 1.0 | Gaussian sigma in grid cells |

The Gaea R32 sidecar JSON carries the same parameters (height_scale, step_x,
step_z are required; all others are optional with defaults matching the desc).

## Terrain vs. Simple Scalar-Field Splats

The terrain-surface compiler (`0x000503`) is distinct from the simple
scalar-field debug splatter (`kGaussianSplatFromFieldSchema`, `0x000501`):

| Aspect | Simple (0x000501) | Terrain (0x000503) |
|--------|-------------------|--------------------|
| Splat orientation | Axis-aligned | Surface-tangent-aligned (anisotropic) |
| Height interpretation | Normalized [0,1] for visualization | Raw world elevations |
| Normal smoothing | None | Optional Gaussian blur |
| Slope stretch clamping | None | Configurable max stretch |
| Subsample / LOD | None | Subsample step with extent compensation |
| Color model | Normalized value gradient | Slope-modulated luminance (or multi-field recipe) |

## Multi-Field Recipe Compilation

Beyond the single-heightfield compile path, the terrain system supports a
multi-field recipe via `TerrainSplatFieldRecipe` + `ScalarFieldSet`.

The recipe-mode compile path (invoked via `TerrainSplatCompileService`) takes a
`ScalarFieldSet` containing named co-registered fields (e.g. "height",
"curvature", "normal_x", "normal_y", "normal_z", "peaks") and a
`TerrainSplatFieldRecipe` that describes how those fields map to splat attributes:

- **Geometry** — from the named height field + compile desc
- **Color** — three modes via `TerrainSplatColorMode`:
  - `SlopeHeightDebug` — slope-modulated grayscale (no field mapping)
  - `RgbFields` — three `FieldMap` entries routing named fields to R, G, B
  - `GrayscaleField` — one `FieldMap` replicated to R = G = B
- **Normals** — `NormalFieldConfig` with three sources: finite-difference from
  height, imported from three scalar fields, or a blend of both. Supports axis
  remapping (Y-up vs Z-up), channel flipping, and encoded-range decode.
- **Curvature** — `CurvatureFieldConfig` modifying radius and opacity at
  high-curvature regions
- **Peaks** — `PeaksFieldConfig` modifying color brightness, opacity, and radius
  at local peaks/ridges

`FieldMap` applies a uniform pipeline: normalize (optional, using field min/max)
then scale, bias, and clamp to [0,1].

## Notes

- The terrain toolhost uses `TerrainSplatCompileService::compile_terrain_splat_surface()`
  for live tuning recompiles. This is a pure CPU function with no GPU or asset
  system dependency.

- `TerrainSplatSurfacePreset` and `TerrainSplatCoveragePreset` in
  `gaussian_splat_terrain_preset.h` capture known-good parameter combinations.
  `kSmoothTerrainSurface` and `kSmoothTerrainCoverage` are the baseline presets.

- Terrain configurations have no user-facing persistence format of their own.
  `LandscapeDocument` (`.wzlandscape.json`) was that format for the retired imgui
  terrain toolhost and was removed with it; authored terrain now persists through
  the scene document's terrain components like every other authored component.
