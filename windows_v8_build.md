# Building `wozzits-v8` with local V8 on Windows

This repo currently consumes a locally built V8 monolith. The Windows setup is delicate because V8 is built with Chromium’s clang/libc++ toolchain, while normal Wozzits projects usually use MSVC/CMake/Ninja.

The current working arrangement is:

```text
V8:
  built from source with GN/Ninja
  x64 release monolith
  clang-cl / lld-link
  Chromium libc++
  Temporal/Rust disabled
  sandbox disabled

wozzits-v8:
  built with V8's clang-cl/lld-link
  links v8_monolith.lib, v8_libplatform.lib, v8_libbase.lib
  links manually archived Chromium libc++.lib
  hides V8/libc++ details behind an opaque C-compatible-ish public API
```

---

## 1. Directory layout

Expected local layout:

```text
D:/code/wozzits/
  deps/
    v8/
      v8/
        include/
        out/
        third_party/
  wozzits-v8/
  wozzits-window-engine/
```

V8 checkout root:

```text
D:/code/wozzits/deps/v8/v8
```

`wozzits-v8` root:

```text
D:/code/wozzits/wozzits-v8
```

---

## 2. Use an x64 Visual Studio environment

Do not build from an x86 Visual Studio shell.

Verify:

```powershell
where.exe link
echo $env:LIB
```

You want paths like:

```text
...\Hostx64\x64\link.exe
...\lib\x64
...\ucrt\x64
...\um\x64
```

You do **not** want:

```text
...\Hostx86\x86\link.exe
...\lib\x86
```

If needed, load the x64 environment into PowerShell:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && set' | ForEach-Object { if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "Env:\$($matches[1])" -Value $matches[2] } }
```

---

## 3. V8 `args.gn`

Create:

```text
D:/code/wozzits/deps/v8/v8/out/x64.release.monolith/args.gn
```

Contents:

```gn
is_debug = false
target_cpu = "x64"
v8_target_cpu = "x64"

is_component_build = false
v8_monolithic = true

v8_use_external_startup_data = false
v8_enable_i18n_support = false
v8_enable_temporal_support = false
enable_rust = false

v8_enable_sandbox = false

treat_warnings_as_errors = false
extra_cflags = [ "-Wno-ctad-maybe-unsupported" ]
```

Important notes:

```text
v8_enable_temporal_support = false
enable_rust = false
```

avoids unresolved `temporal_rs_*` symbols.

```text
v8_enable_sandbox = false
```

avoids the safe-libc++ sandbox assertion for this first local devtools embedding build.

Do **not** set:

```gn
use_custom_libcxx = false
use_custom_libcxx_for_host = false
```

That caused V8 internal layout assertion failures in this checkout.

---

## 4. Build V8 monolith

From the V8 checkout:

```powershell
cd D:\code\wozzits\deps\v8\v8

gn gen out\x64.release.monolith
ninja -C out\x64.release.monolith v8_monolith
```

Expected libraries include:

```text
D:/code/wozzits/deps/v8/v8/out/x64.release.monolith/obj/v8_monolith.lib
D:/code/wozzits/deps/v8/v8/out/x64.release.monolith/obj/v8_libbase.lib
D:/code/wozzits/deps/v8/v8/out/x64.release.monolith/obj/v8_libplatform.lib
```

---

## 5. Create Chromium libc++ archive

V8 builds Chromium libc++ object files but does not necessarily package them as a convenient `.lib` for external CMake consumers.

Create a response file:

```powershell
cd D:\code\wozzits\deps\v8\v8\out\x64.release.monolith

Get-ChildItem .\obj\buildtools\third_party\libc++\libc++ -Filter *.obj | ForEach-Object { $_.FullName } | Set-Content .\libcxx_objs.rsp
```

Archive with **x64** `lib.exe`:

```powershell
lib.exe /OUT:"D:\code\wozzits\deps\v8\v8\out\x64.release.monolith\obj\buildtools\third_party\libc++\libc++.lib" "@D:\code\wozzits\deps\v8\v8\out\x64.release.monolith\libcxx_objs.rsp"
```

Some objects request `c++.lib` via default library directives, so create an alias:

```powershell
Copy-Item "D:\code\wozzits\deps\v8\v8\out\x64.release.monolith\obj\buildtools\third_party\libc++\libc++.lib" "D:\code\wozzits\deps\v8\v8\out\x64.release.monolith\obj\buildtools\third_party\libc++\c++.lib" -Force
```

Verify:

```powershell
Test-Path "D:\code\wozzits\deps\v8\v8\out\x64.release.monolith\obj\buildtools\third_party\libc++\libc++.lib"
Test-Path "D:\code\wozzits\deps\v8\v8\out\x64.release.monolith\obj\buildtools\third_party\libc++\c++.lib"
```

Both should print:

```text
True
```

---

## 6. Local libc++ config shims

`wozzits-v8` compiles its implementation file against Chromium libc++ headers so that `v8::platform::NewDefaultPlatform()` has the correct ABI.

The repo contains local shim files:

```text
wozzits-v8/generated/libcxx/__config_site
wozzits-v8/generated/libcxx/__assertion_handler
```

`generated/libcxx/__config_site`:

```cpp
#pragma once

#define _LIBCPP_HARDENING_MODE_DEFAULT _LIBCPP_HARDENING_MODE_NONE
#define _LIBCPP_ASSERTION_SEMANTIC_DEFAULT _LIBCPP_ASSERTION_SEMANTIC_IGNORE
```

`generated/libcxx/__assertion_handler`:

```cpp
#pragma once

#include <stdlib.h>

#define _LIBCPP_ASSERTION_HANDLER(message) abort()
```

These are required because Chromium libc++’s source headers expect generated config headers that are normally supplied inside GN’s build graph.

---

## 7. `wozzits-v8` local CMake preset

Create local uncommitted file:

```text
D:/code/wozzits/wozzits-v8/CMakeUserPresets.json
```

Contents:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "local-v8-clang-debug",
      "displayName": "Local V8 Clang Debug",
      "inherits": "ninja-debug",
      "cacheVariables": {
        "CMAKE_C_COMPILER": "D:/code/wozzits/deps/v8/v8/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe",
        "CMAKE_CXX_COMPILER": "D:/code/wozzits/deps/v8/v8/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe",
        "CMAKE_LINKER": "D:/code/wozzits/deps/v8/v8/third_party/llvm-build/Release+Asserts/bin/lld-link.exe",

        "V8_ROOT": "D:/code/wozzits/deps/v8/v8",
        "V8_INCLUDE_DIR": "D:/code/wozzits/deps/v8/v8/include",
        "V8_LIB_DIR": "D:/code/wozzits/deps/v8/v8/out/x64.release.monolith/obj"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "build-local-v8-clang-debug",
      "configurePreset": "local-v8-clang-debug"
    }
  ],
  "testPresets": [
    {
      "name": "test-local-v8-clang-debug",
      "configurePreset": "local-v8-clang-debug",
      "output": {
        "outputOnFailure": true
      }
    }
  ]
}
```

Do not commit `CMakeUserPresets.json`.

---

## 8. Build and test `wozzits-v8`

```powershell
cd D:\code\wozzits\wozzits-v8

Remove-Item -Recurse -Force .\build\ninja-debug -ErrorAction SilentlyContinue

cmake --preset local-v8-clang-debug
cmake --build --preset build-local-v8-clang-debug
ctest --preset test-local-v8-clang-debug --output-on-failure
```

Expected tests:

```text
script_host_smoke
engine_style_integration
```

Expected behavior:

```text
JS expressions run
wz.log(...) reaches C++
JS errors are captured
engine-style startup/frame/shutdown scripting example runs
```

---

## 9. Optional engine repo integration

In `wozzits-window-engine`, V8 scripting should be optional.

Use an option such as:

```cmake
option(WOZZITS_ENABLE_V8_SCRIPTING "Build optional V8 scripting examples" OFF)
```

When enabled, consume `wozzits-v8` by `add_subdirectory`:

```cmake
if(WOZZITS_ENABLE_V8_SCRIPTING)
    set(WOZZITS_V8_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WOZZITS_V8_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../wozzits-v8"
        "${CMAKE_CURRENT_BINARY_DIR}/_deps/wozzits-v8")
endif()
```

Engine-side targets should only include:

```cpp
#include <wozzits/script/script_host.h>
```

They should **not** include V8 headers and should **not** manually add Chromium libc++ include paths.

---

## 10. Engine repo local preset

In `wozzits-window-engine`, create local uncommitted:

```text
CMakeUserPresets.json
```

Example standalone preset:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "local-engine-v8-debug",
      "displayName": "Local Engine V8 Debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/local-engine-v8-debug",
      "architecture": {
        "value": "x64",
        "strategy": "external"
      },
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "WOZZITS_ENABLE_V8_SCRIPTING": "ON",

        "CMAKE_C_COMPILER": "D:/code/wozzits/deps/v8/v8/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe",
        "CMAKE_CXX_COMPILER": "D:/code/wozzits/deps/v8/v8/third_party/llvm-build/Release+Asserts/bin/clang-cl.exe",
        "CMAKE_LINKER": "D:/code/wozzits/deps/v8/v8/third_party/llvm-build/Release+Asserts/bin/lld-link.exe",

        "V8_ROOT": "D:/code/wozzits/deps/v8/v8",
        "V8_INCLUDE_DIR": "D:/code/wozzits/deps/v8/v8/include",
        "V8_LIB_DIR": "D:/code/wozzits/deps/v8/v8/out/x64.release.monolith/obj"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "build-local-engine-v8-debug",
      "configurePreset": "local-engine-v8-debug"
    }
  ]
}
```

Build:

```powershell
cd D:\code\wozzits\wozzits-window-engine

cmake --preset local-engine-v8-debug
cmake --build --preset build-local-engine-v8-debug
```

Run the engine V8 example executable from the build tree.

---

## 11. Important design rule

`wozzits-v8` is a containment boundary.

Inside `wozzits-v8`:

```text
V8 headers
Chromium libc++
std::string
std::unique_ptr
libplatform
V8 build macros
```

Outside `wozzits-v8`:

```text
opaque ScriptHost*
const char*
size_t
bool
```

Do not expose these in public headers:

```text
std::string
std::string_view
std::unique_ptr
v8::*
Chromium libc++ headers
```

This is what keeps the rest of Wozzits from inheriting the V8 build-system mess.

---

## 12. Current known debt

Current working setup includes some deliberate compromises:

```text
V8 sandbox disabled
Temporal/Rust disabled
Intl/ICU disabled
manual libc++ archive required
local libc++ config shims required
clang-cl/lld-link island required
public API uses pointer lifetimes rather than copied owned results
```

Near-term cleanup:

```text
document exact V8 build recipe
automate libc++ archive creation
add install/export support to wozzits-v8
replace pointer-result API with caller-provided output buffers if needed
add more bindings carefully: wz.log first, then maybe wz.time/frame
```

---

## 13. “Known-good” success output

`wozzits-v8` standalone engine-style example:

```text
script result: startup complete
[script] script system online
script result: 0
[script] frame 0 from js
script result: 1
[script] frame 1 from js
script result: 2
[script] frame 2 from js
script result: done
[script] script system shutdown
```

Engine repo V8 smoke should similarly show startup/frame logs from JS.

