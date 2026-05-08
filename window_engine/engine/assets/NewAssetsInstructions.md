````markdown
# Adding a New Asset Type

This is the short mechanical checklist for adding a new asset type to `engine/assets`.

Example asset families: `Texture`, `Mesh`, `GaussianSplatCloud`, `AudioClip`.

---

## 1. Add the asset type

Edit:

```text
engine/assets/type_extensions.h
````

Add a new `AssetType` constant.

Example:

```cpp
inline constexpr wz::asset::AssetType kAssetTypeTexture =
    static_cast<wz::asset::AssetType>(67);
```

Use the documented numeric range.

---

## 2. Add the schema ID

Edit:

```text
engine/assets/schema_ids.h
```

Add one schema for each compiler recipe.

Example:

```cpp
inline constexpr wz::asset::SchemaID kTextureProceduralSchema{
    0xF11E'CA55'E7'000300ull
};

inline constexpr wz::asset::SchemaID kTextureFromRawRGBA8Schema{
    0xF11E'CA55'E7'000301ull
};
```

A schema describes the compiler recipe, not just the file format.

---

## 3. Add the compiler version token

Edit:

```text
engine/assets/compiler_version_tokens.h
```

Example:

```cpp
inline constexpr uint64_t kTextureCompilerVersion = 1;
```

Bump this later when the compiler would produce different output from the same input.

---

## 4. Add the asset data types

Create:

```text
engine/assets/<asset_name>/<asset_name>.h
```

Example:

```text
engine/assets/texture/texture.h
```

Define:

```cpp
struct TextureData;
struct TextureCompileDesc;
struct ProceduralTextureCompileDesc;
class TextureTable;
```

Follow the scalar-field pattern:

```text
Data struct
Compile descriptor
Runtime table
```

The table owns the resolved CPU-side data and returns `ResourceHandle`s.

---

## 5. Add the asset table implementation

Create:

```text
src/engine/assets/<asset_name>/<asset_name>.cpp
```

Example:

```text
src/engine/assets/texture/texture.cpp
```

Implement:

```cpp
TextureTable::TextureTable();
ResourceHandle TextureTable::add(TextureData data);
const TextureData* TextureTable::get(ResourceHandle handle) const;
void TextureTable::destroy();
```

Reserve slot 0 as the invalid sentinel.

---

## 6. Add key factories

Create one or more files under:

```text
engine/assets/key_factories/
```

Examples:

```text
engine/assets/key_factories/texture.h
engine/assets/key_factories/texture_procedural.h
```

Each key factory should encode:

```text
recipe parameters
schema ID
compiler version
dependency hash
```

File-backed assets include the source file key in `deps_hash`.

Procedural assets usually have no dependencies.

---

## 7. Add public API to EngineAssetLibrary

Edit:

```text
engine/assets/engine_asset_library.h
```

Add descriptor types:

```cpp
struct TextureFileDesc;
struct ProceduralTextureDesc;
```

Add asset/handle wrappers:

```cpp
struct TextureAsset;
struct TextureHandle;
```

Add methods:

```cpp
TextureAsset create_texture(const TextureFileDesc& desc);
TextureAsset create_procedural_texture(const ProceduralTextureDesc& desc);

TextureHandle get_texture(const TextureAsset& asset) const;
const TextureData* get_texture_data(TextureHandle handle) const;
```

Add a table member:

```cpp
TextureTable textures_;
```

Make sure the table is declared before `system_`.

---

## 8. Add private registration helpers

Edit:

```text
engine/assets/engine_asset_library.h
```

Add private methods:

```cpp
wz::asset::AssetKey register_texture_node(
    wz::asset::AssetKey source_file,
    const TextureCompileDesc& desc
);

wz::asset::AssetKey register_procedural_texture_node(
    const ProceduralTextureCompileDesc& desc,
    std::string_view name
);
```

---

## 9. Update internal compiler registry signature

Edit:

```text
engine/assets/engine_asset_library_internal.h
```

Add the new table parameter:

```cpp
wz::asset::CompilerRegistry make_engine_compiler_registry(
    wz::gpu::Device& device,
    wz::Logger& logger,
    ScalarFieldTable& scalar_field_table,
    TextureTable& texture_table
);
```

---

## 10. Update EngineAssetLibrary constructor

Edit:

```text
src/engine/assets/engine_asset_library.cpp
```

Initialize the new table before `system_`:

```cpp
, textures_{}
, system_(internal::make_engine_compiler_registry(
      device,
      logger,
      scalar_fields_,
      textures_))
```

---

## 11. Add create/get implementation file

Create:

```text
src/engine/assets/engine_asset_library_<asset_name>s.cpp
```

Example:

```text
src/engine/assets/engine_asset_library_textures.cpp
```

Implement:

```cpp
TextureAsset EngineAssetLibrary::create_texture(...);
TextureAsset EngineAssetLibrary::create_procedural_texture(...);
TextureHandle EngineAssetLibrary::get_texture(...) const;
const TextureData* EngineAssetLibrary::get_texture_data(...) const;
register_texture_node(...);
register_procedural_texture_node(...);
```

File-backed version usually does:

```text
register file node
register recipe node depending on file node
```

Procedural version usually does:

```text
register recipe node with no dependencies
```

---

## 12. Register the compiler

Edit:

```text
src/engine/assets/engine_asset_library_compiler_registry.cpp
```

Add a compiler:

```cpp
registry.register_compiler(wz::asset::AssetCompiler{
    .input_schema = kTextureProceduralSchema,
    .output_type = kAssetTypeTexture,
    .compile = [&logger, &texture_table](
        const wz::asset::AssetNode& input,
        std::span<const wz::asset::AssetNode> dep_nodes,
        std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
    {
        // validate meta
        // validate deps
        // generate/decode TextureData
        // texture_table.add(...)
        // return compiled node with handle
    }
});
```

On failure, return:

```cpp
return compile_failed_node(input);
```

---

## 13. Add source files to CMake

Edit the appropriate:

```text
CMakeLists.txt
```

Add:

```text
src/engine/assets/<asset_name>/<asset_name>.cpp
src/engine/assets/engine_asset_library_<asset_name>s.cpp
```

Make sure public headers are reachable through the include path.

---

## 14. Add tests

Create:

```text
tests/<asset_name>_asset_tests.cpp
tests/procedural_<asset_name>_tests.cpp
```

Test:

```text
registration
commit
resolve_all
handle validity
table lookup
known generated data
invalid descriptors
duplicate keys
failure reporting
```

Start with procedural tests before file-backed tests.

---

## 15. Add test resources if needed

Add files under:

```text
resources/<asset_name>s/
```

Examples:

```text
resources/textures/test_checker_rgba8.raw
resources/meshes/test_triangle.mesh.txt
resources/splats/test_splats.ply
```

Only add resources after the procedural path works.

---

## Minimal order

Use this order when actually coding:

```text
1. type_extensions.h
2. schema_ids.h
3. compiler_version_tokens.h
4. engine/assets/<asset>/<asset>.h
5. src/engine/assets/<asset>/<asset>.cpp
6. key_factories/<asset>.h
7. engine_asset_library.h
8. engine_asset_library_internal.h
9. engine_asset_library.cpp
10. engine_asset_library_<assets>.cpp
11. engine_asset_library_compiler_registry.cpp
12. CMakeLists.txt
13. tests
```

```
```
