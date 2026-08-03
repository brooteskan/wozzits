# Mesh Trials

This project is a small scene-editor sandbox for experimenting with mesh
analysis, GPU compute kernels, and mesh render styles.

## Project-Local Shader Files

Shader paths in this project are authored relative to the project root:

```text
window_engine/resources/projects/mesh_trials
```

For example, the project-local compute shaders live at:

```text
shaders/mesh_wavelet/detail_heat_cs.hlsl
shaders/compute/landscape_field_cs.hlsl
```

The scene editor keeps built-in engine shaders rooted at the normal runtime
`resources` folder, but project-authored shader paths are resolved from this
project root when the file exists here. This lets project JSON stay portable
while still using engine built-ins.

## Wavelet Heatmap Workflow

The engine can run the built-in mesh wavelet detail-heat kernel on a mesh and
show the result through mesh render style field visualization.

The current implementation path is:

1. A scene node has a `mesh_source`.
2. The same node enables `mesh_wavelet_analysis`.
3. The same node enables `mesh_render_style.field_visualization_enabled`.
4. During scene materialization, the engine creates the built-in wavelet compute
   pipeline from `window_engine/resources/shaders/mesh_wavelet/detail_heat_cs.hlsl`.
5. The engine creates a `MeshDerivedFieldAsset` for the selected mesh.
6. The styled mesh renderable binds that field to the mesh surface shader.
7. If the GPU wavelet compiler runs, per-channel GPU-resident field buffers are
   kept for rendering, while CPU readback still populates the field data and disk
   cache.

This project now includes project-local compute shaders for the landscape:

- `shaders/mesh_wavelet/detail_heat_cs.hlsl` is the active mesh wavelet shader.
  It overrides the engine default because the materializer requests that path
  relative to the project root.
- `shaders/mesh_wavelet/slope_bands_cs.hlsl` is an experimental slope-band
  variant. To try it today, copy its contents over `detail_heat_cs.hlsl`, or add
  a future materializer selector for project-local wavelet functions.
- `shaders/compute/debug_multiply_u32_cs.hlsl` backs the standalone debug
  compute-kernel node in the scene.
- `shaders/compute/landscape_field_cs.hlsl` is the behavior-driven landscape
  field generator. It reads the engine-bound mesh vertex positions, computes a
  noise + elevation field in mesh-local space, and publishes the output as a
  mesh field visualization through the `#145` behavior compute bridge. The
  behavior module re-submits each frame with an advancing phase, animating the
  field values over time.

## Scene JSON Knobs

Add these objects to a mesh node in
`scene_editor_handles_export.scene.json`:

```json
"mesh_wavelet_analysis": {
  "enabled": true,
  "function": "builtin_detail_heat_v0",
  "scale_count": 4,
  "lambda_max_estimate": 8.0,
  "gamma": 0.75
},
"mesh_render_style": {
  "field_visualization_enabled": true,
  "field_visualization_channel_id": 4608,
  "field_visualization_value_min": 0.0,
  "field_visualization_value_max": 1.0,
  "field_visualization_gamma": 0.75
}
```

Accepted wavelet function names:

- `builtin_detail_heat_v0`
- `gpu_detail_heat_v0`
- `wavelet_heatmap_v0`

They currently map to the same built-in detail-heat implementation.

## Channel IDs

Wavelet output channels are vertex-domain scalar fields:

- Detail heat: `4608` (`0x1200`)
- Position energy at scale `N`: `4096 + N` (`0x1000 + N`)
- Normal energy at scale `N`: `4352 + N` (`0x1100 + N`)

For a first experiment, use channel `4608`.

## Derived Field Recipe Projects

Phase 4 of the scene-authored derived-field sequence is available as:

```text
triangle_corner_count_field.project.json
```

Load it in the scene editor to inspect a procedural cube whose
`mesh_derived_field_source` uses `source_kind: "triangle_corner_count"` and
publishes Float1 channel `8195` (`0x2003`). The mesh render style visualizes
`field:triangle_corner_count`, so vertices referenced by more triangle corners
render higher in the heatmap than vertices referenced by fewer corners.

## Compute To Render Path

The supported compute-to-render path is the mesh-derived-field path:

1. A mesh node has `mesh_source`.
2. The same mesh node enables `mesh_wavelet_analysis`.
3. The same mesh node enables `mesh_render_style.field_visualization_enabled`.
4. Materialization creates a compute pipeline and a `MeshDerivedFieldAsset`.
5. The mesh render style asks the renderer to visualize one channel from that
   field, such as detail heat channel `4608`.

That path is what lets a shader show the result of compute work on a mesh.

## Behavior Compute Field Path

The mesh node now demonstrates the `#145` behavior compute bridge end-to-end.
The `gpu_trial` behavior module submits the `project/landscape_field` compute
kernel each frame, and the executor publishes its output as the mesh field
visualization buffer. This replaces the static wavelet field with animated
behavior-driven data at runtime.

### How it works

1. Because the mesh node carries a `compute_kernel` and enables
   `mesh_render_style.field_visualization_enabled`, materialization creates a
   behavior-field placeholder `MeshDerivedFieldAsset` (vertex domain, Float1
   channel, element count discovered from the mesh). Authoring
   `mesh_wavelet_analysis` with `enabled: true` instead seeds the field from
   the wavelet analysis.
2. The materialized render style registers a
   `MeshFieldVisualizationTargetComponent` on the mesh entity in the scene
   instance, and renderable realization registers the field's GPU buffer in
   the resident field table — the same buffer the behavior updates in place.
3. The `event_trigger` fires `gpu.compute.request`, which the `gpu_trial`
   behavior receives.
4. The behavior submits a `project/landscape_field` compute job: the engine
   binds the mesh vertex positions, sizes the output, and fills the
   vertex-count constant (see Configuration below).
5. The executor dispatches the compute shader, then refreshes the resident
   mesh field in place from the output buffer
   (`update_mesh_field_visualization_from_gpu_source()`), keeping the handle
   bound by renderables valid.
6. On `WZ_EVENT_GPU_COMPUTE_COMPLETED`, the behavior re-submits for the next
   frame, creating a continuous animation loop.
7. The mesh render style reads the behavior-produced field each frame.

### Configuration

No vertex count is authored anywhere. The plugin declares mesh-resolved ports
(ABI 20+) and the engine fills everything from the entity's mesh:

- `wz_gpu_set_structured_input_mesh_positions(&job, "positions")` binds the
  mesh vertex positions (float3, stride 12) at the port's SRV register.
- `wz_gpu_set_structured_output_mesh_field(&job, "output", 0u, 0u)` sizes the
  output from the target field's element count (the mesh vertex count).
- `wz_gpu_set_u32_mesh_vertex_count(&job, "vertex_count")` fills the root
  constant with the mesh vertex count.
- `wz_gpu_set_structured_input_mesh_indices(&job, "indices")` (ABI 21) binds
  the mesh index buffer (uint, stride 4, triangle list) at the port's SRV
  register.
- `wz_gpu_set_u32_mesh_triangle_count(&job, "triangle_count")` (ABI 21) fills
  the root constant with the mesh triangle count (index count / 3).
- `wz_gpu_set_dispatch_domain(&job, WZ_GPU_DISPATCH_DOMAIN_VERTEX)` (ABI 22)
  declares the iteration domain and derives the dispatch group count from
  the kernel's authored thread group size. `wz_gpu_set_groups_from_mesh(&job)`
  is the legacy alias for the AUTO domain; topology ports (indices,
  triangle count) do not feed AUTO, so kernels iterating triangles declare
  `WZ_GPU_DISPATCH_DOMAIN_FACE` explicitly.

If anything cannot be resolved (no mesh field visualization target on the
entity, element count mismatch, ...), the failure reason is reported in the
dispatch report's `publish_failures` and the previously resident field remains
visible.

### Laplacian residual (ABI 23)

`laplacian_residual_cs.hlsl` consumes the prebuilt sparse mesh operator
(`MeshSparseOperatorAsset`, uniform vertex Laplacian) instead of procedural
noise — real detail derived from real mesh structure. The operator must be
compiled for the entity's mesh (create it via
`mesh_sparse_operators().create_sparse_operator` during scene setup); the
plugin then binds everything with engine-resolved ports:

- `wz_gpu_set_structured_input_sparse_operator(&job, "row_offsets", WZ_GPU_SPARSE_OPERATOR_ROW_OFFSETS)`
  and likewise for `col_indices`, `weights`, and `vertex_mass`. The CSR
  buffers are GPU-resident: uploaded once per operator, reused every
  dispatch.
- `wz_gpu_set_u32_sparse_operator_info(&job, "info")` fills
  `{row_count, nonzero_count, kind, value_convention}` so the kernel can
  validate the operator it consumes.
- `wz_gpu_set_dispatch_domain(&job, WZ_GPU_DISPATCH_DOMAIN_VERTEX)` — the
  apply is a per-vertex CSR traversal; the CSR arrays are much larger than
  the iteration domain, so AUTO must not be used here.

Weight convention (`NeighborWeights`): rows store neighbor weights only and
sum to 1, so the kernel computes `residual[i] = x[i] - Σ_j w_ij x[j]` and
outputs zero for rows with no neighbors (isolated vertices are not detail).

### Compute shader design

`landscape_field_cs.hlsl` reads the engine-bound vertex positions and computes
a multi-octave noise field in mesh-local XZ space, blended with a vertex
elevation term so height reads through the color ramp. `frequency` tunes the
feature size relative to the mesh's local units; `phase` animates the noise
drift frame to frame.

## Render Functions Status

The render-related scene functions are current as follows:

- `mesh_render_style.field_visualization_enabled` controls whether the
  mesh-derived field visualization buffer is generated and attached to the
  renderable.
- `mesh_wavelet_analysis` is the built-in path that produces the mesh-derived
  field metadata and initial data. It is optional for the behavior bridge: a
  node with a `compute_kernel` and field visualization enabled gets a
  behavior-field placeholder `MeshDerivedFieldAsset` automatically, with the
  element count discovered from the mesh.
- `compute_kernel` + `behaviors` on the mesh node use the `#145` behavior
  compute bridge to publish per-vertex field data each frame. The behavior
  output refreshes the resident field buffer in place, so the handle bound by
  renderables stays valid frame to frame.

## Scene Editor UI

The scene editor exposes the same controls (this used to name the retired wozzits-imgui editor):

1. Select a mesh node.
2. Add or expand **Mesh Wavelet Analysis**.
3. Enable the analysis and tune **Scales**, **Lambda max**, and **Gamma**.
4. Add or expand **Mesh Render Style**.
5. Enable **Wavelet heatmap**.
6. Set **Heat channel** to `4608` for detail heat.
7. Rebuild/materialize the scene so the field asset is generated and assigned.

If the editor says no resolved mesh field asset exists yet, rebuild or
materialize the scene.

## Good Bot Guidance

A chat bot helping with this project should ask:

- Which mesh node should be analyzed?
- Should the experiment inspect detail heat, position energy, or normal energy?
- Which scale should be visualized for position or normal energy?
- What value range should be mapped to the color ramp?
- Should the scene use the placeholder mesh or a GLB mesh source?
- After edits, should the bot run focused asset/render tests or launch the scene
  editor?

Good starting presets:

- Detail overview: channel `4608`, `scale_count = 4`, `lambda_max_estimate = 8.0`,
  `gamma = 0.75`, value range `0.0` to `1.0`.
- Fine local detail: channel `4608`, `scale_count = 6`, lower value max such as
  `0.25`.
- Position scale probe: channel `4096`, then try `4097`, `4098`, etc.
- Normal scale probe: channel `4352`, then try `4353`, `4354`, etc.

## Current Limitations

Only the built-in detail-heat wavelet function is exposed today. To experiment
with different wavelet families, add a new compute shader/pipeline and extend
the scene wavelet analysis function enum/parser/materialization path so the
scene JSON can select it.

Per-node authoring of a custom vertex/pixel pair from scene JSON was removed
with the `render_shader` component. Author a custom render program as a 0x70A
custom renderable in the asset graph instead; a CPU mesh can feed its geometry
port. Use built-in mesh field visualization for fast inspection.
