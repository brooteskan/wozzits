## Scene ECS Boundary Audit — 2026-06-08

| Section | Result | Details |
|---------|--------|---------|
| 1. Enum ↔ domain switch | PASS | 32 values, 32 cases — all covered |
| 2. Enum ↔ authored summary | PASS | All 32 kinds have a counter field in SceneAuthoredComponentSummary |
| 3. Node ↔ authored_components_for_node | PASS | All optional component fields represented |
| 4. Node ↔ summarize_authored | PASS | All optional component fields counted |
| 5. EditorAuthoring ↔ recipes | PASS | All 8 EditorAuthoring kinds in has_asset_authoring_recipes() and SceneAssetAuthoringRecipeSummary |
| 6. Runtime ↔ runtime summary | FAIL | AmbientLighting (Exportable) has no field in SceneRuntimeComponentSummary |
| 7. Runtime ↔ has_runtime_relevant | PASS | All RuntimeRelevant + Exportable components present |
| 8. JSON roundtrip coverage | PASS | All component fields have parse and export paths |
| 9. Materialization coverage | PASS | All 8 EditorAuthoring components handled in scene_authoring_materialize.cpp |
| 10. Test coverage | PASS | All 4 required test files exist; see gaps in Notes |

### Failures

**Check 6 — `AmbientLighting` missing from `SceneRuntimeComponentSummary`**

`AmbientLighting` is classified as `Exportable` by `scene_component_domain()` (`scene_ecs.h:88-94`) and is checked in `has_runtime_relevant_components()` (`scene_asset_data.h:1265`), but `SceneRuntimeComponentSummary` (`scene_ecs.h:196-216`) has no `ambient_lighting` field.

All other Exportable components (`Renderable`, `Camera`, `Light`, `HDRIEnvironment`, `AuxiliaryVisual`) have dedicated runtime summary fields. `SkyVisual` and `SkySurface` share a `sky_draws` field, which is a deliberate runtime merge; that pattern appears intentional. `AmbientLighting` has no field at all — not even a merged one.

### Notes

- **sky_visual / sky_surface recipe tracking gap**: Both `SkyVisual` and `SkySurface` are included in `has_asset_authoring_recipes()` (`scene_asset_data.h:1251-1255`) but neither appears in `SceneAssetAuthoringRecipeSummary` nor is counted in `summarize_scene_asset_authoring_recipes()`. These are `Exportable` components so check 5 (which is scoped to `EditorAuthoring`) passes, but the recipe summary is incomplete for these two Exportable authoring paths.

- **ambient_lighting and sky roundtrip test gaps**: `ambient_lighting`, `sky_visual`, and `sky_surface` are covered in `scene_authoring_component_roundtrip_tests.cpp` but have **no coverage** in `scene_runtime_component_roundtrip_tests.cpp`. The runtime instantiation path for ambient lighting (which produces a scene light record) and sky draws (which produce `SceneSkyDrawAsset` entries) are not exercised by the runtime roundtrip suite.

- **Surfel LOD fields present**: `SceneTerrainRenderStyleAsset` includes `enable_surfel_lods`, `surfel_target_coverage_px`, `max_asset_triangle_density`, and `max_screen_triangle_density`. These serialize/deserialize cleanly (confirmed in runtime roundtrip tests at lines 682–690) and are wired through the full boundary, consistent with recent terrain surfel work.
