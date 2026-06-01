# Behavior Plugin Template

This folder is a minimal C++ DLL behavior pack for the scene editor.

Build it with CMake, then point the scene editor's **Behavior module folder**
field at the directory containing the built DLL and press **Load Modules**.

The plugin exports:

```cpp
extern "C" uint8_t wz_register_behaviors(WzBehaviorPluginApi* api);
```

Scene nodes bind behaviors by `module/name`. This template registers:

- `template/log_collision_events`
- `template/bounce_on_collision_enter`

The template includes only `engine/behavior/behavior_plugin_abi.h`, so it is a
good starting point for user-authored behavior modules.

