cmake_minimum_required(VERSION 3.14)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
endif()

project(window_engine LANGUAGES C CXX)

if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "wozzits-window-engine only supports Clang targets on Windows. "
        "Configure with the clang-debug/clang-release presets or set "
        "CMAKE_C_COMPILER=clang-cl and CMAKE_CXX_COMPILER=clang-cl.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(WINDOW_ENGINE_DEFAULT_STANDALONE_TARGETS ON)
else()
    set(WINDOW_ENGINE_DEFAULT_STANDALONE_TARGETS OFF)
endif()

option(WINDOW_ENGINE_ENABLE_TESTS "Enable tests" ${WINDOW_ENGINE_DEFAULT_STANDALONE_TARGETS})
option(WINDOW_ENGINE_BUILD_EXAMPLES "Build standalone examples" ${WINDOW_ENGINE_DEFAULT_STANDALONE_TARGETS})
option(WOZZITS_ENABLE_V8_SCRIPTING "Build optional V8 scripting examples" OFF)


include(FetchContent)

function(wz_add_subdirectory_quiet source_dir)
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    add_subdirectory(${source_dir} ${ARGN})
endfunction()


# ----------------------------
# MSVC runtime configuration
# ----------------------------
if(MSVC)
    if(WOZZITS_ENABLE_V8_SCRIPTING)
        # V8 uses static CRT (/MT) — its allocator shim requires it.  Force /MT
        # for all configs so every object in the link agrees.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

# ----------------------------
# GoogleTest
# ----------------------------

if(WINDOW_ENGINE_ENABLE_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/heads/main.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(googletest)


    enable_testing()
endif()



# ----------------------
# dependency: fastgltf
# ----------------------
wz_add_subdirectory_quiet(external/gltf/fastgltf)

# ----------------------
# dependency: tinyply
# ----------------------
add_subdirectory(external/ply)

# ----------------------
# dependency: PMP
# ----------------------
set(PMP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(PMP_BUILD_VIEWERS OFF CACHE BOOL "" FORCE)
set(PMP_BUILD_POLYGONAL OFF CACHE BOOL "" FORCE)
set(PMP_INSTALL OFF CACHE BOOL "" FORCE)
set(PMP_STRICT_COMPILATION OFF CACHE BOOL "" FORCE)
set(PMP_BUILD_REGRESSIONS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WZ_CMAKE_CXX_FLAGS_BEFORE_PMP "${CMAKE_CXX_FLAGS}")
wz_add_subdirectory_quiet(external/pmp)
if(TARGET pmp AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # PMP adds GCC-style warning flags globally inside its subdirectory. Keep
    # third-party diagnostics out of engine builds while preserving our flags.
    target_compile_options(pmp PRIVATE -w)
endif()
set(CMAKE_CXX_FLAGS "${WZ_CMAKE_CXX_FLAGS_BEFORE_PMP}")
unset(WZ_CMAKE_CXX_FLAGS_BEFORE_PMP)


# ----------------------
# dependency: rtaudio 6.0.1
# ----------------------

# ---- RtAudio ----
# Wozzits embeds RtAudio as a private audio backend.
# Keep third-party tests/examples/docs off.

set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Prevent rtaudio from replacing /MD with /MT in its flag vars.
# Its CMakeLists defaults RTAUDIO_STATIC_MSVCRT=ON for static builds, which
# fights the CMAKE_MSVC_RUNTIME_LIBRARY setting above and causes a CRT mismatch.
set(RTAUDIO_STATIC_MSVCRT OFF CACHE BOOL "" FORCE)
# rtaudio's CMakeLists contains option(BUILD_SHARED_LIBS ON), which would set
# the global flag and silently turn window_engine into a DLL on a clean configure.
# Lock it off before the subdirectory is processed.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Windows first pass: WASAPI only. Avoid ASIO SDK friction.
set(RTAUDIO_API_WASAPI ON CACHE BOOL "" FORCE)
set(RTAUDIO_API_DS OFF CACHE BOOL "" FORCE)
set(RTAUDIO_API_ASIO OFF CACHE BOOL "" FORCE)

wz_add_subdirectory_quiet(external/audio/rtaudio)

# ----------------------------
# Core library target
# ----------------------------

set (YAML_VERSION_MAJOR 0)
set (YAML_VERSION_MINOR 2)
set (YAML_VERSION_PATCH 5)
set (YAML_VERSION_STRING "${YAML_VERSION_MAJOR}.${YAML_VERSION_MINOR}.${YAML_VERSION_PATCH}")

add_library(window_engine STATIC)

target_compile_definitions(window_engine PRIVATE
    
    YAML_DECLARE_STATIC
    YAML_DECLARE_STATIC
    YAML_VERSION_MAJOR=0
    YAML_VERSION_MINOR=2
    YAML_VERSION_PATCH=5
    YAML_VERSION_STRING="0.2.5"
)

target_sources(window_engine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/tinyexr/miniz.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/tinyexr/tinyexr_impl.cpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/external/json/yyjson/yyjson.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/json/json_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/json/json_polytree.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/json/json_writer.cpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/external/toml/toml_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/toml/toml_polytree.cpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/external/xml/pugixml/pugixml.cpp"

    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/api.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/dumper.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/emitter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/loader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/parser.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/scanner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/writer.c"


    $<TARGET_OBJECTS:wz_external_ply>
 )

target_include_directories(window_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/external/json/yyjson"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/xml/pugixml"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/toml/tomlplusplus"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/gltf/fastgltf/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/ply/wz_ply"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/tinyexr"
)

target_link_libraries(window_engine
PRIVATE
    rtaudio
    $<BUILD_INTERFACE:pmp>
    fastgltf::fastgltf
    dwmapi
    d3d12
    dxgi
    dxguid
    windowsapp
    xinput)

# New renderer (wozzits-rhi) — the engine depends on it (bridge topology A).
# Header-only INTERFACE library at a sibling path; disable its tests in this
# build. PUBLIC because engine headers (rhi_render_program_bridge.h) expose rhi
# types to consumers.
set(WOZZITS_RHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../wozzits-rhi"
    "${CMAKE_BINARY_DIR}/wozzits-rhi")
# BUILD_INTERFACE so the header-only dependency does not leak into the engine's
# install/export set (same idiom as pmp above).
target_link_libraries(window_engine PUBLIC $<BUILD_INTERFACE:wozzits_rhi>)

add_subdirectory(src)

target_include_directories(window_engine
   PUBLIC
       $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/window_engine>
       $<INSTALL_INTERFACE:include>
)

install(TARGETS window_engine
   EXPORT window_engineTargets
   ARCHIVE DESTINATION lib
   LIBRARY DESTINATION lib
   RUNTIME DESTINATION bin
)

install(DIRECTORY window_engine/api
   DESTINATION include/window_engine
)

install(EXPORT window_engineTargets
   FILE window_engineTargets.cmake
   NAMESPACE window_engine::
   DESTINATION lib/cmake/window_engine
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/window_engineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/window_engineConfig.cmake
    INSTALL_DESTINATION lib/cmake/window_engine
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/window_engineConfig.cmake
    DESTINATION lib/cmake/window_engine
)


if(WOZZITS_ENABLE_V8_SCRIPTING)
    set(WOZZITS_V8_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WOZZITS_V8_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../wozzits-v8"
        "${CMAKE_CURRENT_BINARY_DIR}/_deps/wozzits-v8")
endif()

# ----------------------------
# Shared resource staging
# ----------------------------
# Tests and apps that need runtime resources all run from CMAKE_BINARY_DIR, so
# they share ONE staged copy there. Routing every resource-needing target through
# a single staging target (instead of a per-target POST_BUILD copy_directory)
# fixes two problems with the old per-target copies:
#   - the parallel-build race where several targets copy into the same directory
#     at once (on Windows: a "Permission denied" sharing violation), and
#   - re-copying the whole resources tree (multiple GB: HDRIs + committed asset
#     caches) on every relink of every resource target.
# A stamp gates the copy so it runs only when a source resource actually
# changed: the stamp DEPENDS on every file under window_engine/resources
# (globbed with CONFIGURE_DEPENDS, so added/removed files re-run the glob),
# and the copy skips up-to-date files where the running CMake supports it.
# Deleting a source resource does not delete the staged copy; nuke
# ${CMAKE_BINARY_DIR}/resources if that ever matters.
file(GLOB_RECURSE WZ_RESOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/window_engine/resources/*")
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.26")
    set(WZ_RESOURCE_COPY_MODE copy_directory_if_different)
else()
    set(WZ_RESOURCE_COPY_MODE copy_directory)
endif()
set(WZ_RESOURCE_STAMP "${CMAKE_BINARY_DIR}/wz_resources.stamp")
add_custom_command(
    OUTPUT "${WZ_RESOURCE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E ${WZ_RESOURCE_COPY_MODE}
        "${CMAKE_CURRENT_SOURCE_DIR}/window_engine/resources"
        "${CMAKE_BINARY_DIR}/resources"
    COMMAND ${CMAKE_COMMAND} -E touch "${WZ_RESOURCE_STAMP}"
    DEPENDS ${WZ_RESOURCE_FILES}
    COMMENT "Staging window_engine resources (shared, changed files only)"
    VERBATIM
)
add_custom_target(wz_stage_resources DEPENDS "${WZ_RESOURCE_STAMP}")

# Self-contained, lightweight asset-graph fixture for the on-device rebind tests.
# It lives in the test tree (not the editable sample projects) so editor
# scratch work never breaks the tests, and it uses a procedural cube mesh
# instead of any external/heavy .glb. The graph is configured so the bundled
# shader paths are absolute (machine-correct) rather than CWD-relative.
set(WZ_REBIND_FIXTURE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/test_rebind_fixture")
set(WZ_REBIND_FIXTURE_DST
    "${CMAKE_BINARY_DIR}/resources/projects/test_rebind_fixture")
set(WZ_FIXTURE_SHADER_DIR "${WZ_REBIND_FIXTURE_SRC}/shaders")
configure_file(
    "${WZ_REBIND_FIXTURE_SRC}/assets.graph.json.in"
    "${WZ_REBIND_FIXTURE_DST}/assets.graph.json"
    @ONLY)
configure_file(
    "${WZ_REBIND_FIXTURE_SRC}/.wozzits/project.json"
    "${WZ_REBIND_FIXTURE_DST}/.wozzits/project.json"
    COPYONLY)
configure_file(
    "${WZ_REBIND_FIXTURE_SRC}/scene.json"
    "${WZ_REBIND_FIXTURE_DST}/scene.json"
    COPYONLY)
# Issue #213 increment 1b: a second scene for the test_rebind_fixture graph that
# drives renderables via geometry+program BINDINGS (reuses node 9 = geometry,
# node 10 = render program) instead of a pre-built renderable node. Lives beside
# the graph it references; consumed by the standalone-render binding test below.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/render_binding_scene.json"
    "${WZ_REBIND_FIXTURE_DST}/render_binding.scene.json"
    COPYONLY)

# Issue #229: a scene for the test_rebind_fixture graph that drives a CUSTOM
# (0x70A) renderable via scene-node ingredients — geometry (node 1 cube Mesh) +
# layout-authored custom program (node 16) + a semantic binding to the
# procedural scalar field (node 17) + a per-instance 'tint' constant override.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/custom_binding_scene.json"
    "${WZ_REBIND_FIXTURE_DST}/custom_binding.scene.json"
    COPYONLY)

# Issue #216/#217: a scene for the test_rebind_fixture graph that drives the
# collision-by-reference + motion terrain-stick authoring verbs (a landscape
# node + a tank node, both authored live by the test).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/collision_ref_scene.json"
    "${WZ_REBIND_FIXTURE_DST}/collision_ref.scene.json"
    COPYONLY)

# Self-contained fixture for the WozzitsApp_v1 behavior dispatch test: an empty
# asset graph + a scene with one node bound to the project behavior module
# "move_up_on_frame". The module DLL (behavior_module_test_plugin, defined with
# the other tests below) is staged into this fixture's behavior_module_folder so
# load_scene loads it. The manifest's behavior_module_folder is configured to
# that staged absolute path.
set(WZ_BEHAVIOR_FIXTURE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/behavior_test_fixture")
set(WZ_BEHAVIOR_FIXTURE_DST
    "${CMAKE_BINARY_DIR}/resources/projects/behavior_test_fixture")
set(WZ_BEHAVIOR_FIXTURE_MODULE_DIR
    "${WZ_BEHAVIOR_FIXTURE_DST}/behavior/build/clang-debug")
configure_file(
    "${WZ_BEHAVIOR_FIXTURE_SRC}/.wozzits/project.json.in"
    "${WZ_BEHAVIOR_FIXTURE_DST}/.wozzits/project.json"
    @ONLY)
configure_file(
    "${WZ_BEHAVIOR_FIXTURE_SRC}/assets.graph.json"
    "${WZ_BEHAVIOR_FIXTURE_DST}/assets.graph.json"
    COPYONLY)
configure_file(
    "${WZ_BEHAVIOR_FIXTURE_SRC}/scene.json"
    "${WZ_BEHAVIOR_FIXTURE_DST}/scene.json"
    COPYONLY)

# Self-contained fixture for the WozzitsApp_v1 prefab-spawn test (runtime prefab
# spawning, milestone #2): an empty asset graph + a scene with an "accumulator"
# node (accumulate_on_frame, instance-state probe for the state-preserving
# rebuild) and a "spawner" node (spawn_prefab_on_frame, emits SPAWN_PREFAB). It
# reuses the behavior fixture's staged module dir so the same test plugin DLL is
# loaded; the prefab itself is registered programmatically by the test.
set(WZ_PREFAB_SPAWN_FIXTURE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/prefab_spawn_fixture")
set(WZ_PREFAB_SPAWN_FIXTURE_DST
    "${CMAKE_BINARY_DIR}/resources/projects/prefab_spawn_fixture")
configure_file(
    "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/.wozzits/project.json.in"
    "${WZ_PREFAB_SPAWN_FIXTURE_DST}/.wozzits/project.json"
    @ONLY)
configure_file(
    "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/assets.graph.json"
    "${WZ_PREFAB_SPAWN_FIXTURE_DST}/assets.graph.json"
    COPYONLY)
configure_file(
    "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/scene.json"
    "${WZ_PREFAB_SPAWN_FIXTURE_DST}/scene.json"
    COPYONLY)
# A scenelet auto-registered as the "spawnling" prefab on scene load (the
# runtime-spawning glue): scenelets/<stem>.scene.json -> prefab "<stem>". Its
# root binds accumulate_on_frame, so a spawned instance's behavior runs.
configure_file(
    "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/scenelets/spawnling.scene.json"
    "${WZ_PREFAB_SPAWN_FIXTURE_DST}/scenelets/spawnling.scene.json"
    COPYONLY)

# Self-contained fixture for the WozzitsApp_v1 renderable-param test (issue
# #232): an empty asset graph + a scene with a "tinted" node that carries an
# authored renderable_constants "tint" override and a pulse_tint_on_frame
# behavior. Reuses the behavior fixture's staged module dir so the same test
# plugin DLL is loaded.
set(WZ_RENDERABLE_PARAM_FIXTURE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/renderable_param_fixture")
set(WZ_RENDERABLE_PARAM_FIXTURE_DST
    "${CMAKE_BINARY_DIR}/resources/projects/renderable_param_fixture")
configure_file(
    "${WZ_RENDERABLE_PARAM_FIXTURE_SRC}/.wozzits/project.json.in"
    "${WZ_RENDERABLE_PARAM_FIXTURE_DST}/.wozzits/project.json"
    @ONLY)
configure_file(
    "${WZ_RENDERABLE_PARAM_FIXTURE_SRC}/assets.graph.json"
    "${WZ_RENDERABLE_PARAM_FIXTURE_DST}/assets.graph.json"
    COPYONLY)
configure_file(
    "${WZ_RENDERABLE_PARAM_FIXTURE_SRC}/scene.json"
    "${WZ_RENDERABLE_PARAM_FIXTURE_DST}/scene.json"
    COPYONLY)

# Self-contained fixture for the WozzitsApp_v1 GLB scene-source test (issue
# #213): an empty asset graph + a scene whose host node carries a
# glb_scene_source DESCRIPTOR pointing at the bundled gltf/tank1.glb (staged by
# wz_stage_resources). The descriptor is resolved at materialization into a
# create_scene_from_glb Scene asset and grafted as instanced children. Two
# scenes are staged: scene.json (instance mode + per-component styles) and
# scene_flatten.json (flatten/bake mode). It reuses the behavior fixture's
# staged module dir so a behavior can be bound to a grafted child to prove
# addressability.
set(WZ_GLB_SCENE_SOURCE_FIXTURE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/glb_scene_source_fixture")
set(WZ_GLB_SCENE_SOURCE_FIXTURE_DST
    "${CMAKE_BINARY_DIR}/resources/projects/glb_scene_source_fixture")
configure_file(
    "${WZ_GLB_SCENE_SOURCE_FIXTURE_SRC}/.wozzits/project.json.in"
    "${WZ_GLB_SCENE_SOURCE_FIXTURE_DST}/.wozzits/project.json"
    @ONLY)
configure_file(
    "${WZ_GLB_SCENE_SOURCE_FIXTURE_SRC}/assets.graph.json"
    "${WZ_GLB_SCENE_SOURCE_FIXTURE_DST}/assets.graph.json"
    COPYONLY)
configure_file(
    "${WZ_GLB_SCENE_SOURCE_FIXTURE_SRC}/scene.json"
    "${WZ_GLB_SCENE_SOURCE_FIXTURE_DST}/scene.json"
    COPYONLY)
configure_file(
    "${WZ_GLB_SCENE_SOURCE_FIXTURE_SRC}/scene_flatten.json"
    "${WZ_GLB_SCENE_SOURCE_FIXTURE_DST}/scene_flatten.json"
    COPYONLY)

# Self-contained, fully portable fixture for the WozzitsApp_v1 terrain-constraint
# tick test (issue #216 work item B). An empty asset graph + a scene with a
# heightfield terrain whose height field is an inline PROCEDURAL scalar field
# (radial gradient, no external file), a constrain_movement constraint surface,
# and two motion actors above the surface: one terrain_constrained (sticks) and
# one control (does not). Proves the tick runs build_collision_frame +
# apply_terrain_constraints even with no behaviors authored.
set(WZ_TERRAIN_CONSTRAINT_FIXTURE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/terrain_constraint_fixture")
set(WZ_TERRAIN_CONSTRAINT_FIXTURE_DST
    "${CMAKE_BINARY_DIR}/resources/projects/terrain_constraint_fixture")
configure_file(
    "${WZ_TERRAIN_CONSTRAINT_FIXTURE_SRC}/.wozzits/project.json"
    "${WZ_TERRAIN_CONSTRAINT_FIXTURE_DST}/.wozzits/project.json"
    COPYONLY)
configure_file(
    "${WZ_TERRAIN_CONSTRAINT_FIXTURE_SRC}/assets.graph.json"
    "${WZ_TERRAIN_CONSTRAINT_FIXTURE_DST}/assets.graph.json"
    COPYONLY)
configure_file(
    "${WZ_TERRAIN_CONSTRAINT_FIXTURE_SRC}/scene.json"
    "${WZ_TERRAIN_CONSTRAINT_FIXTURE_DST}/scene.json"
    COPYONLY)

# ----------------------------
# Test helpers
# ----------------------------

function(rs_add_test_group category name)
    set(target "${category}_${name}")

    add_executable(${target}
        tests/${category}/${name}.cpp)

    target_include_directories(${target}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/external/ply/wz_ply"
    )

    set_target_properties(${target}
        PROPERTIES FOLDER "tests/${category}"
    )

    target_compile_definitions(${target}
        PRIVATE SCENE_RENDER_TEST=1
        WZ_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )

    target_link_libraries(${target}
        PRIVATE
            gtest
            GTest::gtest_main
            window_engine
    )

    set(WZ_TESTS_REQUIRING_RESOURCES
    test_window
    test_mesh_wireframe
    test_gaussian_splat
    game_app_main
    rhi_scene_renderer_rebind_tests
    )

    if(name IN_LIST WZ_TESTS_REQUIRING_RESOURCES)
        add_dependencies(${target} wz_stage_resources)
    endif()


    add_test(NAME ${target} COMMAND ${target})

endfunction()

# ----------------------------
# Tests
# ----------------------------

if(WINDOW_ENGINE_BUILD_EXAMPLES)
    add_subdirectory(examples/behavior_plugin_template)

    add_executable(wozzits_rtaudio_smoke
        examples/rtaudio/main.cpp
    )

    target_link_libraries(wozzits_rtaudio_smoke
        PRIVATE
            window_engine
    )

    add_executable(rhi_pull_cube
        examples/rhi_pull_cube/main.cpp
    )

    target_link_libraries(rhi_pull_cube
        PRIVATE
            window_engine
    )

    find_program(WZ_FXC_EXECUTABLE
        NAMES fxc.exe
        HINTS
            "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
            "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64"
    )
    if(WZ_FXC_EXECUTABLE)
        add_custom_command(TARGET rhi_pull_cube POST_BUILD
            COMMAND "${WZ_FXC_EXECUTABLE}" /nologo /T vs_5_1 /E main
                /Fo "$<TARGET_FILE_DIR:rhi_pull_cube>/pull_cube_vs.cso"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/rhi_pull_cube/shaders/pull_cube_vs.hlsl"
            COMMAND "${WZ_FXC_EXECUTABLE}" /nologo /T ps_5_1 /E main
                /Fo "$<TARGET_FILE_DIR:rhi_pull_cube>/pull_cube_ps.cso"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/rhi_pull_cube/shaders/pull_cube_ps.hlsl"
            COMMENT "Compiling rhi_pull_cube HLSL"
        )
    else()
        message(WARNING "fxc.exe not found; rhi_pull_cube will build but needs pull_cube_vs.cso and pull_cube_ps.cso next to the executable.")
    endif()
endif()

# ----------------------------
# wozzits_app_v1 — RHI-native app (Atom "Bootstrap" role): owns window + device
# and drives a WozzitsApp_v1. Built unconditionally; this is the app, not an
# example. WozzitsApp_v1 itself lives in the window_engine library (so the
# editor can compose it too); only the thin main.cpp driver is in this target.
# ----------------------------
add_executable(wozzits_app_v1
    src/app/wozzits_app_v1/main.cpp
)

target_link_libraries(wozzits_app_v1
    PRIVATE
        window_engine
)

# Stage resources (shared, once) so runtime project manifests can find scenes.
add_dependencies(wozzits_app_v1 wz_stage_resources)

# Editor "Launch app" helper: resolves a project root to its runtime paths and
# spawns wozzits_app_v1 as a separate process (isolated test run), relaying its
# logs. The editor spawns this; it is NOT used for authoring (that stays the
# in-process C ABI). See Option Y / issue #189.
add_executable(wozzits_editor_host
    src/tools/wozzits_editor_host/main.cpp
)

target_link_libraries(wozzits_editor_host
    PRIVATE
        window_engine
)

add_dependencies(wozzits_editor_host wozzits_app_v1)

add_library(wozzits_abi SHARED
    src/engine/abi/wozzits_abi.cpp
)

set_target_properties(wozzits_abi
    PROPERTIES
        DEBUG_POSTFIX ""
)

target_compile_definitions(wozzits_abi
    PRIVATE
        WZ_ABI_EXPORTS
)

target_link_libraries(wozzits_abi
    PRIVATE
        window_engine
)

# ----------------------------
# "Here is the app" install component
# ----------------------------
# A dedicated 'app' install component that copies ONLY the three runtime
# binaries -- the RHI-native app, the play-mode host, and the in-process ABI
# dll -- into one flat folder. rtaudio is static-linked and DX12/DXGI are
# system dlls, so these three files are the entire non-system payload; there is
# nothing transitive to bundle. Kept in its own component so it never drags in
# the dev-SDK install (window_engine lib + headers + cmake export) defined
# above.
#
#   Install:      cmake --install <build-dir> --component app
#   Change dest:  reconfigure with -DWOZZITS_APP_INSTALL_DIR=<path>
#
# The editor publishes its own output (Wozzits.Editor.App.exe + Avalonia deps)
# into this SAME folder. Because the editor derives the play-mode host exe from
# the directory of the wozzits_abi.dll it loads, and the host finds
# wozzits_app_v1.exe beside itself, co-locating all four makes the whole
# live/play chain resolve to this one folder with no build-tree scan.
#
# DESTINATION is absolute (a fixed local app folder chosen via the cache var),
# so `cmake --install --component app` writes here regardless of the prefix used
# for the dev-SDK install. Only RUNTIME artifacts are listed: the wozzits_abi
# import .lib (ARCHIVE) is a build-time artifact and is intentionally not shipped.
set(WOZZITS_APP_INSTALL_DIR "D:/wozzits-app" CACHE PATH
    "Destination folder for the 'app' install component (the runnable app).")
install(TARGETS wozzits_app_v1 wozzits_editor_host wozzits_abi
    RUNTIME DESTINATION "${WOZZITS_APP_INSTALL_DIR}"
    COMPONENT app)

if(WINDOW_ENGINE_BUILD_EXAMPLES AND WOZZITS_ENABLE_V8_SCRIPTING)
    add_executable(v8_script_smoke
        examples/v8_script_smoke/v8_script_smoke.cpp
     )

    target_link_libraries(v8_script_smoke
        PRIVATE
            wozzits_v8::wozzits_v8
    )

    target_compile_features(v8_script_smoke PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(v8_script_smoke
            PRIVATE
                /Zc:__cplusplus
        )
    endif()
endif()

if(WINDOW_ENGINE_ENABLE_TESTS)
    target_compile_definitions(window_engine PUBLIC WZ_ENABLE_TESTING)

    rs_add_test_group(engine engine)
    rs_add_test_group(engine frame_storage_tests)
    rs_add_test_group(engine runtime_camera_tests)
    rs_add_test_group(engine flying_camera_tests)
    rs_add_test_group(engine project_manifest_tests)
    rs_add_test_group(engine asset_graph_editor_session_tests)
    rs_add_test_group(engine asset_graph_schema_registry_tests)
    rs_add_test_group(engine asset_catalog_tests)
    rs_add_test_group(engine asset_graph_json_roundtrip_tests)
    rs_add_test_group(engine editor_runtime_control_tests)

    # Strict-isolation render check (issue #213): launch the standalone
    # wozzits_app_v1 executable as a SEPARATE PROCESS on the portable render
    # fixture and assert it renders (bounded --frames run, exit 0). This is the
    # boundary guard for editor-authored scenes — no editor/test code in the
    # render path — and it exercises the same binary that becomes the shippable
    # app. Skips (not fails) when no GPU device is available
    # (kRuntimeNoDeviceExitCode = 3).
    add_test(NAME wozzits_app_v1_standalone_render
        COMMAND wozzits_app_v1
            --resource-root "${CMAKE_BINARY_DIR}/resources"
            --asset-graph "${WZ_REBIND_FIXTURE_DST}/assets.graph.json"
            --scene "${WZ_REBIND_FIXTURE_DST}/scene.json"
            --frames 5)
    set_tests_properties(wozzits_app_v1_standalone_render PROPERTIES
        SKIP_RETURN_CODE 3
        LABELS "render")

    # Issue #213 increment 1b: same separate-process harness, but the scene drives
    # its renderables via geometry+program BINDINGS (the engine assembles the
    # recipe at load; the render program is inherited down the scene tree). Proves
    # the binding renders standalone. Skips on no-GPU (exit 3).
    add_test(NAME wozzits_app_v1_render_binding
        COMMAND wozzits_app_v1
            --resource-root "${CMAKE_BINARY_DIR}/resources"
            --asset-graph "${WZ_REBIND_FIXTURE_DST}/assets.graph.json"
            --scene "${WZ_REBIND_FIXTURE_DST}/render_binding.scene.json"
            --frames 5)
    set_tests_properties(wozzits_app_v1_render_binding PROPERTIES
        SKIP_RETURN_CODE 3
        LABELS "render")

    rs_add_test_group(behavior behavior_registry_core_tests)
    rs_add_test_group(behavior event_channels)
    rs_add_test_group(behavior behavior_plugin_dynamic_module_tests)
    rs_add_test_group(behavior behavior_dispatch_tests)
    rs_add_test_group(behavior behavior_init_tests)
    rs_add_test_group(behavior behavior_descendant_finder_tests)
    rs_add_test_group(behavior behavior_input_dispatch_tests)
    rs_add_test_group(behavior behavior_input_event_api_tests)
    rs_add_test_group(behavior behavior_module_api_tests)
    rs_add_test_group(behavior behavior_commands_motion_tests)
    rs_add_test_group(behavior behavior_adversarial_tests)
    rs_add_test_group(behavior behavior_commands_transform_tests)
    rs_add_test_group(behavior behavior_scene_component_tests)
    rs_add_test_group(behavior scene_camera_behavior_tests)
    rs_add_test_group(behavior prefab_spawner_behavior_tests)
    rs_add_test_group(behavior drive_forward_behavior_tests)
    rs_add_test_group(behavior terrain_align_behavior_tests)
    rs_add_test_group(behavior cognition_tick_dispatch_tests)
    rs_add_test_group(behavior quantum_agent_behavior_tests)
    rs_add_test_group(behavior quantum_agent_actuator_tests)
    rs_add_test_group(behavior module_params_tests)
    rs_add_test_group(cognition qstate_tests)
    rs_add_test_group(cognition mean_field_tests)
    rs_add_test_group(cognition loopy_bp_tests)
    rs_add_test_group(cognition exact_group_tests)
    rs_add_test_group(cognition anneal_tests)
    rs_add_test_group(cognition goals_tests)
    rs_add_test_group(cognition commit_tests)
    rs_add_test_group(cognition agent_layout_tests)
    rs_add_test_group(cognition learning_tests)
    rs_add_test_group(cognition exclusivity_tests)
    rs_add_test_group(cognition sequential_goals_tests)
    rs_add_test_group(cognition conditional_policy_tests)
    rs_add_test_group(cognition tree_bp_tests)
    rs_add_test_group(cognition ttn_tests)
    rs_add_test_group(cognition tree_tn_tests)
    rs_add_test_group(cognition group_topology_tests)
    rs_add_test_group(cognition village_tests)
    rs_add_test_group(cognition coordination_tests)
    rs_add_test_group(cognition cognition_clock_tests)
    rs_add_test_group(cognition agent_cognition_tests)

    add_library(behavior_dynamic_test_plugin SHARED
        tests/behavior/dynamic_behavior_plugin.cpp)
    target_include_directories(behavior_dynamic_test_plugin
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/window_engine")
    target_compile_features(behavior_dynamic_test_plugin PRIVATE cxx_std_20)
    add_dependencies(behavior_behavior_plugin_dynamic_module_tests
        behavior_dynamic_test_plugin)

    add_library(behavior_dynamic_missing_symbol_plugin SHARED
        tests/behavior/dynamic_behavior_missing_symbol.cpp)
    add_dependencies(behavior_behavior_plugin_dynamic_module_tests
        behavior_dynamic_missing_symbol_plugin)

    target_compile_definitions(behavior_behavior_plugin_dynamic_module_tests PRIVATE
        WZ_TEST_BEHAVIOR_PLUGIN_DLL="$<TARGET_FILE:behavior_dynamic_test_plugin>"
        WZ_TEST_BEHAVIOR_MISSING_SYMBOL_DLL="$<TARGET_FILE:behavior_dynamic_missing_symbol_plugin>")

    rs_add_test_group(logging logging)
    rs_add_test_group(logging logger_queue_tests)
    rs_add_test_group(logging threaded_logger_tests)

    rs_add_test_group(jobs job_graph_tests)
    # rs_add_test_group(jobs render_prep_job_tests)
    rs_add_test_group(jobs profiler_tests)
    rs_add_test_group(jobs engine_adversarial_tests)
    rs_add_test_group(engine input_events)



    rs_add_test_group(asset new_asset_test_1)
    rs_add_test_group(asset engine_asset_library)
    rs_add_test_group(asset scalar_field_tests)
    rs_add_test_group(asset vector_field_tests)
    rs_add_test_group(asset scalar_field_procedural_tests)
    rs_add_test_group(asset scalar_field_texture_test)
    rs_add_test_group(asset scalar_field_rhi_residency_test)
    rs_add_test_group(asset scalar_field_mip_pyramid_tests)
    rs_add_test_group(asset gaussian_splat_cloud_rhi_residency_test)
    rs_add_test_group(asset scalar_field_set_tests)
    rs_add_test_group(asset foundational_assets)
    rs_add_test_group(asset key_utils)
    rs_add_test_group(asset asset_graph_draft)
    rs_add_test_group(asset param_defaults)
    rs_add_test_group(asset asset_graph_authoring)
    rs_add_test_group(asset csv_tests)
    rs_add_test_group(asset engine_disk_cache_provider_tests)

    rs_add_test_group(asset_json yyjson_wrapper)
    rs_add_test_group(asset_json json_polytree)
    rs_add_test_group(asset_json json_table)
    rs_add_test_group(asset_json json_asset_module)

    rs_add_test_group(asset_toml toml_parser)
    rs_add_test_group(asset_toml toml_polytree)
    rs_add_test_group(asset_toml toml_asset_module)

    rs_add_test_group(asset_audio wav_decoder_tests)
    rs_add_test_group(asset_audio audio_clip_tests)
    rs_add_test_group(asset_audio audio_clip_bank_tests)
    rs_add_test_group(asset_audio audio_renderable_tests)
    rs_add_test_group(asset_audio scene_audio_player_tests)

    rs_add_test_group(audio voice_tests)
    rs_add_test_group(audio grain_cloud_tests)
    rs_add_test_group(audio mixer_tests)
    rs_add_test_group(audio audio_scheduler_tests)
    rs_add_test_group(audio audio_output_tests)

    rs_add_test_group(asset_mesh mesh_table)
    rs_add_test_group(asset_mesh mesh_key_factory)
    rs_add_test_group(asset_mesh mesh_asset_module_interface)
    rs_add_test_group(asset_mesh procedural_mesh)
    rs_add_test_group(asset_mesh clipmap_lattice_mesh)
    rs_add_test_group(asset_mesh mesh_asset_module)
    rs_add_test_group(asset_mesh_derived_field mesh_derived_field_asset_module)
    rs_add_test_group(asset_mesh_sparse_operator mesh_sparse_operator_asset_module)
    rs_add_test_group(asset_gpu_sparse_mesh gpu_sparse_mesh_asset_module)
    rs_add_test_group(mesh_processing mesh_processing_pmp)

    rs_add_test_group(asset_terrain terrain_asset_module)
    rs_add_test_group(asset_terrain terrain_fixture_assets)
    rs_add_test_group(asset_terrain terrain_lod_seams)
    rs_add_test_group(asset_terrain terrain_visual_proxy_asset_module)

    rs_add_test_group(asset_collision collision_asset_module)
    rs_add_test_group(collision proximity_frame_tests)
    rs_add_test_group(collision collision_frame_core_tests)
    rs_add_test_group(collision collision_surface_sampling_tests)
    rs_add_test_group(collision collision_frame_world_builder_tests)
    rs_add_test_group(collision collision_frame_job_tests)
    rs_add_test_group(collision collision_frame_adversarial_tests)
    rs_add_test_group(collision collision_frame_adversarial_more_tests)

    rs_add_test_group(asset_gaussian_splat gaussian_splat_asset_module)
    rs_add_test_group(asset_gaussian_splat ply_gaussian_splat)
    rs_add_test_group(asset_gaussian_splat ply_reader)
    rs_add_test_group(asset_gaussian_splat gaussian_splat_ply_importer)
    rs_add_test_group(asset_gaussian_splat gaussian_splat_from_scalar_field)
    rs_add_test_group(asset_gaussian_splat gaussian_splat_color_lod_compiler)
    rs_add_test_group(asset_gaussian_splat terrain_splat_from_gaea_r32_compiler)
    rs_add_test_group(asset_gaussian_splat terrain_normal_smoothing)
    rs_add_test_group(asset_gaussian_splat terrain_surface_frame_handedness)
    rs_add_test_group(asset_gaussian_splat terrain_surface_coverage)
    rs_add_test_group(asset_gaussian_splat terrain_splat_field_recipe)
    rs_add_test_group(asset_gaussian_splat terrain_splat_geometry_fields)
    rs_add_test_group(asset_gaussian_splat gaussian_splat_decode)
    rs_add_test_group(asset_gaussian_splat terrain_reconstruction)

    rs_add_test_group(gpu gpu_mesh)
    rs_add_test_group(gpu device_status)
    rs_add_test_group(gpu dx12_descriptor_allocator)
    rs_add_test_group(gpu texture_mip)
    rs_add_test_group(gpu compute_dispatch)
    rs_add_test_group(gpu shader_source_combine)

    rs_add_test_group(asset_data_table data_table)
    rs_add_test_group(asset_data_table data_table_asset_module)
    rs_add_test_group(asset_diagnostics diagnostic_resampled_time_series)
    rs_add_test_group(asset_diagnostics diagnostic_resampled_time_series_asset_module)
    rs_add_test_group(asset_diagnostics diagnostic_resampled_time_series_table_view)
    rs_add_test_group(asset_diagnostics diagnostic_resampled_time_series_csv_chain)
    rs_add_test_group(asset_diagnostics diagnostic_timeframe_summary)
    rs_add_test_group(asset_diagnostics diagnostic_timeframe_summary_asset_module)
    rs_add_test_group(asset_diagnostics diagnostic_timeframe_summary_csv_chain)

    rs_add_test_group(asset_csv_export csv_export)
    rs_add_test_group(asset_csv_export csv_export_asset_module)

    rs_add_test_group(asset_render_style mesh)

    rs_add_test_group(asset_renderable renderable_table)
    rs_add_test_group(asset_renderable renderable_mesh_tests)
    rs_add_test_group(asset_renderable renderable_field_splat_tests)
    rs_add_test_group(asset_renderable renderable_terrain_tests)
    rs_add_test_group(asset_renderable custom_renderable_tests)

    rs_add_test_group(asset_render_program render_program_asset_module)
    rs_add_test_group(asset_render_program render_binding_layout_program_tests)
    rs_add_test_group(asset_render_binding_layout render_binding_layout_asset_module)
    rs_add_test_group(asset_compute_pipeline compute_pipeline_asset_module)
    rs_add_test_group(asset_compute_pipeline hlsl_binding_extract)

    rs_add_test_group(asset_light light_asset_module)

    rs_add_test_group(asset_hdri hdri_image_loader)

    rs_add_test_group(asset_scene scene_authoring)
    rs_add_test_group(asset_scene scene_asset_compile_tests)
    rs_add_test_group(asset_scene scene_source_expansion_tests)
    rs_add_test_group(asset_scene scene_instantiation_transform_tests)
    rs_add_test_group(asset_scene scene_runtime_component_roundtrip_tests)
    rs_add_test_group(asset_scene scene_audio_source_roundtrip_tests)
    rs_add_test_group(asset_scene scene_renderable_binding_roundtrip_tests)
    rs_add_test_group(asset_scene scene_audio_authoring_tests)
    rs_add_test_group(asset_scene scene_descriptor_validation_tests)
    rs_add_test_group(asset_scene scene_editor_visual_tests)
    rs_add_test_group(asset_scene scene_renderable_reference_tests)
    rs_add_test_group(asset_scene scene_resource_resolver_tests)
    rs_add_test_group(asset_scene gpu_scene_resource_resolver_tests)
    rs_add_test_group(asset_scene scene_runtime_build_tests)
    rs_add_test_group(asset_scene scene_ecs_construction_tests)
    rs_add_test_group(asset_scene scene_ecs_boundary_tests)
    rs_add_test_group(asset_scene scene_authoring_component_roundtrip_tests)
    rs_add_test_group(asset_scene scene_editor_camera_metadata_tests)
    rs_add_test_group(asset_scene scene_subtree_export_tests)
    rs_add_test_group(asset_scene prefab_instantiate_tests)
    rs_add_test_group(asset_scene scene_compute_kernel_roundtrip_tests)
    rs_add_test_group(asset_scene scene_mesh_compute_field_roundtrip_tests)
    rs_add_test_group(asset_scene scene_render_shader_roundtrip_tests)
    rs_add_test_group(asset_scene scene_materialize_compute_kernel_tests)
    rs_add_test_group(asset_scene scene_materialize_mesh_compute_field_tests)
    rs_add_test_group(asset_scene scene_materialize_render_shader_tests)
    rs_add_test_group(asset_scene scene_materialize_mesh_import_tests)
    rs_add_test_group(asset_scene scene_materialize_field_sky_tests)
    rs_add_test_group(asset_scene scene_materialize_light_tests)
    rs_add_test_group(asset_scene scene_materialize_terrain_tests)
    rs_add_test_group(asset_scene scene_materialize_default_scene_tests)

    rs_add_test_group(asset_gltf gltf_importer)
    # rs_add_test_group(game_app game_app_main)

    # Containers
    rs_add_test_group(containers mpsc_queue)
    rs_add_test_group(containers mpsc_ring_buffer)
    rs_add_test_group(containers spsc_queue)
    rs_add_test_group(containers buffer)
    rs_add_test_group(containers buffer_2)
    rs_add_test_group(containers buffer_spec_test)

    # Math
    rs_add_test_group(math test_vec3)
    rs_add_test_group(math test_mat4)
    rs_add_test_group(math test_quaternion)
    rs_add_test_group(math test_quaternion_mat4)
    rs_add_test_group(math test_camera)
    rs_add_test_group(math algo_math_spec)
    rs_add_test_group(math screen_space_metrics)

    # Graph
    rs_add_test_group(graph static_dag_tests_0)
    rs_add_test_group(graph static_dag_algo_tests_0)
    rs_add_test_group(graph static_polytree_tests_0)
    rs_add_test_group(graph static_polytree_algo_tests_0)
    rs_add_test_group(graph graph_problems_tests_0)
    rs_add_test_group(graph shared_edge_polytree_tests)
    rs_add_test_group(graph shared_edge_polytree_algo_tests)
    rs_add_test_group(graph shared_edge_graph_tests)

    # Scene render
    rs_add_test_group(scene_graph scene_graph_test_1)
    rs_add_test_group(render render_ir_test_3)
    rs_add_test_group(render render_ir_frustum_test_1)
    rs_add_test_group(render terrain_diagnostics)
    rs_add_test_group(render scene_node_world_tests)
    rs_add_test_group(render stub_backend_test_4)
    rs_add_test_group(render storage_reuse_tests)
    rs_add_test_group(render render_frame_sectioned_test_1)
    rs_add_test_group(render pipeline_benchmark_test_1)
    rs_add_test_group(render rhi_dx12_pipeline_tests)
    rs_add_test_group(render rhi_dx12_compute_dispatch_tests)
    rs_add_test_group(render rhi_dx12_command_recorder_tests)
    rs_add_test_group(render rhi_render_program_bridge_tests)
    rs_add_test_group(render rhi_dummy_backend_integration_tests)
    rs_add_test_group(render rhi_gpu_backend_tests)
    rs_add_test_group(render rhi_scene_renderer_rebind_tests)
    rs_add_test_group(render rhi_gaussian_splat_cloud_render_tests)
    rs_add_test_group(render clipmap_view_tests)
    rs_add_test_group(render wozzits_app_behavior_dispatch_tests)

    # Project behavior-module DLL for the WozzitsApp_v1 behavior dispatch test.
    # Built like the behavior unit-test plugins, then staged into the behavior
    # fixture's behavior_module_folder so load_scene loads it at runtime.
    add_library(wozzits_app_behavior_test_plugin SHARED
        tests/render/fixtures/behavior_module_test_plugin.cpp)
    target_include_directories(wozzits_app_behavior_test_plugin
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/window_engine")
    target_compile_features(wozzits_app_behavior_test_plugin PRIVATE cxx_std_20)
    add_custom_command(TARGET wozzits_app_behavior_test_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${WZ_BEHAVIOR_FIXTURE_MODULE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:wozzits_app_behavior_test_plugin>"
            "${WZ_BEHAVIOR_FIXTURE_MODULE_DIR}/"
        COMMENT "Staging behavior module DLL into behavior_test_fixture")
    add_dependencies(render_wozzits_app_behavior_dispatch_tests
        wozzits_app_behavior_test_plugin wz_stage_resources)

    # WozzitsApp_v1 GLB scene-source test (issue #213). On-device: it loads the
    # glb_scene_source_fixture, so it needs the staged resources (tank1.glb) and
    # the behavior module DLL (to prove a grafted child is behavior-addressable).
    rs_add_test_group(render wozzits_app_glb_scene_source_tests)
    add_dependencies(render_wozzits_app_glb_scene_source_tests
        wozzits_app_behavior_test_plugin wz_stage_resources)
    rs_add_test_group(render wozzits_app_render_binding_tests)
    add_dependencies(render_wozzits_app_render_binding_tests wz_stage_resources)

    # WozzitsApp_v1 runtime prefab-spawn test (prefab-system milestone #2). On-
    # device: loads the prefab_spawn_fixture and reuses the staged behavior module
    # DLL (accumulate_on_frame + spawn_prefab_on_frame); proves a state-preserving
    # behavior-scene rebuild, a SPAWN_PREFAB graft whose behavior runs, and that a
    # spawn does not flip the active scene camera (#219).
    rs_add_test_group(render wozzits_app_prefab_spawn_tests)
    add_dependencies(render_wozzits_app_prefab_spawn_tests
        wozzits_app_behavior_test_plugin wz_stage_resources)

    # WozzitsApp_v1 renderable-param behavior verb test (issue #232). On-device:
    # loads the renderable_param_fixture and reuses the staged behavior module
    # DLL (pulse_tint_on_frame); proves a behavior animates an authored
    # renderable constant through SET_RENDERABLE_PARAM and that the override
    # survives a structural rebuild.
    rs_add_test_group(render wozzits_app_renderable_param_tests)
    add_dependencies(render_wozzits_app_renderable_param_tests
        wozzits_app_behavior_test_plugin wz_stage_resources)

    # WozzitsApp_v1 terrain-constraint tick test (issue #216 work item B). On-
    # device: loads the portable terrain_constraint_fixture (procedural scalar
    # field + heightfield terrain constraint + motion actors); proves the tick
    # runs build_collision_frame + apply_terrain_constraints so a
    # terrain_constrained actor sticks to the surface with no behaviors authored.
    rs_add_test_group(render wozzits_app_terrain_constraint_tests)
    add_dependencies(render_wozzits_app_terrain_constraint_tests wz_stage_resources)

    # WozzitsApp_v1 collision-by-reference + motion terrain-stick authoring test
    # (issue #216/#217). On-device: loads test_rebind_fixture's graph paired with
    # collision_ref.scene.json; proves set_node_collision_asset bridges the
    # reference to the graph node's key + set_node_motion_terrain_fields sets the
    # terrain fields, and both persist through save_scene + a fresh-AppContext
    # reload.
    rs_add_test_group(render wozzits_app_collision_motion_authoring_tests)
    add_dependencies(render_wozzits_app_collision_motion_authoring_tests
        wz_stage_resources)
    rs_add_test_group(scene_compile scene_compiler_test_2)
    rs_add_test_group(scene_compile scene_compiler_tests_3)
    rs_add_test_group(scene_compile scene_metadata_test_1)
    rs_add_test_group(scene_compile scene_ecs_boundary_test_1)
    rs_add_test_group(scene_compile scene_refresh_test_1)
    rs_add_test_group(scene_compile scene_transform_update_test_1)
    rs_add_test_group(scene_compile scene_descriptor_update_test_1)
    rs_add_test_group(scene_compile scene_transform_dirty_filter_test_1)
    rs_add_test_group(scene_compile terrain_lod_selector)
    rs_add_test_group(scene_compile scene_adversarial_test_1)
    rs_add_test_group(scene_compile scene_adversarial_test_2)
    rs_add_test_group(scene_compile scene_adversarial_test_3)

    # ── BenchmarkApp ──────────────────────────────────────────────────────────
    # Standalone interactive app — no gtest dependency, resources required.

    set(BENCHMARK_APP_TARGET benchmark_app_benchmark_app_main)

    add_executable(${BENCHMARK_APP_TARGET}
        tests/benchmark_app/benchmark_app_main.cpp
        "src/event/event.cpp"
    )

    target_include_directories(${BENCHMARK_APP_TARGET}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/external/ply/wz_ply"
    )

    set_target_properties(${BENCHMARK_APP_TARGET}
        PROPERTIES FOLDER "tests/benchmark_app"
    )

    target_link_libraries(${BENCHMARK_APP_TARGET}
        PRIVATE
            window_engine
    )

    add_dependencies(${BENCHMARK_APP_TARGET} wz_stage_resources)

endif()
