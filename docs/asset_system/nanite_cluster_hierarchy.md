# Nanite-like Cluster Hierarchy Research

Issue: [#131](https://github.com/woguls/wozzits-window-engine/issues/131)

This is an architecture note, not an implementation plan for the current
chunked terrain LOD path. The purpose is to describe what a reusable
meshlet/cluster hierarchy would look like in wozzits-window-engine, what it
would cost, and what must be true before building it.

## Summary

A Nanite-like renderer is a long-term replacement for chunked mesh LOD, not a
small extension of the current terrain selector. The core asset should be
mesh-independent: terrain, imported meshes, and generated meshes should all be
able to compile into the same cluster hierarchy format.

The recommended path is:

1. Keep the current chunked terrain LOD system as the production path.
2. Add an offline cluster-hierarchy asset only after chunked mesh LOD, seam
   transitions, and projected-error selection are stable and benchmarked.
3. Prototype CPU selection first, producing normal indexed mesh draw commands.
4. Move to GPU-driven selection and indirect draw only after CPU selection and
   offline validation prove that the hierarchy is correct.

The main reason is validation. A broken cluster hierarchy can create holes,
overlap, unstable popping, or conservative-error failures that are hard to see
from the renderer. The offline verifier is part of the architecture, not a nice
extra.

## Current Engine Boundary

The current terrain render proxy is `TerrainVisualProxyData`. It is CPU-side
metadata for terrain chunk records, per-chunk LOD records, bounds, conservative
error, seam/boundary metadata, transition strips, and representation IDs. GPU
buffers remain renderer-owned. That separation is still the right boundary.

A cluster hierarchy should either be:

- a sibling CPU asset type such as `MeshClusterHierarchyData`, referenced by
  renderables and terrain visual proxies, or
- an optional extension field inside `TerrainVisualProxyData` only when the
  source is terrain-specific.

The first option is preferred. The issue explicitly requires the hierarchy to be
terrain-independent, and imported mesh LOD is the other natural consumer.

## Proposed Data Structures

The hierarchy should be stored as immutable CPU asset data with stable IDs. GPU
upload should be a separate realization step, just like the current terrain
visual proxy and render-resource caches.

```cpp
struct MeshClusterId {
    uint32_t value;
};

struct MeshClusterGroupId {
    uint32_t value;
};

struct MeshClusterBounds {
    float sphere_center[3];
    float sphere_radius;
    float cone_axis[3];
    float cone_cutoff;
    float aabb_min[3];
    float aabb_max[3];
};

struct MeshClusterRecord {
    MeshClusterId cluster_id;
    MeshClusterGroupId group_id;
    uint32_t first_vertex;
    uint32_t vertex_count;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t first_child;
    uint32_t child_count;
    uint32_t first_parent;
    uint32_t parent_count;
    float geometric_error;
    float parent_error;
    MeshClusterBounds bounds;
};

struct MeshClusterHierarchyData {
    uint32_t schema_version;
    uint32_t compiler_version;
    wz::asset::AssetKey source_asset_key;
    std::vector<PackedClusterVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshClusterRecord> clusters;
    std::vector<uint32_t> child_cluster_indices;
    std::vector<uint32_t> parent_cluster_indices;
    std::vector<MeshClusterGroupRecord> groups;
};
```

The exact packed vertex format can vary by source, but the first prototype
should keep it conservative:

| Field | Format | Bytes |
|---|---:|---:|
| Position | 3 x float | 12 |
| Normal | 10:10:10:2 or 3 x float in V0 | 4 or 12 |
| UV0/material payload | optional | 0-8 |

For V0, use ordinary float position and normal if that makes validation easier.
Compression is a later optimization.

## Size Estimates

Use clusters of roughly 64 to 128 triangles. With 128 triangles and ordinary
indexed triangles:

| Item | Estimate |
|---|---:|
| Cluster indices | 384 indices x 4 bytes = 1536 bytes |
| Cluster vertices | about 80-160 vertices x 24-32 bytes = 2-5 KB |
| Cluster metadata | about 96-144 bytes |
| Parent/child refs | about 16-64 bytes |

For a 1 million triangle mesh:

| Quantity | 64 tri clusters | 128 tri clusters |
|---|---:|---:|
| Leaf clusters | about 15625 | about 7813 |
| Metadata only | about 1.5-2.3 MB | about 0.8-1.2 MB |
| Index data | about 6 MB | about 12 MB if duplicated per cluster |
| Vertex data | source-dependent | source-dependent |

The dangerous cost is duplicated vertices across cluster boundaries and across
hierarchy levels. The compiler must report duplication ratio as a first-class
stat. If a cluster hierarchy doubles or triples terrain vertex memory before it
even reaches the GPU, it will be hard to justify for this engine's current
terrain scenes.

## Hierarchy Construction

Construction is offline and deterministic. It should run as an asset compiler,
with disk caching keyed by source asset identity and compiler settings.

V0 construction algorithm:

1. Normalize source mesh triangles into a common temporary mesh.
2. Partition triangles into leaf clusters using spatial locality and target
   triangle count.
3. Build adjacency between leaf clusters from shared source edges.
4. Group nearby clusters into parent groups.
5. Simplify each group into parent clusters with conservative geometric error.
6. Repeat until a small root set remains.
7. Emit parent/child links as a DAG, not a strict tree.
8. Run offline validation and store a validation summary with the asset.

Partitioning should begin spatially, not purely by triangle count. Terrain has
large regular surfaces, while imported meshes may have disconnected components.
A good first heuristic is:

- split disconnected components first,
- bin triangles by Morton code of triangle centroid,
- grow clusters by adjacency until the target triangle count is reached,
- prevent clusters from spanning material, UV, or hard-normal boundaries unless
  the source explicitly permits it.

Error metrics must be conservative. A parent cluster records the maximum
distance from the original covered surface to the simplified parent surface,
not just a local decimation residual. If that cannot be computed robustly, the
compiler should reject the asset or mark it unsuitable for hierarchy rendering.

## Selection Algorithm

The selection result is a cut through the DAG: every source surface region is
covered by exactly one selected cluster. That invariant is the crack policy.
Selection is not "pick any clusters that pass an error test"; it must preserve
coverage.

### CPU Prototype

The CPU prototype should:

1. Start from root clusters.
2. Frustum-cull bounds.
3. Convert each cluster error to projected pixels.
4. Refine a selected cluster when projected error exceeds the threshold.
5. Stop when children are unavailable, error is acceptable, or budget is hit.
6. Emit selected cluster IDs into a compact draw list.
7. Build normal indexed draw commands, grouped by material/render program.

This does not need GPU compute or indirect draw. It can reuse the current
render-frame model if cluster GPU resources are realized as mesh-like buffers.

The CPU selector should record:

- input clusters visited,
- selected clusters,
- selected triangles,
- rejected clusters by frustum,
- rejected clusters by projected error/budget,
- max selected projected error,
- hierarchy cut validation status.

### GPU Selection

GPU selection becomes interesting only after CPU selection is correct. The GPU
path needs:

- cluster metadata buffer,
- parent/child adjacency buffers,
- selected-cluster append/consume buffers,
- indirect draw argument buffer,
- compute shader for traversal/refinement,
- UAV barriers,
- `ExecuteIndirect` or equivalent backend API,
- readback-free diagnostics counters.

The engine currently has DX12 graphics draw submission, structured-buffer SRV
binding, UAV descriptor support in the data-driven pipeline vocabulary, and
terrain mesh/splat submission paths. It does not yet have a general compute
dispatch abstraction or indirect draw execution path. Those are prerequisites
for the real GPU-driven version.

## Rendering Pipeline Changes

The CPU prototype can be staged with normal draw calls:

1. `MeshClusterHierarchyData` asset compiles CPU hierarchy.
2. A render-resource realization uploads shared vertex/index buffers and cluster
   ranges.
3. Scene compilation emits a cluster hierarchy instance.
4. View update selects cluster IDs.
5. Render IR groups selected clusters by material/program.
6. DX12 submit draws selected cluster ranges.

The GPU-driven path requires a new render prep phase:

```text
build_view
  -> compile_scene/update_view
  -> build_cluster_selection_inputs
  -> dispatch_cluster_selection
  -> build/execute_indirect_cluster_draws
```

This is a render path, not an asset compiler path. The asset compiler produces a
valid hierarchy; the renderer chooses a cut through it for the active view.

## Crack And Transition Handling

Chunked mesh LOD currently needs seam policy because neighboring chunks can
select different LODs independently. A Nanite-like hierarchy should avoid that
class of seam by selecting a valid DAG cut. Parent and child clusters must cover
the same source surface region. A selected parent replaces all of its children;
selected children replace the parent. There is no open boundary between a parent
and child if the cut invariant holds.

This means the hierarchy approach should replace chunk seam stitching for
surfaces rendered through the hierarchy. It can coexist with chunked terrain in
the engine, but a single rendered surface should not mix "chunk seam strips" and
"cluster DAG cut" policies unless there is a deliberate boundary between them.

The hard cases are:

- material boundaries,
- disconnected components,
- terrain skirts or artificial borders,
- non-manifold source meshes,
- duplicate vertices that occupy the same position but have different normals
  or UVs.

These must be handled by construction and validation, not patched in the pixel
shader.

## Offline Validation

The validation tool is a required deliverable before rendering. It should accept
`MeshClusterHierarchyData` and source mesh data, then report:

| Check | Purpose |
|---|---|
| DAG validity | no cycles, all parent/child refs valid |
| Root coverage | every leaf/source region reaches at least one root |
| Cut coverage | representative cuts cover every leaf exactly once |
| No overlap | sibling clusters do not double-own the same source triangle |
| Conservative error | parent error bounds child/source deviation |
| Bounds containment | cluster bounds contain all cluster geometry |
| Material compatibility | clusters do not merge incompatible materials |
| Degenerate geometry | no zero-area triangles unless explicitly allowed |
| Determinism | rebuild from same inputs produces byte-identical metadata |

The first validation target should be CPU-only and run in tests against small
fixtures. Later, add an optional visual debug exporter that writes selected cuts
or failed clusters as colored mesh output.

## Hardware Floor

CPU selection with normal indexed draws:

- Direct3D 12 graphics queue
- vertex/index buffers
- constant buffers and SRV descriptors
- no compute requirement
- no indirect draw requirement

GPU selection:

- Direct3D 12 feature level 12_0 as a practical floor
- compute shaders
- UAV buffers and UAV barriers
- append/consume or counter buffers
- `ExecuteIndirect` support
- shader-visible CBV/SRV/UAV heap space for cluster metadata

Mesh shaders are not a requirement for the first GPU path. They may become
useful later, but requiring them would raise the hardware floor too far for an
initial engine architecture.

## Comparison With Chunked Mesh LOD

| Area | Chunked mesh LOD | Cluster hierarchy |
|---|---|---|
| Implementation complexity | moderate | high |
| Current engine fit | already in progress | future infrastructure |
| Validation burden | chunk bounds, seams, errors | DAG coverage, errors, cuts |
| CPU selection | simple | more complex but feasible |
| GPU-driven selection | optional | central long-term value |
| Crack handling | transition strips or constrained LOD delta | valid DAG cut |
| Asset memory risk | predictable per chunk/LOD | duplication can be high |
| Best use | terrain now | massive meshes/terrain later |

The hierarchy is worth its complexity when:

- chunked LOD cannot keep draw count or triangle count under control,
- visible terrain requires finer granularity than chunk LOD can provide,
- CPU selection becomes a bottleneck,
- imported static meshes need the same solution,
- GPU-driven selection infrastructure already exists for other reasons.

It is not worth building while the current bottlenecks are still shader quality,
terrain material representation, disk cache behavior, or basic chunk selection.

## Go/No-Go Recommendation

Recommendation: no-go for implementation right now; go for research and
validation design only.

Prerequisites before implementation:

1. Chunked mesh multi-LOD is stable.
2. Seam/transition handling is stable and benchmarked.
3. Projected-error selection has trustworthy metrics.
4. Terrain render path has a reliable mesh fallback and known performance
   envelope.
5. A small offline verifier exists for cluster coverage and conservative error.
6. The engine has either a CPU cluster draw prototype target or a planned
   compute/indirect-draw abstraction.

First implementation issue, when ready:

1. Add `MeshClusterHierarchyData` CPU structs and fixtures.
2. Add an offline validator with tiny hand-authored hierarchies.
3. Add a naive CPU builder for small meshes.
4. Add CPU selection tests that prove valid DAG cuts.
5. Only then add render integration.

That order keeps the experiment honest: if the hierarchy cannot prove coverage,
error, and determinism offline, it should not reach the renderer.
