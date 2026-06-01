# Terrain Collision Behaviors

Project-local behavior module source for `terrain_collision.project.json`.

The scene editor loads compiled behavior DLLs from:

```text
behavior/build/clang-debug
```

This behavior module currently registers:

- `template`

The handler uses `switch (event->kind)` to respond to routed events such as
`WZ_EVENT_COLLISION_ENTER`. Use `wz_self(event)` for the receiving component's
entity and `wz_other(event)` for the collision partner. Transform helpers such
as `wz_self_world_position` read the scene graph state from the current behavior
dispatch snapshot.

Transform commands are buffered during dispatch and applied afterward. Rotation
commands use `WzQuaternion {x, y, z, w}`, replace local rotation, preserve local
translation, and preserve the current basis-column scale. Additive local
rotation is intentionally deferred.
