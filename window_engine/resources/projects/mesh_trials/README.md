# Mesh Trials

This project is a small scene-editor sandbox for experimenting with mesh
analysis, GPU compute kernels, and mesh render styles.

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
