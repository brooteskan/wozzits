## Renderable V0: Data Asset → RenderableAsset → Prepared Runtime Renderable

# Core pipeline:
Data asset
  MeshAsset
  GaussianSplatCloudAsset
  ScalarFieldAsset

→ RenderableAsset
  backend-agnostic render interpretation:
  kind
  source_asset
  builtin program
  render domain
  policy flags
  bounds

→ RenderableGpuCache
  runtime/backend realization:
  Mesh → GPU mesh handle
  GaussianSplatCloud → GPU splat cloud handle
  ScalarField → GPU texture handle

→ BuiltinRenderProgram
  maps to shader pair asset desc

→ ShaderAssetModule
  compiles HLSL into GPU shader handles

→ renderable_debug_runtime
  prepared renderable + shader handles
  creates existing DX12 debug context

→ submit debug frame

RenderableAsset is not a GPU resource.
RenderableGpuCache is not an asset compiler.
RenderableGpuCache does not own GPU resources; it caches handles.
BuiltinRenderProgram is not a material system.
renderable_debug_runtime is not the final renderer.
Per-frame view state does not belong in RenderableAssetData.

# Renderable V0

Renderable V0 is the first bridge between engine asset data and runtime drawing.

It separates three concerns:

1. **Data assets** describe what the data is.
2. **Renderable assets** describe how that data should participate in rendering.
3. **Runtime realization** uploads or retrieves backend GPU resources and connects them to the current debug renderer.

## Current supported data assets

- `MeshAsset`
- `GaussianSplatCloudAsset`
- `ScalarFieldAsset`

## Current renderable recipes

- `MeshAsset -> MeshWireframeDebug RenderableAsset`
- `GaussianSplatCloudAsset -> GaussianSplatDebug RenderableAsset`
- `ScalarFieldAsset -> ScalarFieldDebug RenderableAsset`

Each recipe emits the shared output type: RenderableAssetData


## Current runtime path:
RenderableAssetData
-> RenderableGpuCache
-> PreparedRenderable
-> BuiltinRenderProgram shader pair lookup
-> ShaderAssetModule
-> renderable_debug_runtime
-> existing DX12 debug context
-> submit debug frame

## Current realizations:
RenderableKind::Mesh
-> upload_mesh(...)
-> GPU mesh handle

RenderableKind::GaussianSplatCloud
-> upload_gaussian_splat_cloud(...)
-> GPU splat-cloud handle

RenderableKind::ScalarField
-> upload_scalar_field_texture(...)
-> GPU texture handle

## Current programs:

tests/window/test_mesh_wireframe.cpp
tests/window/test_gaussian_splat.cpp
tests/window/test_scalar_field.cpp
