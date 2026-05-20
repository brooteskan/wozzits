# `RenderProgramAsset`

> CPU-side declaration of how a built-in render program is drawn: which shaders,
> which binding model, which render domain, and which render-state policy flags.
> This is the bridge between the asset system and the rendering layer — it names
> what to draw with, but does not own a backend PSO.

## Identity

| Field | Value |
|-------|-------|
| Schema ID | `kBuiltinRenderProgramSchema` = `0xF11ECA55E7_000101` |
| Output AssetType | `kAssetTypeRenderProgram` = 1049 |
| CPU storage | `RenderableAssetTable` (field `render_program` on `RenderableAssetData`) |
| GPU realization | None at the asset level; the backend allocates a PSO separately |

`kAssetTypeRenderProgram` lives in the shader/pipeline/render-state description
range (1024–1059), which is intentionally not the GPU-resident range (512–1023).
The asset declares intent; realization into a backend `kAssetTypeGPUGraphicsPipeline`
is a future step.

## What "render program" means here

A render program is not a GPU pipeline object. It is a CPU-side record that
captures:

- Which `BuiltinRenderProgram` variant this is (enum in `renderable/renderable.h`)
- Which `RenderDomain` it targets (`Debug`, `Opaque`, `Transparent`, `Splat`)
- Which `RenderPolicyFlags` apply (`Wireframe`, `AlphaBlend`, `DepthTest`, `DepthWrite`)
- A `ShaderPairAsset` reference (vertex + pixel shader keys)

The `BuiltinRenderProgram` enum drives which PSO the backend ultimately creates.
Four variants currently exist:

| Variant | Domain | Notes |
|---------|--------|-------|
| `MeshWireframeDebug` | `Debug` | IA-driven wireframe; depends on mesh vertex/index buffers |
| `GaussianSplatDebug` | `Splat` | Classic per-splat path |
| `ScalarFieldDebug` | `Debug` | Field-to-geometry debug visualization |
| `GaussianSplatPullDebug` | `Splat` | Pull-based path: no IA, reads t0 `StructuredBuffer<Splat>` at SRV slot 0 |

## Where this asset lives in the graph

`RenderProgramAsset` is wired into the renderable system, not exposed as a
standalone module today. Each `create_*_renderable()` call on `RenderableAssetModule`
implicitly selects the appropriate `BuiltinRenderProgram` and stores it on the
resulting `RenderableAssetData`.

```
kGaussianSplatDebugRenderableSchema
    └─ compiler ──► RenderableAssetData {
           kind    = GaussianSplatCloud
           program = BuiltinRenderProgram::GaussianSplatDebug   ← this
           domain  = RenderDomain::Splat
           policy  = DepthTest | DepthWrite
           source_asset = <splat cloud key>
       }
```

The `render_program` field on `RenderableAssetData` is a `ResourceHandle` that
is invalid by default. Call-site code that resolves a `RenderProgramAsset` and
writes back the handle can override `BuiltinRenderProgram` with a fully resolved
program record at draw time.

## Key Types

All defined in `engine/assets/renderable/renderable.h`:

```cpp
enum class BuiltinRenderProgram : uint8_t {
    MeshWireframeDebug,
    GaussianSplatDebug,
    ScalarFieldDebug,
    GaussianSplatPullDebug,
    Count   // sentinel
};

enum class RenderDomain : uint8_t {
    Debug, Opaque, Transparent, Splat
};

enum RenderPolicyFlags : uint32_t {
    RenderPolicy_None       = 0,
    RenderPolicy_Wireframe  = 1u << 0,
    RenderPolicy_AlphaBlend = 1u << 1,
    RenderPolicy_DepthTest  = 1u << 2,
    RenderPolicy_DepthWrite = 1u << 3,
};

struct RenderableAssetData {
    RenderableKind           kind{};
    wz::asset::AssetKey      source_asset{};
    BuiltinRenderProgram     program{};
    RenderDomain             domain{};
    uint32_t                 policy_flags = RenderPolicy_None;
    wz::asset::ResourceHandle render_program{};  // invalid until explicitly set
    float bounds_min[3]{};
    float bounds_max[3]{};
};
```

## Dependencies

`kBuiltinRenderProgramSchema` carries a `ShaderPairAsset` reference internally
(vertex + pixel `AssetKey` values). Each `BuiltinRenderProgram` resolves to a
specific HLSL shader pair at initialization time via `ShaderAssetModule`.

| Dependency | How required |
|------------|--------------|
| `AssetType::Shader` (vertex) | HLSL VS bytecode for this program |
| `AssetType::Shader` (pixel)  | HLSL PS bytecode for this program |

## Usage Example

Calling code typically goes through `RenderableAssetModule`, which handles the
render program selection implicitly:

```cpp
// Create a Gaussian splat debug renderable.
// The module selects BuiltinRenderProgram::GaussianSplatDebug automatically.
GaussianSplatDebugRenderableDesc desc{
    .name       = "my_splat_cloud_renderable",
    .splat_cloud = splat_cloud_asset,
};
RenderableAsset renderable = renderable_module.create_gaussian_splat_debug(desc);

// After system.compile_pending():
RenderableHandle handle = renderable_module.get_renderable(renderable);
const RenderableAssetData* data = renderable_module.get_renderable_data(handle);

// data->program == BuiltinRenderProgram::GaussianSplatDebug
// data->domain  == RenderDomain::Splat
// data->render_program is invalid until the backend resolves it
```

To override with an explicit resolved program (advanced use):

```cpp
// After resolving a RenderProgramAsset externally:
data_mutable->render_program = resolved_program_handle;
// The submit path will prefer render_program over program when valid.
```

## GPU / Scene-Render Boundary

This asset does **not** cross into scene-render directly. Scene-render receives
draw commands with a `MaterialHandle` and a `MeshHandle` (for geometry) or a
`SplatHandle` and sort indices (for splats). The mapping from
`BuiltinRenderProgram` to a PSO happens in the window-engine submit path, not
in the asset system and not in scene-render.

The scene-render `DrawCommand` structure carries only the data needed to issue
a draw call. The PSO selection belongs to the frame submission layer in
window-engine.

## Notes

- `kBuiltinRenderProgramCount` = `static_cast<size_t>(BuiltinRenderProgram::Count)`.
  The submit path can index a fixed-size PSO table by `BuiltinRenderProgram` value.

- The `GaussianSplatPullDebug` variant is reserved for the pull-based splat path
  (no input assembler, reads per-splat data from t0 `StructuredBuffer<Splat>` at
  SRV slot 0). The sort index buffer is at t1. This is the intended production
  path for the cloud splat pipeline.

- Backend PSO creation is intentionally decoupled: the asset records *what* is
  needed; a separate initialization step uses that record to create the actual
  `ID3D12PipelineState`. This keeps the asset system free of backend headers.
