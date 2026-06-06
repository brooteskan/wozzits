Phase 7 for issue #115: add partial invalidation and incremental re-resolution for editor-facing asset workflows.

## Motivation

Issue #116 exposed a recurring pattern: the materialization layer added local stale-detection guards because the asset system has no way to express "this logical asset changed; re-derive only the demanded affected outputs."

`AssetKey` is content-addressed, so parameter changes correctly produce a new key. What is missing is a stable logical identity that survives those parameter changes and lets the graph replace the current key for that logical asset, mark downstream dependents dirty, and re-resolve the demanded portion of the affected subgraph.

## Scope

- Add a stable `AssetIdentity` layer separate from `AssetKey`.
- Maintain an `AssetIdentity -> AssetKey` current-version mapping in the asset system.
- Add an update path such as `update_asset(identity, new_node, new_dep_keys)`.
- Propagate dirty state forward through dependents of the replaced node.
- Add demand-aware `resolve_dirty()` for dirty nodes reachable from active demand roots.
- Support cache-first dirty resolution using the existing `ExternalCacheProvider` path.
- Keep `resolve_all()` unchanged and independent of dirty flags.
- Remove ad-hoc materialization stale guards once the asset system owns re-derivation.

## Suggested implementation order

1. Add `AssetIdentity` and optional identity metadata on `AssetNode`.
2. Add identity mapping tests with no behavior change.
3. Add `update_asset(identity, new_node, deps)` and verify graph/index replacement semantics.
4. Add dirty marking and forward propagation tests.
5. Add `resolve_dirty()` with demand-root filtering.
6. Migrate the HDRI/materialization stale-guard case from #116 onto this system.

## Notes

Identity ownership should be explicit. Avoid deriving identity too magically from mutable display names unless the owning module can guarantee stability and uniqueness.

This follows #115 phases 1-6, which added demand roots, cache-first root resolution, runtime/editor convenience paths, eviction of non-demanded transient inputs, and `EngineDiskCacheProvider`.
