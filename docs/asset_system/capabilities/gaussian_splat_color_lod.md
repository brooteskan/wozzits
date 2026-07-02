# `GaussianSplatColorLODAsset`

> Per-splat prefiltered neighborhood color and confidence, derived from a
> `GaussianSplatCloudData`. Used as an LOD / anti-pop companion at render time
> to blend toward a locally-averaged color at distance or under stride.

## Identity

| Field | Value |
|-------|-------|
| Schema ID | `kGaussianSplatColorLODSchema` = `0xF11ECA55E7_000502` |
| Output AssetType | `kAssetTypeGaussianSplatColorLOD` = 4114 |
| CPU storage | `GaussianSplatColorLODTable` (owned by `EngineAssetLibrary`) |
| GPU realization | Packed into the GPU structured buffer alongside base splat data during renderable upload |

Note: the schema value `0x000502` is in the Gaussian splat range, not the
diagnostics range, despite the AssetType value (4114) being in the tooling range.
This is intentional: Color LOD is a derived product that exists specifically for
the terrain toolhost pipeline.

## Module API

**Header:** `engine/assets/gaussian_splat_color_lod_asset_module.h`
**Class:** `GaussianSplatColorLODAssetModule`

```cpp
// Create — register a color-LOD recipe derived from an existing cloud.
GaussianSplatColorLODAsset create_from_cloud(
    const GaussianSplatColorLODFromCloudDesc& desc);

// Resolve — returns a handle into the CPU table; null if not yet compiled.
GaussianSplatColorLODHandle get_lod(
    const GaussianSplatColorLODAsset& asset) const;

// Inspect — returns a pointer to the compiled CPU data; null if not ready.
const GaussianSplatColorLODData* get_lod_data(
    GaussianSplatColorLODHandle handle) const;
```

## Key Factory

**Header:** `engine/assets/key_factories/gaussian_splat_color_lod.h`

```cpp
wz::asset::AssetKey make_gaussian_splat_color_lod_key(
    const wz::asset::AssetKey& source_cloud_key,
    const GaussianSplatColorLODCompileDesc& desc) noexcept;
```

Identity components:
- `content_hash` — hash of the compile descriptor fields (radius, sigma, flags, gain)
- `schema_hash` — `kGaussianSplatColorLODSchema`
- `compiler_hash` — `kGaussianSplatColorLODCompilerVersion`
- `deps_hash` — derived from the source cloud's `AssetKey`

Different compile parameters or a different source cloud produce distinct keys.

## Dependencies

| Dependency | How required |
|------------|--------------|
| `kAssetTypeGaussianSplatCloud` | Source cloud; always required |

## CPU Runtime Data

`GaussianSplatColorLODData` (defined in `engine/assets/gaussian_splat/gaussian_splat_color_lod.h`):

- `neighborhood_color` — `vector<float>`, size = 3 * splat_count. Per-splat prefiltered neighborhood color, linear RGB in [0,1].
- `lod_confidence` — `vector<float>`, size = splat_count. Per-splat confidence in [0,1]: high = low local variance (safe to blend), low = high variance (keep original detail).

Parallel-array indexed by source splat index: index `i` of the LOD arrays
corresponds to index `i` of `GaussianSplatCloudData::splats`.

## Compile Descriptor

`GaussianSplatColorLODCompileDesc`:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `neighbor_radius_world` | float | 0.05 | Cell size for uniform-grid neighbor search; max contribution radius |
| `gaussian_sigma_world` | float | 0.025 | Gaussian falloff sigma (world units) |
| `include_self` | bool | true | Whether the splat contributes to its own neighborhood average |
| `use_opacity_weight` | bool | true | Multiply each neighbor's weight by its decoded opacity |
| `color_variance_gain` | float | 4.0 | Variance-to-confidence gain: confidence = 1 - clamp(var * gain) |

## GPU Realization

The Color LOD data is packed into the GPU structured buffer during renderable
upload. When a `RenderableAssetData` has a non-empty `companion_asset` key
pointing to a `kAssetTypeGaussianSplatColorLOD`, `RenderableGpuCache` resolves
it and passes the data to the DX12 upload path. The upload packs per-splat LOD
color and confidence into the `DX12GaussianSplatVertex` alongside the base
splat data.

When no LOD data is present, the GPU vertex's LOD slot falls back to base color
with confidence = 0 — no behavioral change versus the pre-LOD renderer.

GPU-side blend behavior is controlled by `SplatColorLODSettings` (pushed per-frame
via `set_splat_color_lod_settings()`), which selects the blend mode (Natural,
LODOnly, Confidence, Blended) and controls distance/stride/confidence parameters.

## Usage Example

```cpp
// 1. Create a splat cloud first.
auto cloud = splat_module.create_from_ply(ply_desc);

// 2. Create a color-LOD derived asset from that cloud.
GaussianSplatColorLODFromCloudDesc lod_desc{
    .name         = "my_cloud_lod",
    .source_cloud = cloud,
    .compile      = {
        .neighbor_radius_world = 0.05f,
        .gaussian_sigma_world  = 0.025f,
    },
};
auto lod = lod_module.create_from_cloud(lod_desc);

// 3. Create a renderable that uses both cloud + LOD.
// HISTORICAL (issue #195): the splat-debug renderable (0x000701) and
// create_gaussian_splat_debug() were deleted; splats render via the RHI splat
// renderable (0x000709, create_gaussian_splat_cloud_rhi), which does not yet
// consume a color LOD. The LOD asset type and its compilers remain (steps 1-2
// and 4 are current); renderable attachment awaits an RHI-path consumer.
GaussianSplatDebugRenderableWithLODDesc render_desc{
    .name       = "my_cloud_renderable",
    .splat_cloud = cloud,
    .color_lod   = lod,
};
auto renderable = renderable_module.create_gaussian_splat_debug(render_desc);

// 4. After compile: LOD data is available.
auto handle = lod_module.get_lod(lod);
if (const auto* data = lod_module.get_lod_data(handle))
{
    // data->splat_count() == source cloud splat count
    // data->neighborhood_color[i*3 + 0..2] = prefiltered RGB for splat i
    // data->lod_confidence[i] = confidence for splat i
}
```

## Notes

- The compiler uses a uniform-grid spatial index for neighbor lookups.
  `GaussianSplatColorLODCompileStats` reports occupied cell count, average/max
  neighbor count, and compile time.

- Compile is deterministic: identical input produces identical output.

- The `color_variance_gain` field controls how aggressively the compiler maps
  local color variance to low confidence. A value of 4.0 means a per-channel
  variance of 0.25 maps to confidence = 0 (keep original). Higher gains make
  the system more conservative about blending in variable regions.
