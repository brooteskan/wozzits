# Modular Engine Asset Library Design

## 1. Purpose

The current `EngineAssetLibrary` has grown into a mixed-responsibility class. It owns the shared asset system, registers compilers, exposes public APIs for multiple asset families, owns runtime tables, and contains private helper logic for file carriers, shaders, scalar fields, and future asset families.

This design splits the system into a top-level asset context plus asset-specific modules.

The goal is not to add more taxonomy. The goal is to make each asset family add real capability while keeping shared DAG ownership centralized.

## 2. Design Goals

1. Keep one shared `wz::asset::AssetSystem` per engine asset library instance.
2. Keep asset graph commit and resolve lifecycle centralized.
3. Move asset-family-specific APIs into modules.
4. Keep runtime data ownership explicit.
5. Keep file carriers as implementation plumbing, not user-facing asset APIs unless a real use case appears.
6. Allow future modules such as CSV, textures, meshes, Gaussian splats, audio, materials, and packages without bloating the top-level class.
7. Avoid adding new schemas or asset types that do not add behavior.

## 3. High-Level Architecture

The new top-level object remains named `EngineAssetLibrary`.

It owns:

- `wz::asset::AssetSystem system_`
- shared runtime tables
- compiler registry construction
- logger/device/resource-root references
- asset modules

It exposes:

- graph lifecycle methods
- module accessors

Conceptual shape:

```text
EngineAssetLibrary
    commit()
    resolve_all()

    files()
    shaders()
    scalar_fields()
    csv()
    textures()        // future
    meshes()          // future
    splats()          // future
```

Example usage:

```cpp
auto field = assets.scalar_fields().create_procedural(desc);
auto shaders = assets.shaders().create_shader_pair(desc);
auto csv = assets.csv().create_csv_file(desc);

assets.commit();
assets.resolve_all();
```

The modules register nodes into the shared asset system. They do not own the asset system.

## 4. Responsibility Split

### 4.1 EngineAssetLibrary

The top-level `EngineAssetLibrary` is the asset context.

Responsibilities:

- Own the shared `AssetSystem`.
- Own shared resource tables.
- Own asset modules.
- Construct the compiler registry.
- Provide `commit()` and `resolve_all()`.
- Provide accessors to modules.
- Preserve initialization order for tables, system, and modules.

Non-responsibilities:

- It should not directly expose every asset-family API.
- It should not contain every compiler body.
- It should not contain every file/parser/import helper.

### 4.2 Asset Modules

Each module owns the public API for one asset family or subsystem.

Examples:

```text
FileCarrierAssetModule
ShaderAssetModule
ScalarFieldAssetModule
CSVAssetModule
TextureAssetModule
MeshAssetModule
GaussianSplatAssetModule
```

Responsibilities:

- Register asset nodes for its family.
- Provide family-specific creation/query methods.
- Query resolved handles and table data when appropriate.
- Keep private helper logic local to the module.

Non-responsibilities:

- Modules do not commit the graph.
- Modules do not resolve all assets globally.
- Modules do not own `AssetSystem`.
- Modules do not own unrelated tables.

### 4.3 Runtime Tables

CPU-side runtime tables remain owned by `EngineAssetLibrary`.

Examples:

```text
ScalarFieldTable
CSVDocumentTable
TextureTable
MeshTable
GaussianSplatCloudTable
```

Modules receive references to the tables they need.

This keeps handle ownership explicit:

```text
AssetSystem stores ResourceHandle
EngineAssetLibrary-owned table stores actual CPU-side data
Module exposes typed lookup API
```

GPU-side assets remain owned by the GPU backend. GPU compilers return handles into backend-owned tables/resources.

## 5. Proposed Class Layout

### 5.1 Top-Level EngineAssetLibrary

Conceptual member order:

```cpp
class EngineAssetLibrary
{
public:
    EngineAssetLibrary(
        wz::gpu::Device& device,
        wz::Logger& logger,
        wz::fs::Path resource_root
    );

    bool commit();
    ResolveReport resolve_all();

    FileCarrierAssetModule& files();
    ShaderAssetModule& shaders();
    ScalarFieldAssetModule& scalar_fields();
    CSVAssetModule& csv();

    const FileCarrierAssetModule& files() const;
    const ShaderAssetModule& shaders() const;
    const ScalarFieldAssetModule& scalar_fields() const;
    const CSVAssetModule& csv() const;

private:
    wz::gpu::Device& device_;
    wz::Logger& logger_;
    wz::fs::Path resource_root_;

    // Tables before system, because compiler lambdas capture table references.
    ScalarFieldTable scalar_fields_table_;
    CSVDocumentTable csv_documents_table_; // future

    wz::asset::AssetSystem system_;

    // Modules after system, because modules hold references to system.
    FileCarrierAssetModule files_;
    ShaderAssetModule shaders_;
    ScalarFieldAssetModule scalar_fields_;
    CSVAssetModule csv_; // future
};
```

Member initialization order matters. Tables must be constructed before the compiler registry captures them. `system_` must be constructed before modules that hold references to it.

### 5.2 FileCarrierAssetModule

This module manages internal file-carrier node registration.

It should not necessarily expose a public `create_file_asset()` API.

Responsibilities:

- Register file-backed source/carrier nodes.
- Canonicalize paths relative to `resource_root`.
- Construct file carrier keys using `make_file_key()`.
- Store `FileSourceDesc` metadata.

Conceptual API:

```cpp
class FileCarrierAssetModule
{
public:
    FileCarrierAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        wz::fs::Path resource_root
    );

    wz::asset::AssetKey register_file_node(
        const wz::fs::Path& relative_path,
        wz::asset::SchemaID schema,
        wz::asset::AssetType type
    );

private:
    wz::asset::AssetSystem& system_;
    wz::Logger& logger_;
    wz::fs::Path resource_root_;
};
```

This module is shared by other modules.

Examples:

```text
Shader module uses it to register HLSL source carriers.
Scalar field module uses it to register raw float file carriers.
CSV module uses it to register text/CSV source carriers if needed.
```

### 5.3 ShaderAssetModule

Move existing shader-pair functionality here.

Responsibilities:

- Expose shader-pair creation and lookup.
- Register HLSL file carrier nodes through `FileCarrierAssetModule`.
- Register HLSL shader recipe nodes.
- Retrieve resolved GPU handles.

Conceptual API:

```cpp
class ShaderAssetModule
{
public:
    ShaderPairAsset create_shader_pair(const ShaderPairDesc& desc);
    ShaderPairHandles get_shader_pair(const ShaderPairAsset& asset) const;

private:
    wz::asset::AssetKey register_hlsl_shader_node(
        std::string_view name,
        wz::asset::AssetKey source_file,
        const wz::gpu::HLSLCompileDesc& desc
    );
};
```

Dependencies:

- `AssetSystem&`
- `Logger&`
- `FileCarrierAssetModule&`
- possibly `Device&` only for compiler registration, not node registration

### 5.4 ScalarFieldAssetModule

Move existing scalar-field functionality here.

Responsibilities:

- Expose file-backed and procedural scalar-field creation.
- Register scalar-field recipe nodes.
- Retrieve scalar-field handles and table data.

Conceptual API:

```cpp
class ScalarFieldAssetModule
{
public:
    ScalarFieldAsset create_scalar_field(const ScalarFieldFileDesc& desc);

    ScalarFieldAsset create_procedural_scalar_field(
        const ProceduralScalarFieldDesc& desc
    );

    ScalarFieldHandle get_scalar_field(const ScalarFieldAsset& asset) const;

    const ScalarFieldData* get_scalar_field_data(
        ScalarFieldHandle handle
    ) const;

private:
    wz::asset::AssetKey register_scalar_field_node(
        wz::asset::AssetKey source_file,
        const ScalarFieldCompileDesc& desc
    );

    wz::asset::AssetKey register_procedural_scalar_field_node(
        const ProceduralScalarFieldCompileDesc& desc,
        std::string_view name
    );
};
```

Dependencies:

- `AssetSystem&`
- `Logger&`
- `FileCarrierAssetModule&`
- `ScalarFieldTable&`

### 5.5 CSVAssetModule

CSV should be introduced after the module split is stable.

CSV is a real structured asset capability, not a byte-carrier alias.

Target behavior:

- Parse RFC 4180-style CSV.
- Support quoted fields.
- Support escaped quotes as `""`.
- Support multiline quoted fields.
- Support CRLF and LF line endings.
- Preserve empty fields.
- Allow ragged rows.
- Preserve cell values as text.
- Do not infer types.

Conceptual API:

```cpp
class CSVAssetModule
{
public:
    CSVFileAsset create_csv_file(const CSVFileDesc& desc);
    CSVDocumentHandle get_csv_file(const CSVFileAsset& asset) const;
    const CSVDocument* get_csv_document_data(CSVDocumentHandle handle) const;

private:
    wz::asset::AssetKey register_csv_file_node(
        wz::asset::AssetKey source_file,
        const CSVFileDesc& desc
    );
};
```

Dependencies:

- `AssetSystem&`
- `Logger&`
- `FileCarrierAssetModule&` or direct source registration helper
- `CSVDocumentTable&`

## 6. Compiler Registration

Compiler registration should be modularized.

Current problem:

```text
engine_asset_library_compiler_registry.cpp
    grows without bound
```

Desired shape:

```cpp
wz::asset::CompilerRegistry make_engine_compiler_registry(
    wz::gpu::Device& device,
    wz::Logger& logger,
    ScalarFieldTable& scalar_field_table,
    CSVDocumentTable& csv_document_table
)
{
    wz::asset::CompilerRegistry registry;

    internal::register_file_carrier_compilers(registry, logger);
    internal::register_shader_compilers(registry, logger, device);
    internal::register_scalar_field_compilers(registry, logger, scalar_field_table);
    internal::register_csv_compilers(registry, logger, csv_document_table);

    return registry;
}
```

Each compiler group should live in its own implementation file.

Suggested files:

```text
src/engine/assets/engine_asset_library_compiler_registry.cpp
src/engine/assets/file_carriers/file_carrier_compilers.cpp
src/engine/assets/shader/shader_compilers.cpp
src/engine/assets/scalar_field/scalar_field_compilers.cpp
src/engine/assets/csv/csv_compilers.cpp
```

The composition root remains small. Compiler bodies live with their asset family.

## 7. File Carriers and Real Capabilities

The project rule going forward:

```text
Do not add a schema or asset type unless it adds behavior or reserves a deliberately planned role.
```

Byte-equivalent carriers are allowed to remain if already created, but new work should avoid semantic duplication.

Capability examples:

```text
RawFile:
    reads bytes from disk

TextFile:
    should eventually validate text assumptions if it is distinct from RawFile

CSVFile:
    parses structured tabular text

JSONFile:
    should either parse JSON or not exist yet

DirectoryAsset:
    enumerates directory entries

ArchiveAsset:
    reads archive structure

Manifest:
    parses/validates manifest structure

ExternalReference:
    validates and stores external URI/reference metadata
```

## 8. CSV Design Direction

CSV should not be implemented as another byte carrier.

CSV represents spreadsheet-like tabular data:

```text
Document
    rows
        cells
            text
```

Important design choices:

- Parser produces rows and cells.
- Rows may be ragged.
- Cells remain strings.
- Header interpretation is not part of the core parser.
- Type inference is not part of the core parser.
- Domain-specific importers interpret the document later.

Potential later layers:

```text
CSVDocument
    lossless parsed rows/cells

CSVTableView
    optional header interpretation
    rectangular checks
    column lookup

Domain importer
    converts table rows into scalar samples, spawn points, material data, etc.
```

## 9. Migration Plan

### Phase 1 — Introduce module shells

Create headers/classes for:

```text
FileCarrierAssetModule
ShaderAssetModule
ScalarFieldAssetModule
```

No behavior changes yet.

### Phase 2 — Move file carrier node registration

Move `register_file_node()` from `EngineAssetLibrary` into `FileCarrierAssetModule`.

Keep compiler registration function separate:

```text
internal::register_file_carrier_compilers(...)
```

Update shader/scalar-field code to use the file module.

### Phase 3 — Move scalar-field API

Move scalar-field descriptors, asset wrappers, handles, and methods into `ScalarFieldAssetModule` headers/source files.

Top-level access becomes:

```cpp
assets.scalar_fields().create_procedural_scalar_field(desc);
```

Existing tests should be updated accordingly.

### Phase 4 — Move shader API

Move shader-pair descriptors, asset wrappers, handles, and methods into `ShaderAssetModule`.

Top-level access becomes:

```cpp
assets.shaders().create_shader_pair(desc);
```

### Phase 5 — Slim down EngineAssetLibrary

After modules exist, `EngineAssetLibrary` should contain only:

- constructor
- graph lifecycle
- module accessors
- shared ownership

### Phase 6 — Add CSV module

Only after the module architecture is stable:

1. Define `CSVDocument`.
2. Define `CSVDocumentTable`.
3. Define `CSVFileDesc`, `CSVFileAsset`, `CSVDocumentHandle`.
4. Implement RFC 4180 parser with ragged-row support.
5. Register CSV compiler.
6. Add tests for parser and asset-system integration.

## 10. Testing Strategy

### Existing tests must keep passing

Refactor must preserve behavior before adding CSV.

### Module migration tests

For each migrated module:

```text
old behavior remains equivalent
new module API works
commit/resolve lifecycle still centralized
```

### CSV parser tests

Parser-level tests:

```text
parses simple rows
preserves empty fields
preserves trailing empty fields
parses quoted fields
parses commas inside quoted fields
parses escaped quotes
parses multiline quoted fields
accepts CRLF and LF
allows ragged rows
rejects unclosed quotes
rejects invalid quote placement
```

Asset integration tests:

```text
CSV file asset registers
commit succeeds
resolve_all parses document
handle is valid
CSVDocumentTable lookup returns expected rows/cells
invalid CSV reports compile failure
```

## 11. Open Questions

1. Should `TextFile` validate UTF-8, or remain a byte-preserving text-labeled carrier?
2. Should already-added redundant carriers remain registered, or only remain reserved as constants?
3. Should CSV support custom delimiters immediately, or should that become `DelimitedTextAssetModule` later?
4. Should `CSVDocument` store source locations per cell for diagnostics?
5. Should `CSVDocumentTable` be append-only like `ScalarFieldTable` in V1?
6. Should module accessors return references or pointers?
7. Should modules be constructible by users, or only by `EngineAssetLibrary`?

Recommended default answers:

1. `TextFile` should eventually validate text assumptions if it remains distinct.
2. Keep existing redundant work for now, but stop expanding that pattern.
3. Implement RFC 4180 CSV first; general delimiters can be a later `DelimitedText` module.
4. Leave room for source locations; implement if diagnostics become painful.
5. Yes, follow the V1 table contract.
6. Return references from module accessors.
7. Modules should be owned by `EngineAssetLibrary`; user construction is not the primary API.

## 12. Summary

The asset system should become modular without decentralizing the DAG.

The new `EngineAssetLibrary` is the asset context. It owns the shared `AssetSystem`, shared tables, and modules. Asset modules expose focused APIs and register nodes into the shared graph. Compiler registration is grouped by module. Runtime data ownership stays explicit.

CSV should be added only after this split is stable, and it should add real capability: parsing RFC 4180-style tabular text with ragged-row support into a structured document representation.

