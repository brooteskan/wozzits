# Mesh Trials Behaviors

Project-local behavior module source for `test_mesh.project.json`.

The scene editor loads compiled behavior DLLs from:

```text
behavior/build/clang-debug
```

Build the behavior DLLs with:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug
```

This behavior module currently registers:

- `gpu_trial` — landscape field compute dispatcher

The `gpu_trial` module responds to `gpu.compute.*` events. On
`WZ_EVENT_GPU_COMPUTE_REQUEST` it submits a `project/landscape_field` compute
job whose output port is marked with `WZ_GPU_RESOURCE_REF_MESH_FIELD_VISUALIZATION`
via `wz_gpu_set_structured_output_mesh_field()`. On `WZ_EVENT_GPU_COMPUTE_COMPLETED`
it re-submits to create a continuous animation loop.

All mesh-shaped values are engine-resolved (ABI 20): the kernel's `positions`
input is bound from the entity's mesh, the output buffer and `vertex_count`
constant are sized/filled from the mesh vertex count, and the dispatch group
count is derived from the kernel's thread group size
(`wz_gpu_set_groups_from_mesh`). No vertex count is authored in behavior
config.

Registered kernel contracts:
- `project/landscape_field`: positions (float3 SRV, engine-bound),
  output (float UAV, published as mesh field), vertex_count (u32,
  engine-filled), phase (f32), frequency (f32), bias (f32).
