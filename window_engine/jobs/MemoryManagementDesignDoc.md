# Wozzits Working Design Document: Memory and Storage Model

## Purpose

This document defines the working memory/storage model for Wozzits Engine. It is intended for implementers who need to decide where data lives, who owns it, when it is destroyed, and how temporary frame products should flow through systems such as jobs, assets, scene compilation, RenderIR construction, and GPU submission.

The core principle is:

> Wozzits should use explicit ownership and deterministic reset/destruction, not garbage collection.

Every object should fall into one of a small number of lifetime/ownership categories. If a type does not clearly fit one category, its storage design is not finished.

---

# 1. Core Rule

Every Wozzits data type must be one of:

1. **Owner**
   Owns memory/resources and destroys or releases them.

2. **View**
   Borrows memory from an owner. Never destroys. Must not outlive the owner.

3. **Handle**
   Refers to a table-owned resource. Never destroys the resource directly.

4. **Builder**
   Temporarily owns mutable construction state. Consumed by a build/commit function.

5. **Scratch / Arena allocation**
   Allocated from a known reset boundary. Invalid after reset.

A type that is "sort of owning" is a design bug waiting to happen.

---

# 2. Naming Convention

Use names that make ownership obvious.

| Suffix                | Meaning                                                |
| --------------------- | ------------------------------------------------------ |
| `*Storage`            | Owns backing memory/resources.                         |
| `*View`               | Non-owning view into storage. Usually spans/pointers.  |
| `*Handle`             | Stable reference into a resource table. Not ownership. |
| `*Builder`            | Mutable construction object, consumed into storage.    |
| `*Template`           | Persistent committed reusable topology/configuration.  |
| `*Execution`          | Per-run mutable state. Reset and reused.               |
| `*Arena` / `*Scratch` | Resettable temporary allocation region.                |

Examples:

```cpp
struct CompiledSceneStorage;  // owns compiled primitive memory
struct CompiledSceneView;     // spans into CompiledSceneStorage
struct RenderIRStorage;       // owns draw-reference memory
struct RenderIRView;          // spans into RenderIRStorage
struct GPUHandle;             // references GPU-owned table slot
struct JobGraphTemplate;      // committed reusable topology
struct FrameExecution;        // mutable per-run scheduler state
```

---

# 3. Lifetime Buckets

## 3.1 Static / Program Lifetime

For constants, schemas, function tables, and other immutable registration data.

Examples:

* Asset type IDs
* Schema hashes
* Compiler registration tables, if globally owned
* Static dispatch tables

Rule:

```text
Static data must be immutable after registration unless explicitly protected.
```

---

## 3.2 Engine / App Lifetime

Created during init, destroyed during shutdown.

Examples:

* `AppContext`
* Window
* GPU device
* Logger
* Asset library
* Persistent scenes
* Committed job templates
* Persistent runtime state such as camera and debug objects

Rule:

```text
Engine/app lifetime objects live from init() to shutdown().
They may own persistent resources.
They must release those resources explicitly during shutdown.
```

---

## 3.3 Resource / Table Lifetime

For resources owned by tables and referenced by handles.

Examples:

* GPU shader table
* GPU texture table
* Pipeline table
* Root signature table
* Asset resource table
* Material/mesh tables

Rule:

```text
Tables own resources.
Handles reference resources.
Handles never release resources directly.
```

A handle should remain valid only while:

1. the owning table/device/library is alive;
2. the slot still exists;
3. the generation/epoch still matches, if generation checking is used.

---

## 3.4 Frame Lifetime

Created or reset once per frame. Destroyed or reset after the frame is complete.

Examples:

* `FrameContext`
* `FrameExecution`
* `ViewData` (in `FrameStorage`)
* `CompiledSceneStorage` (in `FrameStorage`)
* `RenderIRStorage` (in `FrameStorage`)
* `RenderFrameStorage` (in `FrameStorage`)
* Per-frame job payloads

Rule:

```text
Frame products live until the end of the frame or until GPU submission no longer needs them.
They must not be stored as persistent app state unless explicitly promoted.
```

In the current implementation, `FrameStorage` is a persistent member of `GameApp` whose contents are
overwritten each frame. This is intentional: the backing buffers are reused without heap churn
rather than being destroyed and reallocated.

---

## 3.5 Scratch / Phase Lifetime

Temporary memory used only inside one phase.

Examples:

* Temporary sort arrays
* Traversal stacks
* Culling scratch
* Temporary compiler buffers
* Intermediate staging arrays

Rule:

```text
Scratch memory is invalid after the phase reset point.
Never store scratch-backed views outside the phase.
```

**Status:** `ScratchArena` is not yet implemented. Systems that need per-phase temporaries currently
allocate from the heap. Instrumentation (§5.1) must confirm allocation pressure before a scratch
allocator is introduced. See §5.4.

---

## 3.6 Borrowed Lifetime

Non-owning access to something owned elsewhere.

Examples:

* `std::span`
* `*View` types (e.g., `CompiledSceneView`, `RenderIRView`, `RenderFrameView`)
* DAG views
* `void*` job bindings
* Raw pointers in `JobContext`

Rule:

```text
Borrowed data must never outlive its owner.
Borrowed data must never be freed by the borrower.
```

---

# 4. Memory Problems Resolved by Current Design

## 4.1 AppContext Ownership

`AppContext` owns long-lived engine resources: window, GPU device, logger, and asset library.

```text
AppContext init creates engine-session resources.
AppContext shutdown releases them.
```

Engine/app lifetime. First created, last destroyed.

---

## 4.2 Core DAG View vs Storage Split

The core graph library separates:

```text
DAG<N,E>        = view
DAGStorage<N,E> = owner
```

```text
A DAG view must never outlive its DAGStorage.
```

This is the general pattern applied throughout.

---

## 4.3 Asset DAG Destruction

Asset nodes contain non-trivial data (vectors, type-erased payloads, asset IR), so raw byte-buffer
storage is not sufficient. `AssetStorage` wraps raw `DAGStorage` and explicitly destroys nodes.

```text
Asset graphs must be created through asset_build().
Do not hold raw DAGStorage<AssetNode, AssetEdge>.
```

---

## 4.4 JobGraphTemplate Storage

`JobGraphTemplate` stores committed job topology and derived scheduling cache. `JobNode` is currently
trivially destructible, so raw DAG storage is acceptable. A compile-time assertion protects this.

```text
JobGraphTemplate may use raw DAGStorage only while JobNode remains trivially destructible.
If JobNode becomes non-trivial, introduce JobGraphStorage.
```

---

## 4.5 FrameExecution State

`FrameExecution` owns only per-run scheduling state: job status, remaining input counts, borrowed
per-node bindings, and remaining job count. It does not own job payloads.

```text
FrameExecution::bindings are borrowed pointers.
For synchronous execution, stack-allocated frame data is safe if it outlives execute().
```

This rule changes for async jobs. See §5.3.

---

## 4.6 CompiledScene Ownership

Resolved. The scene compilation pipeline uses an explicit Storage/View split with a
caller-provides-storage signature:

```cpp
CompiledSceneView compile(
    CompiledSceneStorage&                 storage,   // caller owns, caller controls lifetime
    const SceneGraph&                     g,
    std::span<const RenderableDescriptor> descs,
    std::span<const LightRecord>          lights,
    const ViewData&                       view);
```

`CompiledSceneStorage` owns the backing buffers (`unique_ptr<byte[]>`). `CompiledSceneView` is a
set of `std::span`s into those buffers. The caller (currently `FrameStorage` via `job_compile_scene`)
controls storage lifetime.

```text
compile() fills caller-provided storage and returns a view into it.
The view must not outlive the storage.
```

---

## 4.7 RenderIR Ownership

Resolved. Same Storage/View pattern and caller-provides-storage signature:

```cpp
RenderIRView build_render_ir(RenderIRStorage& storage, CompiledSceneView scene);
```

`RenderIRView` stores `CompiledSceneView source` **by value** (not by pointer). The spans inside
`source` still alias `CompiledSceneStorage` buffers, so `update_view()` writes into those buffers
are immediately visible to `update_render_ir()`. Storing source by value eliminates the dangling-
pointer risk without changing the correctness model.

```text
RenderIRView::source is a by-value copy of the CompiledSceneView.
Its spans alias CompiledSceneStorage buffers.
Never call update_render_ir() after the CompiledSceneStorage has been reset.
```

---

## 4.8 RenderFrame Ownership

Resolved. Same pattern:

```cpp
RenderFrameView build_frame(
    RenderFrameStorage& storage,
    RenderIRView        ir,
    CompiledSceneView   scene);
```

`RenderFrameStorage` owns the command buffer. `RenderFrameView` is spans into it, passed to the
GPU backend as `submit_render_frame(device, frame.frame)`.

```text
The submitted RenderFrameView must not outlive RenderFrameStorage.
For synchronous submit (current path), FrameStorage outlives submission.
For deferred submit, storage must survive until the backend is done reading it.
```

---

## 4.9 FrameStorage

Resolved. `FrameStorage` is a persistent member of `GameApp` that owns all CPU-side per-frame
products:

```cpp
struct FrameStorage
{
    wz::scene::ViewData             view{};
    wz::scene::CompiledSceneStorage compiled_scene;
    wz::render::RenderIRStorage     render_ir;
    wz::render::RenderFrameStorage  render_frame;
};
```

Each frame, the job graph overwrites these fields in order. Because the storages use `unique_ptr`
for backing memory, the buffers are reused when sizes match. `FrameStorage` lives for the lifetime
of `GameApp` and therefore outlives all jobs and GPU submission in any given frame.

```text
FrameStorage owns all CPU-side per-frame products.
Its contents are overwritten each frame, not destroyed and reallocated.
It must outlive all jobs and GPU submission within the frame.
```

---

## 4.10 Render Preparation Jobification

Resolved. The full render-prep pipeline runs as DAG jobs:

```text
platform_events
    → shutdown_input
        → camera_update
            → job_build_view        (writes FrameStorage::view)
                → job_compile_scene (writes FrameStorage::compiled_scene)
                    → job_build_render_ir    (writes FrameStorage::render_ir)
                        → job_build_render_frame (writes FrameStorage::render_frame)
```

All four render-prep jobs satisfy the job boundary rule (§6.3):
- Inputs borrowed from persistent `GameApp` state (camera, debug_object, scene).
- Outputs written into `FrameStorage`, which is persistent and outlives all jobs.
- Storage outlives all dependent jobs since `GameApp` is persistent.
- Current execution is synchronous; async extension is safe because outputs are in persistent storage,
  not on the stack.

---

# 5. Memory Problems That Still Need Design

These must be resolved in the order listed. Later items have design dependencies on earlier ones.

## 5.1 Instrumentation

**Instrumentation is a design dependency, not a nice-to-have.** The job system provides structure
but not feedback. Before redesigning CPU memory around performance or adding async worker pools,
the engine must be able to answer four questions:

```text
1. Which jobs run?
2. How long does each job take?
3. How much memory does each phase allocate?
4. Which jobs are on the critical path?
```

### 5.1.1 Per-Job Timing

Timing is the first instrumentation target — it is the least invasive and immediately useful.

Proposed design:

```cpp
struct JobTimingRecord
{
    wz::jobs::NodeHandle node;
    const char*          name;
    uint64_t             start_ticks;
    uint64_t             end_ticks;
};

struct FrameJobProfile
{
    uint64_t                      frame_index = 0;
    std::vector<JobTimingRecord>  jobs;

    void reset(uint64_t frame);
};
```

Wrap `run_node()` inside `DagScheduler`:

```cpp
start = now_ticks();
job.run(ctx);
end = now_ticks();
profile->record(node, name, start, end);
```

Target output per frame:

```text
platform_events:      0.04 ms
shutdown_input:       0.00 ms
camera_update:        0.01 ms
build_view:           0.00 ms
compile_scene:        0.12 ms
build_render_ir:      0.03 ms
build_render_frame:   0.02 ms
```

This is the data needed before introducing arenas, worker pools, or job splitting.

### 5.1.2 Allocation Counters

After timing, add per-phase allocation counters. Do not start by replacing `new` globally.

Proposed design:

```cpp
struct StorageStats
{
    uint64_t bytes_allocated_this_frame    = 0;
    uint32_t allocation_count_this_frame   = 0;

    uint64_t compiled_scene_capacity       = 0;
    uint64_t render_ir_capacity            = 0;
    uint64_t render_frame_capacity         = 0;
};
```

The key distinction to confirm:

```text
allocated new backing memory this frame
vs.
reused existing backing storage (size fits within existing buffer)
```

`FrameStorage` is designed so that backing buffers are reused when sizes match. Instrumentation
must confirm this. If `CompiledSceneStorage`, `RenderIRStorage`, or `RenderFrameStorage` reallocate
every frame, the storage layer needs capacity tracking or a grow-only rule before it needs a full
arena.

Track per storage:

```text
stable_buffer bytes (CompiledSceneStorage)
view_buffer bytes (CompiledSceneStorage)
buffer bytes (RenderIRStorage)
buffer bytes (RenderFrameStorage)
reallocations per frame
largest observed capacity
```

Decision rule:

```text
If capacities stabilize after warmup frames, ScratchArena is not yet needed.
If per-frame temp allocations continue growing under load, introduce ScratchArena.
```

### 5.1.3 Job System Benchmarks

Add synthetic benchmarks for the scheduler itself. These do not model the game; they bound the
scheduler's overhead so job-splitting decisions can be made with evidence.

Target benchmark cases:

```text
empty chain:     A → B → C → ... N
wide fanout:     root → 100 independent jobs → join
many tiny jobs:  10,000 no-op jobs
mixed graph:     render-like chain plus independent side jobs
```

Measurements to capture:

```text
scheduler overhead per job
scheduler overhead per edge
reset cost for FrameExecution
commit cost for JobGraphTemplate
```

This matters because if scheduler overhead is ~0.5 µs per job, splitting small phases is free.
If it is ~20 µs per job, tiny jobs become expensive. Do not guess.

---

## 5.2 GPU Resource Table Lifetime

The GPU handle/table ownership concept is established in the design, but the formal lifetime rules
for pipeline state objects, root signatures, descriptor heaps, and device shutdown are unspecified.

The core problem is:

```text
CPU ownership and GPU usage are not synchronized by default.
A CPU table can mark a resource as no longer live,
but the GPU may still be executing commands that reference it.
```

### 5.2.1 Three Resource States

Every GPU table resource moves through three states:

```text
Live
    Valid handle resolves to an active resource.

Retired
    Removed from live use. Handle should no longer resolve as valid.
    Underlying GPU object must not yet be destroyed — GPU may still reference it.

Released
    GPU fence has been reached. Underlying object is actually destroyed.
```

This is the vocabulary used elsewhere in this document: `Handle` refers to table-owned live data,
`Retired` means pending safe destruction, `Released` means actually destroyed.

### 5.2.2 Table Ownership

The device owns the tables:

```cpp
struct DX12Device
{
    ShaderTable        shaders;
    TextureTable       textures;
    BufferTable        buffers;
    PipelineTable      pipelines;
    RootSignatureTable root_signatures;

    DeferredReleaseQueue retired;
};
```

`GPUHandle` never releases anything. Rules:

```text
create_*()             inserts into a table, returns a GPUHandle.
resolve(handle)        looks up a live table slot.
retire(handle, fence)  is a table/device operation — never a handle operation.
GPUHandle destruction  does nothing.
```

### 5.2.3 Slot Structure

```cpp
enum class ResourceState : uint8_t { Empty, Live, Retired };

template<typename T>
struct GPUResourceSlot
{
    T             object{};
    uint32_t      generation   = 0;
    ResourceState state        = ResourceState::Empty;
    uint64_t      retire_fence = 0;
};
```

The handle carries enough to detect stale references:

```cpp
struct GPUHandle
{
    uint32_t       id;
    uint32_t       generation;
    GPUResourceType type;
};
```

This matches the existing rule: a handle is valid only while the owning table is alive, the slot
exists, and the generation matches.

### 5.2.4 DeferredReleaseQueue

```cpp
void TextureTable::retire(GPUHandle h, uint64_t fence)
{
    auto* slot = resolve_live_slot(h);
    if (!slot) return;

    slot->state        = ResourceState::Retired;
    slot->retire_fence = fence;

    retired.push({ .type = GPUResourceType::Texture, .id = h.id,
                   .generation = h.generation, .retire_fence = fence });
}

void DeferredReleaseQueue::collect(uint64_t completed_fence)
{
    for each retired resource:
        if (completed_fence >= retire_fence)
            owning_table.release_slot(resource);
}
```

### 5.2.5 Device Shutdown Rule

```text
Normal runtime deletion may defer release.
Device shutdown must first wait for GPU idle, then release everything.
```

```cpp
void destroy_device(Device& device)
{
    wait_for_gpu_idle(device);
    collect_all_retired_immediately(device);

    destroy_debug_contexts(device);
    destroy_resource_tables(device);
    destroy_swapchain_resources(device);
    destroy_command_objects(device);
    destroy_fence(device);
    release_d3d12_device(device);
}
```

A live `ID3D12Device` at shutdown means something still holds a reference. The table lifetime
design provides the checklist: every table and context must release its owned COM objects before
the device itself is released.

### 5.2.6 GPUOwner Lane

`ExecutionLane::GPUOwner` should eventually mean:

```text
Only the GPU owner lane/thread mutates GPU tables.
Other jobs may request resource creation/retirement,
but table mutation is serialized through the GPUOwner lane.
```

This prevents subtle races once async jobs exist:

```text
Asset/job thread:    request texture creation
GPUOwner:            creates texture, inserts into TextureTable, returns GPUHandle
Render jobs:         read handles (read-only)
GPUOwner:            submits frame, retires/releases resources by fence
```

### 5.2.7 Recommended First Step

Do not implement all GPU tables at once. Specify one resource kind first.

The current debug opaque context owns a root signature, PSO, and vertex buffer that are not
tracked by any table. Define their lifetime explicitly:

```text
Who owns debug opaque root signature?
Who owns debug opaque PSO?
Who owns shader blobs?
Who releases them?
Are they table resources or context-owned resources?
Are they retired by fence or released only after wait-idle?
```

For the current debug context, treat it as device-owned context storage rather than public
handle-table resources:

```cpp
struct DebugOpaqueContextStorage
{
    ID3D12RootSignature* root_sig       = nullptr;
    ID3D12PipelineState* pso            = nullptr;
    ID3D12Resource*      vertex_buffer  = nullptr;

    void destroy_after_gpu_idle();
};
```

Then generalize PSOs and root signatures into tables once the first cleanup is complete and
the pattern is established.

---

## 5.3 Async Job Payload Lifetime

Current job execution is synchronous, so this is safe:

```cpp
AppUpdateFrameData data{};
exec.bind(node, &data);
scheduler.execute(graph, exec);
```

This becomes unsafe when jobs can outlive the stack frame.

```text
Asynchronous jobs may not borrow stack frame data.
They may only borrow persistent data or FrameStorage, which outlives all jobs in a frame.
```

The current render-prep jobs already satisfy this rule because their payloads point into
`FrameStorage` (persistent). No structural change is needed to make them async-safe; the rule
must be enforced as new jobs are added. Instrumentation (§5.1) must show critical-path pressure
before actual async dispatch is introduced.

Potential future pattern for frame-scoped async payloads:

```cpp
auto* data = frame.make<AppUpdateFrameData>();   // arena allocation into FrameStorage
exec.bind(node, data);
scheduler.execute_async(graph, exec);
scheduler.wait();
frame.reset();
```

---

## 5.4 ScratchArena

Some systems need temporary allocation within a phase without per-frame heap churn.

```cpp
struct ScratchArena
{
    void* allocate(size_t bytes, size_t alignment);
    void reset();
};
```

```text
ScratchArena allocations are invalid after reset().
Only temporary phase-local data may live in scratch.
Do not store scratch-backed views in persistent objects.
```

**Not yet implemented.** Instrumentation (§5.1) must measure allocation pressure and confirm
that per-frame churn is real before this is introduced. If `FrameStorage` backing buffers
stabilize in capacity after warmup, this can wait indefinitely.

---

# 6. Storage-System Design Boundary

When implementing a new system, ask this sequence of questions.

## 6.1 Classification Questions

For every new type:

```text
1. Is this an Owner, View, Handle, Builder, Execution object, or Scratch allocation?
2. If it owns memory, where is destruction/reset?
3. If it borrows memory, who owns that memory?
4. If it is a handle, which table owns the resource?
5. If it is frame data, when is it invalidated?
6. If it is async-visible, what guarantees it outlives the job?
```

If these cannot be answered, storage design is incomplete.

---

## 6.2 Function Return Rules

A function returning frame data must use the caller-provides-storage pattern:

```cpp
// Good: caller owns storage, function fills it, returns a view
CompiledSceneView compile(CompiledSceneStorage& storage, ...);
RenderIRView      build_render_ir(RenderIRStorage& storage, CompiledSceneView scene);
RenderFrameView   build_frame(RenderFrameStorage& storage, RenderIRView ir, CompiledSceneView scene);
```

This makes it impossible to accidentally return a view into function-local storage, and gives the
caller full control over storage lifetime.

Bad:

```cpp
RenderIRView build_render_ir(...); // returns spans into local temporaries
```

---

## 6.3 Job Boundary Rule

Before moving a phase into the job graph, verify:

```text
1. Job inputs are borrowed from valid owners.
2. Job outputs are written into explicit storage.
3. Output storage outlives all dependent jobs.
4. Async execution would not invalidate any pointer earlier than expected.
```

The render-prep job chain satisfies all four. Use it as a reference when adding new jobs.

---

# 7. Scene/Render Storage Shape

The scene/render pipeline has settled into this shape:

```text
Persistent SceneStorage       (engine/app lifetime — owned by GameApp::debug_object)
    ↓
CompiledSceneStorage          (frame lifetime — owned by FrameStorage)
CompiledSceneView             (borrowed by RenderIRView::source)
    ↓
RenderIRStorage               (frame lifetime — owned by FrameStorage)
RenderIRView                  (borrowed by job_build_render_frame)
    ↓
RenderFrameStorage            (frame lifetime — owned by FrameStorage)
RenderFrameView               (borrowed by submit_render_frame for duration of submit)
    ↓
GPU backend submit
```

Initial implementation uses `unique_ptr<byte[]>`-backed storage. Later these may be backed by a
frame arena if instrumentation confirms per-frame allocation pressure. Do not introduce arena
complexity before ownership is clear and churn is measured.

---

# 8. Practical Rules for Implementers

## Rule 1: Do not store views in persistent objects unless the owner is also persistent.

Bad:

```cpp
struct GameApp
{
    RenderIRView current_ir; // dangling after FrameStorage reset
};
```

Good — frame data belongs in FrameStorage:

```cpp
struct FrameStorage
{
    RenderIRStorage render_ir;   // owner
    // RenderIRView is returned by build_render_ir and used immediately; not stored
};
```

---

## Rule 2: Stack frame data is safe only for synchronous jobs.

Good (current synchronous path):

```cpp
AppUpdateFrameData data{};
exec.bind(node, &data);
scheduler.execute(graph, exec);
// data still live here
```

Bad for async:

```cpp
AppUpdateFrameData data{};
scheduler.execute_async(graph, exec); // job may outlive data
```

For async jobs, bind into persistent or FrameStorage-backed memory, not the stack.

---

## Rule 3: Handles are not ownership.

Correct mental model:

```text
The GPU texture table owns a texture.
GPUHandle identifies the table slot.
Retiring or releasing the GPUHandle does not destroy the texture — the table does.
```

---

## Rule 4: Raw DAGStorage is only safe for trivial node types.

If a node type owns memory, use a storage wrapper that destroys nodes explicitly (see `AssetStorage`).

---

## Rule 5: Commit/build consumes builders.

Builder types are mutable construction state. Once committed, use the storage/template/view result.
Do not hold a `*Builder` after calling its build function.

---

# 9. Summary Boundary

## Resolved

```text
AppContext ownership
Core DAG view/storage split
AssetStorage destruction rule
JobGraphTemplate committed topology
JobNode trivial-storage guard
FrameExecution synchronous borrowed bindings
GPU handle/table ownership concept
CompiledScene — Storage/View split, caller-provides-storage
RenderIR — Storage/View split, source stored by value in RenderIRView
RenderFrame — Storage/View split, caller-provides-storage
FrameStorage — persistent in GameApp, owns all CPU-side frame products
Render preparation jobification — full pipeline in DAG job graph
```

## Still Requires Design — in priority order

```text
1. Job/frame instrumentation
      Timing first (per-job tick counters via DagScheduler).
      Allocation counters second (FrameStorage buffer reuse vs. reallocation).
      Synthetic job-system benchmarks (scheduler overhead per job/edge).
      Gate: required before ScratchArena, async dispatch, or job splitting.

2. GPU resource table lifetime
      Formal three-state model: Live → Retired → Released.
      Table ownership (ShaderTable, TextureTable, PipelineTable, ...) inside DX12Device.
      GPUResourceSlot generation tracking.
      DeferredReleaseQueue.
      Device shutdown rule: wait_for_gpu_idle → collect_all_retired → destroy tables.
      GPUOwner lane serialization rule.
      First concrete step: debug opaque context cleanup.

3. Async job binding lifetime guard
      Formal rule enforced before actual async dispatch.
      Render-prep jobs are already async-safe; new jobs must follow the same rule.

4. ScratchArena
      Only after instrumentation confirms per-frame heap churn is real.
      Only after frame-data ownership is fully settled.
```

---

# 10. Working Principle

The storage system should make this impossible to misunderstand:

```text
Who owns this memory?
Who may view it?
When is it destroyed or reset?
Can a job outlive it?
Can the GPU outlive it?
```

If the answer is not obvious from the type name and API, the design is not finished.

---

# Addendum: Vocabulary

Avoid the word "garbage." Give every temporary thing a named place and a reset rule.

| Term              | Meaning                                                          |
| ----------------- | ---------------------------------------------------------------- |
| `Storage`         | Owns live data for a named lifetime scope.                       |
| `View`            | Borrows live data; must not outlive storage.                     |
| `Handle`          | Refers to table-owned live data.                                 |
| `Scratch`         | Temporary workspace; invalid after phase reset.                  |
| `FrameStorage`    | This frame's working set; overwritten each frame.                |
| `Retired`         | No longer live, but not yet safe to destroy (GPU still reading). |
| `Released`        | Actually destroyed.                                              |

The `Retired` → `Released` transition is what `DeferredReleaseQueue` manages. It is not garbage
collection — it is deterministic deferred release with a named safety rule (fence value).
