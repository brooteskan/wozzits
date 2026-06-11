# `ComputePipelineAsset`

> CPU-side declaration of a compute shader's binding layout, root constants,
> and thread-group size. It is asset metadata used to create backend compute
> pipelines later; it is not itself a GPU PSO.

## Identity

| Field | Value |
|-------|-------|
| Schema ID | `kComputePipelineSchema` = `0xF11ECA55E7_000102` |
| Output AssetType | `kAssetTypeComputePipeline` = 1050 |
| CPU storage | `ComputePipelineTable` |
| GPU realization | Created on demand by `wz::gpu::create_compute_pipeline()` |

## Module API

**Header:** `engine/assets/compute_pipeline_asset_module.h`
**Class:** `ComputePipelineAssetModule`

```cpp
ComputePipelineAsset create_compute_pipeline(ComputePipelineDesc desc);
wz::asset::ResourceHandle get_compute_pipeline(ComputePipelineAsset asset) const;
const ComputePipelineData* get_compute_pipeline_data(
    wz::asset::ResourceHandle handle) const;
```

## Runtime Data

`ComputePipelineData` contains:

- `name`
- `bindings` (`ComputeBindingDesc` records)
- `root_constant_dwords`
- `thread_group_size_x/y/z`
- resolved compute shader `ResourceHandle`

Bindings describe the shader-visible resource shape:

- `StructuredBufferSRV`
- `StructuredBufferUAV`
- `ByteAddressBufferSRV`
- `ByteAddressBufferUAV`

The semantic field is advisory metadata used by higher-level systems. Current
values are `Unknown`, `MeshVertices`, `MeshIndices`,
`MeshDerivedFieldValues`, and `Scratch`.

## Dependencies

| Dependency | How required |
|------------|--------------|
| `AssetType::Shader` | Compiled compute shader; always required |

## Users

Two current paths consume compute pipeline assets:

- Authored `SceneComputeKernelAsset` materialization creates a compute shader
  and `ComputePipelineAsset`, then `build_kernel_library_from_scene()` realizes
  it into a backend compute pipeline for behavior GPU jobs.
- `MeshComputeDerivedFieldDesc` uses a compute pipeline to generate a cached
  `MeshDerivedFieldData` at asset compile time.

## Notes

- HLSL binding extraction can derive bindings when an authored compute kernel
  omits explicit ports.
- Scene-authored ports remain the source of truth when present.
- The asset records what the compute pipeline requires; the GPU backend owns the
  actual PSO/root-signature resources and their release.
