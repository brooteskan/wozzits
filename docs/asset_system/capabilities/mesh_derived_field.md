# `MeshDerivedFieldAsset`

> Typed per-element values over a source mesh domain. Mesh-derived fields are
> the current representation for mesh analysis channels, behavior-published
> mesh fields, and compute-derived field outputs.

## Identity

All schemas below produce `kAssetTypeMeshDerivedField` = 154 and store runtime
data in `MeshDerivedFieldTable`.

| Capability | Schema ID |
|------------|-----------|
| Explicit field | `kMeshDerivedFieldExplicitSchema` = `0xF11ECA55E7_000406` |
| Wavelet analysis field | `kMeshWaveletAnalysisSchema` = `0xF11ECA55E7_000407` |
| Behavior field placeholder | `kBehaviorFieldPlaceholderSchema` = `0xF11ECA55E7_000408` |
| Mesh compute derived field | `kMeshComputeDerivedFieldSchema` = `0xF11ECA55E7_000409` |
| Builtin mesh derived field | `kBuiltinMeshDerivedFieldSchema` = `0xF11ECA55E7_00040B` |

## Module API

**Header:** `engine/assets/mesh_derived_field_asset_module.h`
**Class:** `MeshDerivedFieldAssetModule`

```cpp
MeshDerivedFieldAsset create_explicit_field(
    const ExplicitMeshDerivedFieldDesc& desc);

MeshDerivedFieldAsset create_wavelet_analysis(
    const MeshWaveletAnalysisDesc& desc);

MeshDerivedFieldAsset create_behavior_field_placeholder(
    const BehaviorFieldPlaceholderDesc& desc);

MeshDerivedFieldAsset create_compute_derived_field(
    const MeshComputeDerivedFieldDesc& desc);

MeshDerivedFieldAsset create_builtin_field(
    const BuiltinMeshDerivedFieldDesc& desc);

MeshDerivedFieldHandle get_mesh_derived_field(
    const MeshDerivedFieldAsset& asset) const;

const MeshDerivedFieldData* get_mesh_derived_field_data(
    MeshDerivedFieldHandle handle) const;
```

## Runtime Data

`MeshDerivedFieldData` contains:

- `source_mesh_key`
- `source_topology_hash`
- `domain` (`Vertex`, `Edge`, `Face`, `Corner`)
- `element_count`
- channel descriptors with `channel_id`, `value_type`, byte offset/count
- packed channel bytes

Supported value types are:

- `Float1`
- `Float2`
- `Float3`
- `Float4`
- `UInt1`

Channels are stored structure-of-arrays style: all values for one channel are
contiguous, then the next channel follows. Channel ids must be nonzero and
unique within a field.

## Schemas

### Explicit Field

`ExplicitMeshDerivedFieldDesc` stores author/test supplied channel bytes over a
source mesh. It is useful for tests and for bootstrapping known scalar signals.

### Builtin Field

`BuiltinMeshDerivedFieldDesc` stores a compact source recipe over a mesh and
lets the compiler generate a Float1 vertex channel after the source mesh has
resolved. V0 source kinds are:

- `Constant`
- `PositionGradient`
- `VertexIndexGradient`
- `TriangleCornerCount`

This is the asset-backed implementation used by the scene-authored
`MeshDerivedFieldSource` component. The scene component stores authored intent
such as `field_id`, `channel_id`, `source_kind`, `component`, `normalize`, and
`constant_value`; `resolved_field_asset` is materialized editor/cache state and
is not exported as authored JSON.

### Wavelet Analysis

`MeshWaveletAnalysisDesc` computes CPU reference graph-wavelet channels over a
source mesh. It can optionally reference a compute pipeline but remains the
older analysis path; the current sparse-operator trajectory uses
`MeshSparseOperatorAsset` plus behavior GPU compute.

### Behavior Field Placeholder

`BehaviorFieldPlaceholderDesc` creates a zero-filled single-channel field whose
element count is discovered from the source mesh. Behavior GPU jobs then publish
their output buffer into that field/channel as a resident mesh visualization.

### Mesh Compute Derived Field

`MeshComputeDerivedFieldDesc` runs an authored compute pipeline at asset compile
time, reads the output back, and stores the result as a cached
`MeshDerivedFieldData`.

Inputs can currently be mesh positions, normals, UV0, indices, or interleaved
vertices. Positions and indices use `GpuResidentMeshDataTable` when available
and populate it when missing. Float1 output channels are also registered in
`GpuResidentFieldTable` while the output buffer is still on the GPU, so later
rendering or behavior input can reuse the resident channel.

## GPU Realization

`GpuResidentFieldTable` maps `(field_key, channel_id)` to a GPU mesh-field
visualization resource. The current visualization and behavior input paths are
Float1-only.

Current producers:

- mesh compute derived field compile, for Float1 channels
- behavior GPU output publishing through
  `WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION`
- renderable GPU cache upload of field visualization data

Current consumers:

- mesh field visualization rendering
- behavior sparse-operator jobs via
  `WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL`

## Notes

- Mesh-derived fields are values over mesh elements; sparse operators are
  relationships between mesh elements. Keep them separate.
- The behavior field-input resolver currently discovers the field through the
  entity's `mesh_field_visualization_targets` bridge. Explicit authored input
  field binding is a follow-up.
- Vector field behavior input is not implemented yet. ABI 24 names component
  and magnitude modes, but v0 accepts Float1 only.
