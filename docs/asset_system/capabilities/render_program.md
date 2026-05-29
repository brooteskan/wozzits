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
| CPU storage | `RenderProgramTable` |
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
- Vertex and pixel shader asset keys

The `BuiltinRenderProgram` enum drives which PSO the backend ultimately creates.
Eight variants currently exist:

| Variant | Domain | Notes |
|---------|--------|-------|
| `MeshWireframeDebug` | `Debug` | IA-driven wireframe; depends on mesh vertex/index buffers |
| `MeshWireframeDepthDebug` | `Debug` | Depth-tested/depth-writing mesh wireframe debug path |
| `MeshDepthPrepassDebug` | `Debug` | Depth-only mesh prepass path used before some debug rendering |
| `GaussianSplatDebug` | `Splat` | Classic per-splat path |
| `ScalarFieldDebug` | `Debug` | Field-to-geometry debug visualization |
| `GaussianSplatPullDebug` | `Splat` | Pull-based path: no IA, reads t0 `StructuredBuffer<Splat>` at SRV slot 0 |
| `GaussianSplatNeighborhoodColorBlend` | `Splat` | Pull-based + LOD color blend modes (distance/stride/confidence blending) |
| `GaussianSplatTerrainCoverageDebug` | `Splat` | Pull-based + coverage kernel modes (depth-writing terrain surface) |

## Where this asset lives in the graph

`RenderProgramAsset` is exposed through `RenderProgramAssetModule::create_builtin()`.
The module registers a `kBuiltinRenderProgramSchema` node with explicit vertex
and pixel shader dependencies and stores the compiled record in
`RenderProgramTable`.

Renderable assets also carry a `BuiltinRenderProgram` enum directly. Each
`create_*_renderable()` call on `RenderableAssetModule` implicitly selects the
appropriate enum value and stores it on the resulting `RenderableAssetData`.

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
is invalid by default. Call-site code may resolve a `RenderProgramAsset` and
write that handle back to override the enum-only path with a fully resolved
program record at draw time.

## Key Types

All defined in `engine/assets/renderable/renderable.h`:

```cpp
enum class BuiltinRenderProgram : uint8_t {
    MeshWireframeDebug,
    MeshWireframeDepthDebug,
    MeshDepthPrepassDebug,
    GaussianSplatDebug,
    ScalarFieldDebug,
    GaussianSplatPullDebug,
    GaussianSplatNeighborhoodColorBlend,
    GaussianSplatTerrainCoverageDebug,
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
    wz::asset::AssetKey      companion_asset{};  // optional (e.g. ColorLOD)
    BuiltinRenderProgram     program{};
    RenderDomain             domain{};
    uint32_t                 policy_flags = RenderPolicy_None;
    wz::asset::ResourceHandle render_program{};  // invalid until explicitly set
    float bounds_min[3]{};
    float bounds_max[3]{};
};
```

## Dependencies

`kBuiltinRenderProgramSchema` carries vertex and pixel shader `AssetKey`
dependencies through `BuiltinRenderProgramDesc`. Callers typically create those
shader assets via `ShaderAssetModule::create_shader_pair()` or equivalent shader
registration code before creating the render program.

| Dependency | How required |
|------------|--------------|
| `AssetType::Shader` (vertex) | HLSL VS bytecode for this program |
| `AssetType::Shader` (pixel)  | HLSL PS bytecode for this program |

## Usage Example

Renderable creation still works without an explicit `RenderProgramAsset`; the
module stores the built-in enum value directly:

```cpp
// Create a Gaussian splat debug renderable.
// The module selects BuiltinRenderProgram::GaussianSplatDebug automatically.
GaussianSplatDebugRenderableDesc desc{
    .name       = "my_splat_cloud_renderable",
    .splat_cloud = splat_cloud_asset,
};
RenderableAsset renderable = renderable_module.create_gaussian_splat_debug(desc);

// After assets.resolve_all():
RenderableHandle handle = renderable_module.get_renderable(renderable);
const RenderableAssetData* data = renderable_module.get_renderable_data(handle);

// data->program == BuiltinRenderProgram::GaussianSplatDebug
// data->domain  == RenderDomain::Splat
// data->render_program is invalid unless an explicit RenderProgramAsset is attached
```

To override with an explicit resolved program (advanced use):

```cpp
BuiltinRenderProgramDesc program_desc{
    .name = "gaussian_splat_debug_program",
    .program = BuiltinRenderProgram::GaussianSplatDebug,
    .vertex_shader = shader_pair.vertex_shader,
    .pixel_shader = shader_pair.pixel_shader,
};
RenderProgramAsset program = render_programs.create_builtin(program_desc);

// After assets.resolve_all():
auto resolved_program_handle = render_programs.get_render_program(program);
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

- The `GaussianSplatPullDebug` variant is the base pull-based splat path
  (no input assembler, reads per-splat data from t0 `StructuredBuffer<Splat>` at
  SRV slot 0). The sort index buffer is at t1.

- `GaussianSplatNeighborhoodColorBlend` extends the pull path with LOD color
  blend modes. The shader reads packed neighborhood color + confidence from the
  structured buffer and blends toward the neighborhood color based on
  distance, stride, and per-splat confidence. Blend behavior is controlled by
  `SplatColorLODSettings` pushed per-frame.

- `GaussianSplatTerrainCoverageDebug` extends the pull path with terrain
  coverage kernel evaluation (Gaussian, SmoothDisc, PolynomialDisc, HardDisc).
  Writes depth, enabling use as a depth prepass for field accumulation rendering.

- Backend PSO creation is intentionally decoupled: the asset records *what* is
  needed; a separate initialization step uses that record to create the actual
  `ID3D12PipelineState`. This keeps the asset system free of backend headers.
