# Asset Capability Template

Copy this file to `docs/asset_system/capabilities/<name>.md` and fill in each section.
Delete sections that do not apply (e.g. GPU realization for pure-CPU assets).

---

# `<CapabilityName>`

> One-sentence description of what this capability produces and what it is used for.

## Identity

| Field           | Value |
|-----------------|-------|
| Schema ID       | `kXxxSchema` = `0xF11ECA55E7_______` |
| Output AssetType | `kAssetTypeXxx` = N |
| CPU storage     | `XxxTable` (owned by `EngineAssetLibrary`) |
| GPU realization | Uploaded by `XxxGPUUploader` on demand / None |

Multiple schemas may share the same output AssetType (e.g., all mesh schemas produce
`kAssetTypeMesh`). List each schema separately if applicable.

## Module API

**Header:** `engine/assets/xxx_asset_module.h`  
**Class:** `XxxAssetModule`

```cpp
// Create — registers the asset with the system; compilation is deferred.
XxxAsset create_xxx(const XxxDesc& desc);

// Resolve — returns a handle into the CPU table; null if not yet compiled.
XxxHandle get_xxx(const XxxAsset& asset) const;

// Inspect — returns a pointer to the compiled CPU data; null if not ready.
const XxxData* get_xxx_data(XxxHandle handle) const;
```

## Key Factory

**Header:** `engine/assets/key_factories/xxx.h`

```cpp
wz::asset::AssetKey make_xxx_key(/* ... */);
```

Keys are deterministic and content-addressed. Two calls with identical inputs
produce the same key.

## Dependencies

List what other asset types this capability depends on at compile time.

| Dependency      | How required |
|-----------------|--------------|
| `kAssetTypeXxx` | e.g., source file carrier, always required |

## CPU Runtime Data

`XxxData` (defined in `engine/assets/xxx/xxx.h`) contains:

- ...list key fields...

Stored in `XxxTable`; accessed by `ResourceHandle` returned from `get_xxx()`.

## GPU Realization

> Delete this section if the asset has no GPU-side representation.

Uploaded by `<UploadPath>` after the CPU compile completes.
GPU handle type: `wz::gpu::GPUHandle` / `wz::scene::XxxHandle`.

The upload step is triggered by ... (caller-driven / automatic / lazy).

## Usage Example

```cpp
// 1. Create descriptor
XxxDesc desc{ .name = "my_asset", /* ... */ };

// 2. Register with the module (compilation is deferred)
XxxAsset asset = xxx_module.create_xxx(desc);

// 3. After system.compile_pending() — resolve the CPU handle
XxxHandle handle = xxx_module.get_xxx(asset);

// 4. Inspect data
if (const XxxData* data = xxx_module.get_xxx_data(handle))
{
    // ... use data
}
```

## Notes

- Any caveats, performance notes, or design constraints that don't fit above.
