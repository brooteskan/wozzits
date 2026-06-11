# `MeshSparseOperatorAsset`

> CSR sparse operator over a source mesh domain. It lets compute consumers reuse
> a compiled mesh relationship/operator instead of rediscovering topology each
> dispatch.

## Identity

| Field | Value |
|-------|-------|
| Schema ID | `kMeshSparseOperatorSchema` = `0xF11ECA55E7_00040A` |
| Output AssetType | `kAssetTypeMeshSparseOperator` = 157 |
| CPU storage | `MeshSparseOperatorTable` |
| GPU realization | Resident CSR buffers in `GpuResidentSparseOperatorTable` |

## Module API

**Header:** `engine/assets/mesh_sparse_operator_asset_module.h`
**Class:** `MeshSparseOperatorAssetModule`

```cpp
MeshSparseOperatorAsset create_sparse_operator(
    const MeshSparseOperatorDesc& desc);

MeshSparseOperatorHandle get_sparse_operator(
    const MeshSparseOperatorAsset& asset) const;

const MeshSparseOperatorData* get_sparse_operator_data(
    MeshSparseOperatorHandle handle) const;
```

## Runtime Data

`MeshSparseOperatorData` contains:

- `source_mesh_key`
- `source_topology_hash`
- `kind`
- `domain`
- `value_convention`
- `row_count`
- `nonzero_count`
- CSR arrays: `row_offsets`, `col_indices`, `weights`
- optional companion `vertex_mass`

The data model uses the same domain vocabulary as `MeshDerivedFieldData`.

## Implemented Kind

The current kind is `UniformVertexLaplacian` over the vertex domain.

It compiles a sorted unique directed adjacency graph from mesh triangles. Each
row stores neighbor weights only, with weight `1 / degree(i)` for connected
rows. Isolated rows have no nonzeros.

The value convention is `NeighborWeights`:

```text
smooth[i] = sum_j w_ij f[j]
Lf[i]     = f[i] - smooth[i]
```

For `UniformVertexLaplacian`, `vertex_mass` is present and all 1.

Not implemented yet:

- cotangent / mass-weighted Laplace-Beltrami operators
- full-matrix-entry operators with explicit diagonals
- face, edge, corner, or cluster-domain operators

## GPU Residency

Behavior GPU jobs bind sparse operator components through:

- `WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR`
- component selectors:
  - `WZ_GPU_SPARSE_OPERATOR_ROW_OFFSETS`
  - `WZ_GPU_SPARSE_OPERATOR_COL_INDICES`
  - `WZ_GPU_SPARSE_OPERATOR_WEIGHTS`
  - `WZ_GPU_SPARSE_OPERATOR_VERTEX_MASS`
- `WZ_GPU_RESOURCE_REF_MESH_SPARSE_OPERATOR_INFO`

The executor uploads each CSR component once into
`GpuResidentSparseOperatorTable`, keyed by operator asset key, and reuses those
GPU buffers across dispatches.

CSR buffer sizes deliberately do not drive AUTO dispatch sizing. Sparse-operator
consumers should declare `WZ_GPU_DISPATCH_DOMAIN_VERTEX` for the current v0
operator.

## Current Consumer Path

ABI 24 supports:

```text
MeshSparseOperatorAsset
  + MeshDerivedField Float1 channel
  -> GPU CSR residual apply
  -> output MeshDerivedField channel
```

The field input is declared with
`WZ_GPU_RESOURCE_REF_MESH_DERIVED_FIELD_CHANNEL` or helper
`wz_gpu_set_structured_input_mesh_field_signal(...)`.

The v0 signal descriptor is:

```text
source_kind: MeshDerivedField
channel_id: uint32
value_type: Float1
component_mode: all | x
apply_mode: Residual
```

Validation rejects missing operators, source mesh mismatches, non-vertex
operator/field domains, row/element-count mismatches, missing channels, and
non-Float1 channels.

## Notes

- Consumer kernels must handle isolated rows as zero detail. Without that guard,
  a `NeighborWeights` residual would treat an isolated vertex as maximal detail.
- The current resolver still reaches the source mesh/operator through the
  entity's mesh-field visualization target. Direct authored operator/input field
  binding is a follow-up.
- Diffusion, ping-pong smoothing, and scale-band output should build on this
  path rather than changing the v0 binding contract.
