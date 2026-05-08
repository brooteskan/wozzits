````markdown
# Wozzits Engine Assets

This directory contains the engine-level asset layer for Wozzits.

The goal of this layer is to provide a consistent way to describe, register, compile, resolve, and retrieve engine assets such as shaders, scalar fields, textures, meshes, Gaussian splat clouds, and future asset families.

The asset system is intentionally recipe-driven:

```text
file/procedural recipe
    → AssetNode
        → compiler
            → ResourceHandle
                → runtime table or GPU-owned resource
````

The generic `wz::asset::AssetSystem` owns the dependency graph, compiler dispatch, cache, and compiled-node records. Engine-specific asset data is owned outside the generic system, usually by tables owned by `EngineAssetLibrary`.

---

## Core concepts

### AssetKey

Every asset node is identified by an `AssetKey`.

For engine assets, keys are built from:

```text
content_hash   — recipe parameters or source path identity
schema_hash    — compiler recipe/schema ID
compiler_hash  — compiler version token
deps_hash      — upstream dependency key hash
```

File carrier nodes are path-addressed, not file-content-addressed. File bytes are read lazily during resolution.

Compiled nodes include recipe parameters, schema, compiler version, and dependency identity so that different compile inputs produce different keys.

---

### SchemaID

A `SchemaID` identifies the compiler contract for a node.

A schema is **not just a file format**. It describes what kind of compiler recipe the node participates in.

Examples:

```cpp
kRawFileSchema
kHLSLFileSchema
kHLSLShaderSchema
kScalarFieldFromRawF32Schema
kScalarFieldProceduralSchema
```

Multiple schemas may produce the same asset type.

For example:

```text
kScalarFieldFromRawF32Schema
kScalarFieldProceduralSchema
    → kAssetTypeScalarField
```

That is expected and preferred.

---

### AssetType

An `AssetType` describes the output category of an asset.

Engine asset types are declared in:

```text
engine/assets/type_extensions.h
```

Current allocation policy:

```text
[0,   7]   Generic asset library values
[8,  63]   Reserved for future generic asset growth
[64,127]   Engine CPU/intermediate asset types
[128,191]  Renderer/backend GPU-resident asset types
[192,255]  Game/project-level asset types
```

Examples:

```cpp
kAssetTypeRawFile
kAssetTypeScalarField
kAssetTypeGaussianSplatCloud
```

---

### ResourceHandle

Compiled asset nodes usually store a `ResourceHandle` in their payload.

The handle does not own the resource. It points into a runtime table or backend resource store.

Convention:

```text
id == 0      invalid/null handle
epoch == 0   invalid/stale handle
id >= 1      live table entries only
```

Runtime tables must reserve slot 0 and never return it as a valid resource.

---

## Ownership model

The generic `AssetSystem` does not own heavyweight runtime data.

Instead:

```text
AssetSystem
    owns graph nodes, compiler cache, compiled-node records

EngineAssetLibrary
    owns CPU-side runtime tables

GPU backend/device
    owns GPU resources
```

For example, scalar fields follow this model:

```text
AssetNode payload
    → ResourceHandle

ScalarFieldTable
    → owns ScalarFieldData
```

This same pattern should be used for future CPU-side asset families:

```text
TextureTable
MeshTable
GaussianSplatCloudTable
AudioClipTable
```

GPU resources should remain backend-owned and be exposed through handles.

---

## File carrier pattern

File-backed assets usually use two nodes:

```text
file carrier node
    → recipe node
```

Example:

```text
.rawf32 file
    → kRawFileSchema node
        → kScalarFieldFromRawF32Schema node
            → ScalarFieldData
```

The file carrier reads bytes. The recipe compiler interprets those bytes.

This avoids confusing file I/O with domain-specific asset compilation.

---

## Procedural asset pattern

Procedural assets usually have no file dependency.

Example:

```text
ProceduralScalarFieldDesc
    → kScalarFieldProceduralSchema node
        → ScalarFieldData
```

The asset key must include every parameter that affects generated output.

If asset names are intended to distinguish otherwise-identical procedural assets, the name should participate in key identity.

---

## CPU assets vs GPU assets

Do not blur CPU-side data and GPU-side resources.

Prefer this layering:

```text
TextureData  → GPUTexture
MeshData     → GPUMesh
SplatCloud   → GPUSplatCloud
ShaderSource → GPUShader
```

The first compiler should usually produce CPU-side data unless the asset is inherently GPU-facing, such as a compiled shader.

Avoid compilers that do too much at once:

```text
bad:
    import file → decode → upload to GPU → create material binding

better:
    import file → TextureData
    TextureData → GPUTexture
    GPUTexture + Sampler + Shader → MaterialBinding
```

---

## Compiler version tokens

Compiler version tokens live in:

```text
engine/assets/compiler_version_tokens.h
```

Bump the relevant token when identical inputs would now produce different output.

Bump for:

```text
output layout changes
validation rule changes
generated value changes
import default changes
dependency interpretation changes
```

Do not bump for:

```text
pure refactors
logging changes
error message changes
performance improvements with identical output
```

Each independent compiler family should have its own token.

Examples:

```cpp
kHLSLCompilerVersion
kScalarFieldCompilerVersion
kTextureCompilerVersion
kMeshCompilerVersion
kGPUMeshUploadCompilerVersion
```

---

## Dependency hashing policy

Dependency vectors are ordered by default.

That means:

```text
deps = [A, B]
```

is not the same identity as:

```text
deps = [B, A]
```

If a key factory represents semantically unordered dependencies, it must canonicalize them explicitly before hashing.

Every multi-dependency key factory should document which dependency index means what.

Example:

```text
dep[0] = source file
dep[1] = optional include file
dep[2] = material binding table
```

---

## Resolve flow

Typical usage:

```cpp
wz::engine::assets::EngineAssetLibrary assets{
    device,
    logger,
    resource_root
};

auto field = assets.create_procedural_scalar_field(desc);

if (!assets.commit()) {
    // graph rejected: cycle, missing dependency, duplicate issue, etc.
}

auto report = assets.resolve_all();

if (!report.ok()) {
    // inspect report.failures
}

auto handle = assets.get_scalar_field(field);
const ScalarFieldData* data = assets.get_scalar_field_data(handle);
```

`resolve_all()` returns a structured `ResolveReport` containing both the resolved count and failures.

---

## Adding a new asset type

When adding a new asset family, follow this order.

### 1. Define the boundary

Decide what the asset represents.

Examples:

```text
TextureData       CPU-side decoded texels
GPUTexture        backend-owned uploaded texture
MeshData          CPU-side geometry
GPUMesh           backend-owned vertex/index buffers
Material          binding recipe, not texture data
```

Do not combine multiple stages into one asset unless there is a strong reason.

---

### 2. Add an AssetType

Add the type in:

```text
engine/assets/type_extensions.h
```

Example:

```cpp
inline constexpr wz::asset::AssetType kAssetTypeMesh =
    static_cast<wz::asset::AssetType>(67);
```

Use the documented numeric ranges.

---

### 3. Add SchemaIDs

Add schemas in:

```text
engine/assets/schema_ids.h
```

Good names describe the recipe:

```cpp
kTextureProceduralSchema
kTextureFromRawRGBA8Schema
kMeshProceduralSchema
kMeshFromSimpleTextSchema
kGaussianSplatFromPLYSchema
```

---

### 4. Add a compiler version token

Add a token in:

```text
engine/assets/compiler_version_tokens.h
```

Example:

```cpp
inline constexpr uint64_t kMeshCompilerVersion = 1;
```

---

### 5. Define runtime data and descriptors

Create a folder for the asset family:

```text
engine/assets/mesh/
engine/assets/texture/
engine/assets/gaussian_splat/
```

Define:

```cpp
struct MeshData;
struct MeshCompileDesc;
class MeshTable;
```

For CPU-side assets, follow the scalar-field pattern:

```text
immutable data struct
compile descriptor stored in AssetNode::meta
runtime table that owns resolved data
ResourceHandle returned by table
```

---

### 6. Add key factories

Create key factory headers under:

```text
engine/assets/key_factories/
```

Examples:

```text
mesh.h
mesh_procedural.h
texture.h
texture_procedural.h
```

Keys should encode:

```text
recipe parameters
schema ID
compiler version
dependency hash
```

File-backed assets should include their source file key in `deps_hash`.

Procedural assets usually have no dependency hash.

---

### 7. Add EngineAssetLibrary API

Add public descriptors and handle wrappers:

```cpp
struct MeshFileDesc;
struct ProceduralMeshDesc;
struct MeshAsset;
struct MeshHandle;
```

Add public methods:

```cpp
MeshAsset create_mesh(const MeshFileDesc& desc);
MeshAsset create_procedural_mesh(const ProceduralMeshDesc& desc);

MeshHandle get_mesh(const MeshAsset& asset) const;
const MeshData* get_mesh_data(MeshHandle handle) const;
```

---

### 8. Add registration helpers

Inside `EngineAssetLibrary`, add private helpers:

```cpp
wz::asset::AssetKey register_mesh_node(...);
wz::asset::AssetKey register_procedural_mesh_node(...);
```

File-backed assets usually register both a file carrier and a recipe node.

Procedural assets usually register only a recipe node.

---

### 9. Register the compiler

Add compiler registration in:

```text
engine_asset_library_compiler_registry.cpp
```

A compiler should generally:

```text
1. Validate metadata.
2. Validate dependency count and payload types.
3. Decode or generate data.
4. Validate generated data.
5. Store data in the runtime table.
6. Return a compiled node containing the ResourceHandle.
```

Do not return a compiled node unless the payload is valid.

Use `compile_failed_node(input)` to fail compilation cleanly.

---

### 10. Add tests

Tests should cover:

```text
registration
duplicate-key behavior
commit
resolve_all
failure reports
handle validity
table lookup
known generated values
invalid descriptors
invalid dependencies
```

Prefer procedural tests first. They avoid file parsing and prove the asset-system path cleanly.

---

## Recommended implementation order for new families

For CPU-side asset families:

```text
1. AssetType
2. SchemaID
3. Compiler version token
4. Data struct
5. Compile descriptor
6. Runtime table
7. Key factory
8. EngineAssetLibrary public API
9. Registration helper
10. Compiler registration
11. Tests
```

For GPU-side upload assets:

```text
1. CPU source asset must already exist
2. GPU AssetType
3. Upload SchemaID
4. Upload compiler version token
5. Upload descriptor if needed
6. Key factory using CPU asset key as dependency
7. Compiler that calls GPU/backend API
8. Tests with a stub or real backend
```

---

## Current asset families

### Shaders

Current shader path:

```text
HLSL file carrier
    → HLSL shader recipe
        → GPU shader handle
```

Shaders are GPU-facing assets. Their compiler calls the GPU layer and returns a handle that represents backend-owned shader data.

---

### Scalar fields

Current scalar-field path:

```text
raw f32 file
    → ScalarFieldData

procedural scalar field recipe
    → ScalarFieldData
```

Scalar fields are CPU-side sampled data. They are not images, though they may later be converted into textures for debug display or rendering.

Scalar fields may represent:

```text
height maps
walkability maps
influence maps
lookup tables
terrain masks
baked computations
```

---

### Gaussian splats

Gaussian splat schema/key support exists conceptually, but full runtime data/table/compiler support should follow the same pattern as scalar fields.

Recommended split:

```text
GaussianSplatCloudData
    → CPU-side splat cloud

GPUSplatCloud
    → uploaded backend resource
```

---

### Textures

Recommended split:

```text
TextureData
    → CPU-side decoded texels

GPUTexture
    → backend-owned uploaded texture
```

Initial implementation should probably use procedural RGBA8 textures before image decoding.

---

### Meshes

Recommended split:

```text
MeshData
    → CPU-side geometry

GPUMesh
    → backend-owned vertex/index buffers
```

Initial implementation should probably use procedural triangle/quad/cube mesh data before OBJ, glTF, FBX, or GPU upload.

---

## Design rule

Keep V1 boring.

Do not add import libraries, compression, streaming, hot reload, GPU upload, material binding, editor previews, or advanced formats until the narrow CPU-side asset path is proven.

The asset system should grow by small vertical slices:

```text
recipe
    → key
        → node
            → compiler
                → handle
                    → table lookup
                        → test
```

```
```
