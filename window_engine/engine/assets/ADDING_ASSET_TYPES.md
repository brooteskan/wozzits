# Adding a New Asset Type

A worked example: adding a `Mesh` asset type backed by a `.obj` file.

---

## 1. Reserve an AssetType — `type_extensions.h`

Add a constant in the correct numeric range (see the range table at the top of the file).
Meshes are CPU/intermediate assets, so they belong in `[64, 511]`.

```cpp
inline constexpr wz::asset::AssetType kAssetTypeMesh =
    static_cast<wz::asset::AssetType>(128);  // pick the next free value
```

---

## 2. Reserve a SchemaID — `schema_ids.h`

Add one constant per recipe variant. Meshes fall in the `0x000400–0x0004FF` range.

```cpp
inline constexpr wz::asset::SchemaID kMeshFromObjSchema{
    0xF11E'CA55'E7'000400ull
};
```

---

## 3. Write a key factory — `key_factories/mesh.h` (new file)

The key must uniquely identify the output given its inputs. For a file-backed asset,
hash the source file key plus any compile parameters.

```cpp
#pragma once
#include <engine/assets/engine_asset_key_core.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/compiler_version_tokens.h>

namespace wz::engine::assets {

    [[nodiscard]] inline wz::asset::AssetKey make_mesh_key(
        const wz::asset::AssetKey& source_file_key) noexcept
    {
        return wz::asset::AssetKey{
            .content_hash = { 0, 0 },
            .schema_hash  = detail::hash_u64(kMeshFromObjSchema.value),
            .compiler_hash = detail::hash_u64(kMeshCompilerVersion),  // add to compiler_version_tokens.h
            .deps_hash    = detail::key_to_dep_hash(source_file_key),
        };
    }

} // namespace wz::engine::assets
```

---

## 4. Create a runtime table (if the asset lives on the CPU)

If the compiled asset is a CPU-side value (like `ScalarFieldData`), add a typed table
that stores it and hands out `ResourceHandle`s.

Mirror the pattern from `scalar_field/scalar_field.h`:
- A `MeshData` struct holding the parsed data.
- A `MeshTable` class with `add(MeshData)` → `ResourceHandle` and `get(ResourceHandle)` → `MeshData*`.

Skip this step for GPU-resident assets — those return a `wz::gpu::GPUHandle` directly
(as the shader compiler does).

---

## 5. Write the module header — `window_engine/engine/assets/mesh_asset_module.h` (new file)

```cpp
#pragma once
#include <asset/system.h>
#include <engine/assets/file_carrier_asset_module.h>
#include <engine/assets/mesh/mesh.h>  // MeshData, MeshTable
#include <logging/logger.h>

namespace wz::engine::assets {

    struct MeshFileDesc {
        std::string  name;
        wz::fs::Path path;
    };

    struct MeshAsset {
        wz::asset::AssetKey output{};
        bool valid() const noexcept { return !(output == wz::asset::AssetKey{}); }
    };

    struct MeshHandle {
        wz::asset::ResourceHandle handle{};
        bool valid() const noexcept { return handle.valid(); }
    };

    class MeshAssetModule {
    public:
        MeshAssetModule(wz::asset::AssetSystem&, wz::Logger&,
                        FileCarrierAssetModule&, MeshTable&);

        MeshAsset  create_mesh(const MeshFileDesc& desc);
        MeshHandle get_mesh(const MeshAsset& asset) const;
        const MeshData* get_mesh_data(MeshHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        FileCarrierAssetModule& files_;
        MeshTable&              table_;
    };

} // namespace wz::engine::assets
```

---

## 6. Write the module implementation — `src/engine/assets/mesh_asset_module.cpp` (new file)

```cpp
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/mesh.h>
#include <vector>

namespace wz::engine::assets {

    MeshAssetModule::MeshAssetModule(
        wz::asset::AssetSystem& system, wz::Logger& logger,
        FileCarrierAssetModule& files, MeshTable& table)
        : system_(system), logger_(logger), files_(files), table_(table) {}

    MeshAsset MeshAssetModule::create_mesh(const MeshFileDesc& desc)
    {
        const wz::asset::AssetKey file_key =
            files_.register_file_node(desc.path, kObjFileSchema, kAssetTypeRawFile);
        if (file_key == wz::asset::AssetKey{}) return {};

        const wz::asset::AssetKey mesh_key = make_mesh_key(file_key);

        wz::asset::AssetNode node;
        node.key    = mesh_key;
        node.type   = kAssetTypeMesh;
        node.schema = kMeshFromObjSchema;
        node.stage  = wz::asset::AssetStage::Source;

        if (!system_.register_asset(std::move(node), { file_key })) {
            logger_.error("failed to register mesh node: " + desc.name);
            return {};
        }

        return MeshAsset{ .output = mesh_key };
    }

    MeshHandle MeshAssetModule::get_mesh(const MeshAsset& asset) const
    {
        if (!asset.valid()) return {};
        MeshHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;
        return out;
    }

    const MeshData* MeshAssetModule::get_mesh_data(MeshHandle handle) const
    {
        return handle.valid() ? table_.get(handle.handle) : nullptr;
    }

} // namespace wz::engine::assets
```

---

## 7. Register the compiler — `engine_asset_library_compiler_registry.cpp`

Add a `registry.register_compiler(...)` block inside `make_engine_compiler_registry`.
The compiler receives the source node's `meta` (your compile desc) and the compiled
payloads of its dependencies.

```cpp
registry.register_compiler(wz::asset::AssetCompiler{
    .input_schema = kMeshFromObjSchema,
    .output_type  = kAssetTypeMesh,
    .compile = [&logger, &mesh_table](
        const wz::asset::AssetNode& input,
        std::span<const wz::asset::AssetNode> dep_nodes,
        std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
    {
        // 1. Read bytes from the file-carrier dependency.
        const auto* bytes =
            std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);
        if (!bytes) return compile_failed_node(input);

        // 2. Parse / validate.
        MeshData data = parse_obj(*bytes);
        if (!data.valid()) return compile_failed_node(input);

        // 3. Store in table; embed handle in the compiled node.
        wz::asset::ResourceHandle handle = mesh_table.add(std::move(data));
        wz::asset::AssetNode out = input;
        out.stage   = wz::asset::AssetStage::Compiled;
        out.payload = handle;
        return out;
    }
});
```

Also add `MeshTable& mesh_table` to the `make_engine_compiler_registry` signature and
its declaration in `engine_asset_library_internal.h`.

---

## 8. Wire into `EngineAssetLibrary` — two files

**`window_engine/engine/assets/engine_asset_library.h`**

```cpp
#include <engine/assets/mesh_asset_module.h>

// In the private section — order is load-bearing (see comment block):
MeshTable       mesh_table_;   // before system_
// ...
MeshAssetModule meshes_;       // after files_

// Add accessor:
MeshAssetModule& meshes() { return meshes_; }
const MeshAssetModule& meshes() const { return meshes_; }
```

**`src/engine/assets/engine_asset_library.cpp`** — constructor initialiser list:

```cpp
, mesh_table_{}
// (system_ and files_ already here)
, meshes_(system_, logger_, files_, mesh_table_)
```

And pass `mesh_table_` to `make_engine_compiler_registry(device, logger, scalar_fields_table_, mesh_table_)`.

---

## 9. Add the new `.cpp` to the build — `src/engine/CMakeLists.txt`

```cmake
assets/mesh_asset_module.cpp
```

---

## Checklist

- [ ] `type_extensions.h` — new `kAssetType*` constant, correct range
- [ ] `schema_ids.h` — one `kSchema` constant per recipe variant, correct range
- [ ] `compiler_version_tokens.h` — new version token for the key factory
- [ ] `key_factories/my_asset.h` — deterministic key function
- [ ] Runtime table (if CPU-resident) — `add()` / `get()` / `destroy()`
- [ ] Module header + implementation
- [ ] Compiler lambda in `engine_asset_library_compiler_registry.cpp`
- [ ] `engine_asset_library.h` — include, table member, module member, accessor
- [ ] `engine_asset_library.cpp` — initialiser list, pass table to registry
- [ ] `CMakeLists.txt` — new `.cpp` source
- [ ] Tests — registration, commit, resolve, data correctness, rejection cases
