# Mesh Trials

This project is a small scene-editor sandbox for experimenting with mesh
analysis, GPU compute kernels, and mesh render styles.

## Project-Local Shader Files

Shader paths in this project are authored relative to the project root:

```text
window_engine/resources/projects/mesh_trials
```

For example, the starter render shader files live at:

```text
shaders/render/custom_mesh_vs.hlsl
shaders/render/custom_mesh_ps.hlsl
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

## Custom Render Shader Workflow

The `render_shader` component lets a mesh node author a custom HLSL vertex/pixel
shader pair from scene JSON or from the scene editor UI.

Current first-milestone constraints:

- `binding_model`: `mesh_ia`
- `input_layout`: `mesh_position_normal_uv`
- `blend`: `opaque`
- `depth`: `test_write`
- `raster`: `solid_cull_none`
- Root constants match the existing mesh surface layout at `b0`, with 40
  DWORDs.
- Arbitrary textures, samplers, UAVs, SRVs, and descriptor tables are not yet
  authored through `render_shader`.

Add this to a mesh node, such as `empty_1` / `mesh`, in
`scene_editor_handles_export.scene.json`:

```json
"render_shader": {
  "program_id": "mesh/custom_surface",
  "vertex_hlsl_path": "shaders/render/custom_mesh_vs.hlsl",
  "pixel_hlsl_path": "shaders/render/custom_mesh_ps.hlsl",
  "vertex_entry": "main",
  "pixel_entry": "main",
  "vertex_target": "vs_5_0",
  "pixel_target": "ps_5_0",
  "binding_model": "mesh_ia",
  "input_layout": "mesh_position_normal_uv",
  "blend": "opaque",
  "depth": "test_write",
  "raster": "solid_cull_none"
}
```

The starter pixel shader colors the mesh from UV and normal data. It is a good
place to begin when testing whether a custom render program is being used.

## Scene Editor Render Shader UI

In the sibling `wozzits-imgui` scene editor:

1. Load `test_mesh.project.json`.
2. Select the mesh node (`empty_1`, named `mesh`).
3. Add **Render Shader** from the component browser.
4. In the **Render Shader** panel, set:
   - `Program id`: `mesh/custom_surface`
   - `Vertex HLSL path`: `shaders/render/custom_mesh_vs.hlsl`
   - `Pixel HLSL path`: `shaders/render/custom_mesh_ps.hlsl`
   - `Vertex entry`: `main`
   - `Pixel entry`: `main`
   - `Vertex target`: `vs_5_0`
   - `Pixel target`: `ps_5_0`
5. Keep the render-state dropdowns on their current first-milestone values:
   `mesh_ia`, `mesh_position_normal_uv`, `opaque`, `test_write`,
   `solid_cull_none`.
6. Rebuild/materialize the scene. The editor will compile the project-local HLSL
   files and attach the resulting custom render program to the mesh renderable.
7. Save the project to persist the `render_shader` block.

## Combining Compute And Render Shaders

There are two distinct shader paths in this project:

- `compute_kernel` is for behavior/event-driven HLSL compute work.
- `render_shader` is for replacing the mesh surface vertex/pixel shader pair.

The supported compute-to-render path today is the mesh-derived-field path:

1. A mesh node has `mesh_source`.
2. The same mesh node enables `mesh_wavelet_analysis`.
3. The same mesh node enables `mesh_render_style.field_visualization_enabled`.
4. Materialization creates a compute pipeline and a `MeshDerivedFieldAsset`.
5. The mesh render style asks the renderer to visualize one channel from that
   field, such as detail heat channel `4608`.

That path is what lets a shader show the result of compute work on a mesh today.
The built-in `MeshFieldHeatmap` render program already knows how to bind the
mesh field visualization buffer and color the mesh from it.

A scene node can also carry both `mesh_wavelet_analysis` / field visualization
and `render_shader`; the materializer can build the wavelet field and the custom
render program together. However, the first custom `render_shader` milestone
does not yet expose descriptor bindings, so a custom pixel shader cannot directly
read the generated mesh field buffer. With a custom render shader attached, the
submit path prefers that custom render program over the built-in heatmap program.

To make a custom render shader display compute results directly, the next engine
extension should add descriptor bindings to authored `render_shader`, with a
semantic such as `MeshFieldVisualization`, and then allow an HLSL declaration
like:

```hlsl
StructuredBuffer<float> MeshFieldValues : register(t0);
```

At that point, the intended full loop becomes:

1. Run a compute shader that writes one scalar per mesh vertex.
2. Store or expose that output as a `MeshDerivedFieldAsset` channel.
3. Author a render shader with a `MeshFieldVisualization` SRV binding.
4. In the pixel or vertex shader, read the per-vertex/per-pixel field value and
   map it to color, displacement, opacity, or another material response.

Until that descriptor-binding step exists, use the built-in wavelet heatmap
renderer for compute-result visualization, and use `render_shader` for custom
surface appearance experiments based on mesh vertex attributes and constants.

## Scene Editor UI

The sibling `wozzits-imgui` scene editor exposes the same controls:

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
- Should the mesh use the built-in heatmap renderer, or a custom render shader?
- If using a custom render shader, which HLSL files and entry points should be
  authored?
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

Custom render shaders currently support the mesh surface IA path only. They do
not yet author arbitrary descriptor tables, textures, samplers, or direct
mesh-derived-field SRV bindings. Use built-in mesh field visualization when the
goal is to display compute output on the mesh.
