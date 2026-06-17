# AssetGraphDraft Key Parity

`AssetGraphDraft` is the editable authoring model that sits between editor
operations and the runtime-oriented `AssetSystem` registration graph. This
document specifies the key-factory boundary needed for draft materialization to
produce the same `AssetKey` values as engine asset module registration.

## Invariant

For every engine asset recipe identified by `(schema, type)`, a key produced
while materializing a draft node must be bit-identical to the key produced by
the normal `EngineAssetLibrary` registration path from the same inputs:

```text
key(draft node built from params P, deps D)
==
key(EngineAssetLibrary.create_*(P) with deps D)
```

This matters because the runtime asset graph is content-addressed by
`AssetKey`. If the editor creates or edits a node and lands on a different key
than the registration path would have produced, it creates a parallel cache
identity for the same logical asset.

For shader editing, the desired outcome is:

1. The editor authors an HLSL source and shader node through draft state.
2. Draft commit materializes the same keys as `ShaderAssetModule`.
3. The committed `AssetSystem` reuses or invalidates cache entries under the
   canonical keys.

Key parity is necessary before file reload or hot-reload behavior can be made
predictable.

## Current Tension

The generic draft layer can already validate, snapshot, materialize, and project
an editable graph. Its fallback key function is intentionally generic:
`wz::asset::make_asset_key(const AssetNode&, dep_keys)`.

Engine asset recipes are not generic:

- file carriers use path-based keys;
- HLSL shaders use typed shader params and a source-file dependency;
- render programs fold descriptor bindings, root constants, topology, render
  domain, render policy, and other structured fields.

The real key factories take typed descriptors or typed arguments, not generic
`AssetNode` values. For parity, draft materialization cannot reimplement those
rules independently.

## Generic Draft Hook

Keep `wz::asset` engine-agnostic by adding a callback that can be injected by
the engine layer:

```cpp
namespace wz::asset {

using AssetKeyFactoryFn = std::function<std::optional<AssetKey>(
    const AssetNode& node,
    std::span<const AssetKey> dep_keys)>;

[[nodiscard]] inline AssetKey resolve_asset_graph_draft_key(
    const AssetNode& node,
    std::span<const AssetKey> dep_keys,
    const AssetKeyFactoryFn& key_factory)
{
    if (key_factory) {
        if (std::optional<AssetKey> key = key_factory(node, dep_keys)) {
            return *key;
        }
    }
    return make_asset_key(node, dep_keys);
}

} // namespace wz::asset
```

Thread the hook through these existing entry points with a default empty
callback so current callers keep their behavior:

- `materialize_asset_graph_draft_keys(draft, registry, key_factory = {})`
- `asset_graph_draft_to_registrations(draft, registry, key_factory = {})`

Both paths must route key creation through
`resolve_asset_graph_draft_key(...)`. The recursive `key_for_node` lambda inside
`asset_graph_draft_to_registrations` is load-bearing: downstream `deps_hash`
values only match engine registration if upstream draft keys are canonical too.

`validate_asset_graph_draft` does not need this hook because validation checks
shape, compiler availability, input ports, and existing keys; it does not mint
new keys.

## Engine Dispatcher

The engine layer owns both the compiler registry and the typed key factories, so
it should own the dispatcher:

```cpp
namespace wz::engine::assets {

wz::asset::AssetKeyFactoryFn make_engine_asset_key_factory(
    const wz::asset::CompilerRegistry& registry);

} // namespace wz::engine::assets
```

Dispatch by schema, because schema identifies the recipe and also covers
compiler-less source nodes such as file carriers. The dispatcher returns
`std::nullopt` for recipes that are not yet draft-authorable, allowing the
generic fallback to preserve current behavior.

Dependency lookup should be by input-port name, not by hard-coded index:

```cpp
AssetKey dep_at(
    const CompilerRegistry& registry,
    const AssetNode& node,
    std::span<const AssetKey> deps,
    std::string_view port_name);
```

That makes the factory resilient to port-order changes in `AssetCompiler`.

Expose the hook from `EngineAssetLibrary`, which already owns the asset system
and registry:

```cpp
wz::asset::AssetKeyFactoryFn EngineAssetLibrary::draft_key_factory() const
{
    return make_engine_asset_key_factory(system_.registry());
}
```

## Shared Params-To-Desc-To-Key Pipeline

Parity must be by construction. Each recipe should have one path from authored
inputs to descriptor to key:

```cpp
CustomRenderProgramDesc desc_from_node(
    const wz::asset::AssetNode& node,
    std::span<const wz::asset::AssetKey> deps);

CustomRenderProgramAsset RenderProgramAssetModule::create_custom(
    const CustomRenderProgramDesc& desc)
{
    const AssetKey key = make_custom_render_program_key(desc);
    // register...
}

// Draft hook:
return make_custom_render_program_key(desc_from_node(node, deps));
```

The rule is: no recipe gets a key in two places. If a `create_*` method currently
inlines descriptor interpretation or key construction, extract the shared
builder first and make both registration and draft materialization call it.

## Phasing

Phase 1 should cover flat-parameter recipes:

- file carriers;
- HLSL source file and HLSL shader;
- compute shader / compute pipeline recipes whose inputs fit flat params;
- scalar/vector field source recipes with scalar/string parameters.

These recipes can usually be rebuilt from `ParamBlock` plus ordered dependency
keys.

Phase 2 should cover structured descriptors:

- custom render programs;
- recipes with descriptor binding arrays;
- recipes with root-constant arrays;
- recipes with nested authoring structures.

These need an authoring representation decision first:

- typed `std::any` metadata with a JSON serialization story; or
- an extended `ParamBlock` that supports arrays/nested values.

Until then, structured recipes should be explicit fallback cases rather than
half-authorable drafts.

## Tests

The acceptance tests for this work are parity tests, not UI tests.

1. Per-recipe parity:
   build a draft node from params plus dependencies, materialize with the engine
   hook, and assert the resulting key equals the relevant
   `EngineAssetLibrary.create_*` key.

2. Dependency propagation:
   change an upstream draft node parameter and assert a dependent node key
   changes through canonical dependency folding.

3. Fallback:
   use an unknown or explicitly generic schema; assert the key equals today's
   generic `wz::asset::make_asset_key(...)`.

4. Coverage guard:
   iterate registered compilers and assert each `(schema, type)` either has a
   hook arm or is present in an explicit allow-list of generic/future recipes.

## Risks

The generic draft hashing helpers and the engine asset key helpers each define
similar mixing and dependency-folding functions. Once the engine hook exists,
the engine helpers become authoritative for engine recipes. The duplicate
hashing code should eventually collapse into one shared header to avoid silent
drift.

This design does not specify file watching, cache invalidation, or editor
transaction APIs. It only makes draft-authored keys correct. Reload and
hot-reload behavior should build on top of canonical keys.
