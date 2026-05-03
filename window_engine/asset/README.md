# wz::asset — Asset System DAG

A content-addressed, schema-driven dependency graph for deterministic asset compilation. Transforms raw filesystem data into compiled GPU resources via an embedded build graph inside the engine.

---

## Concepts

**Asset** — anything that must be compiled before the renderer can use it: a mesh, texture, material, shader, audio clip, etc.

**AssetKey** — the identity of one deterministic build of an asset. It is a composite of four hashes:

```
content_hash   hash of the raw input bytes
schema_hash    hash of the interpretation rules
compiler_hash  hash encoding the compiler version
deps_hash      derived from all transitive dependency keys
```

Changing any component produces a different key. Two assets with the same key are guaranteed to produce the same GPU resource.

**AssetNode** — a vertex in the graph. Holds the key, type, schema, and the payload at its current stage: raw bytes (Source), decoded data (Intermediate), or a live GPU resource (Compiled).

**Compiler** — a pure function `(input node, resolved deps) → compiled node`. No I/O, no hidden state. Registered by `(SchemaID, AssetType)` pair.

**Cache** — a mutable, eviction-based store of `AssetKey → GPUHandle`. Entirely optional: dropping it only forces recompilation; it does not affect correctness.

**GPUHandle** — an opaque reference to a renderer-owned resource. The asset system stores and returns these; it does not interpret them.

---

## Lifecycle

The system has three sequential phases.

```
Registration  →  Commit  →  Runtime
```

Registration and Commit are single-threaded. Runtime (resolve) is currently single-threaded; parallel compilation is a planned extension.

---

## Phase 1 — Registration

Declare every asset node and its direct prerequisites. Order does not matter; the graph builder resolves ordering at commit time.

```cpp
#include <asset/system.h>
using namespace wz::asset;

CompilerRegistry registry;
// ... register compilers (see below) ...

AssetSystem sys(std::move(registry));

AssetKey tex_key  { /* hashes */ };
AssetKey mesh_key { /* hashes */ };

AssetNode tex_node;
tex_node.key    = tex_key;
tex_node.type   = AssetType::Texture;
tex_node.schema = kAlbedoSchema;
tex_node.stage  = AssetStage::Source;
tex_node.payload = load_raw_bytes("hero_albedo.png");

AssetNode mesh_node;
mesh_node.key    = mesh_key;
mesh_node.type   = AssetType::Mesh;
mesh_node.schema = kStaticMeshSchema;
mesh_node.stage  = AssetStage::Source;
mesh_node.payload = load_raw_bytes("hero.obj");

sys.register_asset(tex_node);
sys.register_asset(mesh_node);   // no deps — standalone source asset
```

A node with dependencies lists the keys of its direct prerequisites:

```cpp
AssetKey mat_key { /* hashes */ };

AssetNode mat_node;
mat_node.key    = mat_key;
mat_node.type   = AssetType::Material;
mat_node.schema = kPBRMaterialSchema;
mat_node.stage  = AssetStage::Source;
mat_node.payload = load_raw_bytes("hero.mat");

// This material depends on the texture and the mesh.
sys.register_asset(mat_node, { tex_key, mesh_key });
```

`register_asset` returns `false` if the key is already registered.

---

## Phase 2 — Commit

Freeze the graph. This validates all dependency references, runs cycle detection (via Kahn's algorithm on the underlying DAG), and builds the immutable CSR-backed graph structure.

```cpp
if (!sys.commit()) {
    // Either a dep key was never registered, or a dependency cycle exists.
    // The system remains in the registration phase — inspect and correct.
    report_error("Asset graph is invalid");
    return;
}
```

After a successful commit:
- The graph is immutable.
- `register_asset` must not be called again.
- `resolve` and `resolve_all` become available.

---

## Phase 3 — Runtime

### Demand-driven resolve

Request a single compiled asset by key. Prerequisites are resolved recursively and memoized in the cache before the requested node is compiled, so each node is compiled at most once per cache lifetime.

```cpp
auto result = sys.resolve(mat_key);

if (std::holds_alternative<GPUHandle>(result)) {
    GPUHandle h = std::get<GPUHandle>(result);
    renderer.draw(h);
} else {
    ResolveError err = std::get<ResolveError>(result);
    // handle error (see Error Handling below)
}
```

### Eager batch resolve

Compile every registered asset upfront, in topological order (prerequisites before dependents). Nodes already in the cache are skipped. Useful for load-time baking or offline pipelines.

```cpp
std::vector<std::pair<AssetKey, ResolveError>> errors;
uint32_t compiled = sys.resolve_all(&errors);

if (!errors.empty()) {
    for (auto& [key, err] : errors)
        log_asset_error(key, err);
}
```

`resolve_all` uses the pre-computed topological order from the graph rather than the call stack, so it scales to arbitrarily deep dependency chains.

---

## Registering Compilers

Compilers are registered before constructing `AssetSystem`. Each compiler handles one `(SchemaID, AssetType)` pair.

```cpp
CompilerRegistry registry;

registry.register_compiler(AssetCompiler{
    .input_schema = kAlbedoSchema,
    .output_type  = AssetType::Texture,
    .compile = [&gpu](
        const AssetNode&           input,
        std::span<const AssetNode> dep_nodes,
        std::span<const GPUHandle> dep_handles) -> AssetNode
    {
        // 1. Decode the source bytes into an intermediate format.
        auto& raw  = std::get<std::vector<uint8_t>>(input.payload);
        ImageData img = decode_png(raw);

        // 2. Upload to GPU and get a handle.
        GPUHandle h = gpu.upload_texture(img);

        // 3. Return the compiled node.
        AssetNode out   = input;
        out.stage       = AssetStage::Compiled;
        out.payload     = h;
        return out;
    }
});

AssetSystem sys(std::move(registry));
```

The compiler receives `dep_nodes` and `dep_handles` in the same order as the node's declared prerequisites. A compiler for a node with no prerequisites can ignore both spans.

**The compiler contract:**
- Must be deterministic and side-effect-free.
- Must return a node at `AssetStage::Compiled` with a valid `GPUHandle` as its payload.
- Returning anything else causes `resolve` to return `ResolveError::CompileFailed`.

---

## Cache Management

The cache is accessible directly via `sys.cache()`.

```cpp
// Check for a compiled asset without triggering compilation.
std::optional<GPUHandle> h = sys.cache().lookup(mat_key);

// Soft-invalidate: marks the entry stale without removing the slot.
// The next resolve() call will recompile and repopulate the entry.
// Preferred for hot-reload — the hash table slot is reused.
sys.cache().invalidate(mat_key);

// Hard-evict: removes the entry entirely.
sys.cache().evict(mat_key);

// Drop everything (e.g. on device lost / context reset).
sys.cache().clear();
```

After invalidation, the next `resolve(key)` recompiles the node and restores the cache entry:

```cpp
// Hot-reload cycle for a single texture:
sys.cache().invalidate(tex_key);

auto r = sys.resolve(tex_key);  // recompiles from DAG source data
```

---

## Error Handling

`resolve` returns `Result<GPUHandle>`, which is `std::variant<GPUHandle, ResolveError>`.

| Error | Cause |
|---|---|
| `NodeNotFound` | Key is not in the committed graph, or `resolve` was called before `commit`. |
| `CompilerNotFound` | No compiler is registered for the node's `(SchemaID, AssetType)`. |
| `CompileFailed` | The compiler returned a node that is not at `AssetStage::Compiled`, or returned an invalid `GPUHandle`. |
| `DependencyFailed` | At least one prerequisite failed to resolve. The specific error is on the failing prerequisite. |

When a dependency fails, `DependencyFailed` propagates up the chain. Use `resolve_all` with an error collector to identify all failing nodes in one pass rather than discovering them one at a time.

---

## Scene System Integration

Scene nodes reference assets by `AssetKey` only. They do not own asset data.

```cpp
// In a SceneNode:
AssetKey material_key;

// At render time:
auto r = asset_sys.resolve(material_key);
if (std::holds_alternative<GPUHandle>(r))
    renderer.submit(transform, std::get<GPUHandle>(r));
```

The asset system resolves prerequisites automatically. The scene system never needs to know that a material depends on a texture.

---

## Full Example

```cpp
#include <asset/system.h>
using namespace wz::asset;

void setup_assets(GPUDevice& gpu) {

    // ── Compilers ──────────────────────────────────────────────────────────────

    CompilerRegistry registry;

    registry.register_compiler(AssetCompiler{
        .input_schema = kAlbedoSchema,
        .output_type  = AssetType::Texture,
        .compile = [&](const AssetNode& n, auto, auto) {
            auto& raw = std::get<std::vector<uint8_t>>(n.payload);
            return compiled_node(n, gpu.upload_texture(decode_png(raw)));
        }
    });

    registry.register_compiler(AssetCompiler{
        .input_schema = kStaticMeshSchema,
        .output_type  = AssetType::Mesh,
        .compile = [&](const AssetNode& n, auto, auto) {
            auto& raw = std::get<std::vector<uint8_t>>(n.payload);
            return compiled_node(n, gpu.upload_mesh(parse_obj(raw)));
        }
    });

    registry.register_compiler(AssetCompiler{
        .input_schema = kPBRMaterialSchema,
        .output_type  = AssetType::Material,
        .compile = [&](const AssetNode& n,
                       std::span<const AssetNode> dep_nodes,
                       std::span<const GPUHandle> dep_handles) {
            // dep_handles[0] = albedo texture, dep_handles[1] = mesh
            return compiled_node(n, gpu.create_material(dep_handles));
        }
    });

    // ── Registration ───────────────────────────────────────────────────────────

    AssetSystem sys(std::move(registry));

    AssetKey tex_key  = compute_key("hero_albedo.png", kAlbedoSchema);
    AssetKey mesh_key = compute_key("hero.obj",        kStaticMeshSchema);
    AssetKey mat_key  = compute_key("hero.mat",        kPBRMaterialSchema);

    sys.register_asset(make_source_node(tex_key,  AssetType::Texture,  kAlbedoSchema,     "hero_albedo.png"));
    sys.register_asset(make_source_node(mesh_key, AssetType::Mesh,     kStaticMeshSchema, "hero.obj"));
    sys.register_asset(make_source_node(mat_key,  AssetType::Material, kPBRMaterialSchema,"hero.mat"),
                       { tex_key, mesh_key });

    // ── Commit ─────────────────────────────────────────────────────────────────

    if (!sys.commit()) {
        fatal("Asset graph rejected — cycle or missing dependency");
    }

    // ── Eager bake (load time) ─────────────────────────────────────────────────

    std::vector<std::pair<AssetKey, ResolveError>> errors;
    sys.resolve_all(&errors);
    for (auto& [key, err] : errors)
        log("Asset failed: {}", err);

    // ── Runtime ────────────────────────────────────────────────────────────────

    // Scene nodes hold keys. The renderer calls resolve() per frame (cache hit).
    auto r = sys.resolve(mat_key);
    if (auto* h = std::get_if<GPUHandle>(&r))
        renderer.draw_mesh(*h);
}
```

---

## File Structure

```
wz/asset/
  types.h      — AssetKey, AssetNode, GPUHandle, Hash, AssetType, AssetStage, AssetIR
  dag.h        — AssetGraph type aliases and dependency-vocabulary graph helpers
  compiler.h   — AssetCompiler, CompilerRegistry
  cache.h      — AssetCache
  system.h     — AssetSystem (registration, commit, resolve)
```

`types.h` has no dependency on the graph library and can be included anywhere. The graph library (`wz::core::graph`) is an implementation detail of `dag.h` and `system.h`.


## Performance Tips

### Prefer `resolve_all` at load time

`resolve(key)` is recursive — each call walks prerequisites depth-first via the call stack. For deep dependency chains this is fine at runtime (cache hits short-circuit immediately after the first resolve), but on a cold cache it can produce long call chains and redundant cache lookups at each level.

`resolve_all` avoids this entirely. It iterates the pre-computed topological order stored in the graph, so prerequisites are always already cached by the time their dependents are compiled. No recursion, no redundant lookups:

```cpp
// At load time — compile everything in one linear pass.
sys.resolve_all();

// At runtime — guaranteed cache hit, O(1) per call.
auto r = sys.resolve(mat_key);
```

The practical pattern is: `resolve_all` once at load time (or level transition), then `resolve` freely at runtime.

### Reserve cache capacity upfront

`AssetCache` is backed by `std::unordered_map`. If you know the number of assets ahead of commit, pass that count to the `AssetSystem` constructor to avoid rehashing during `resolve_all`:

```cpp
const uint32_t expected_asset_count = 4096;
AssetSystem sys(std::move(registry), expected_asset_count);
```

### Soft-invalidate instead of evicting on hot-reload

`evict(key)` removes the hash table entry entirely. On the next resolve the slot must be reallocated and reinserted. For hot-reload — where the same set of assets cycle repeatedly through invalidation — prefer `invalidate(key)`, which marks the entry stale in place. The slot is reused on the next `store`, avoiding repeated allocation churn:

```cpp
// Prefer this for hot-reload:
sys.cache().invalidate(key);
sys.resolve(key);               // recompiles, reuses existing slot

// Reserve evict() for assets being permanently unloaded:
sys.cache().evict(key);
```

### Shared dependencies are compiled once

Any asset referenced as a prerequisite by multiple nodes is compiled exactly once per cache lifetime, regardless of how many times it appears in the graph. The cache is checked at the top of every `resolve` call, so a shared texture depended on by fifty materials costs one compilation and fifty lookups.

If you suspect over-compilation, add a counter to your compiler's `compile` lambda during development — a correctly memoized asset should report exactly one invocation between cache clears.

### Clear the cache on device reset, not on level load

The cache stores `GPUHandle` values owned by the renderer. On a GPU device loss or context reset, all handles become invalid and the cache must be fully cleared:

```cpp
gpu.on_device_reset([&]() { sys.cache().clear(); });
```

Level transitions do not require a full clear. Assets shared across levels (common textures, UI materials) remain valid in the cache and will not be recompiled. Only `evict` assets you know are being unloaded.