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

# Fail a test that manufactures a NaN or an infinity, rather than only reporting
# it (tests/support/fp_status_listener.cpp). ON because the tree is clean: the
# listener landed reporting-only, found 32 tests raising hard IEEE exceptions --
# one of them a live culling bug -- and all 32 are closed.
#
# This repo has no CI, so `ctest` IS the automated path and the gate lives on the
# test property rather than in a pipeline file. Running a test executable
# DIRECTLY is unaffected, which is what you want while debugging one; the gate
# applies to the run that is supposed to be authoritative.
#
# Turn it off for a bisect or a spike with -DWZ_FP_STRICT_TESTS=OFF. If it starts
# failing, the honest fixes are to repair the arithmetic or -- when the test feeds
# hostile input on purpose -- to declare it with wz::testing::ExpectFpException.
option(WZ_FP_STRICT_TESTS
    "ctest fails a test that raises FE_INVALID / FE_DIVBYZERO / FE_OVERFLOW" ON)

# Per-test wall-clock ceiling. Without one ctest applies its 1500s default, so a
# LIVENESS bug -- a counter that never drains, a claim() with no publish(), a
# thief spinning on a deque that never yields -- shows up as a 25-minute hang
# rather than a failure, and reads as "the suite is slow today" instead of "the
# suite found something". Every concurrency suite here also contains unbounded
# `while` spins waiting for another thread, which is exactly the shape that
# hangs. These values are ceilings for a pathological run, not budgets: the
# slowest unit suite is well under 10s in Debug.
set(WZ_TEST_TIMEOUT_SECONDS 120 CACHE STRING
    "ctest per-test timeout (seconds) for the unit-test groups")
# The standalone/export tests spawn processes, compile shaders, and drive a real
# device, so they get a wider ceiling.
set(WZ_RENDER_TEST_TIMEOUT_SECONDS 300 CACHE STRING
    "ctest per-test timeout (seconds) for the standalone render/export tests")


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

# fastgltf's cmake/compilers.cmake branches on `if (MSVC)`, and CMake sets MSVC
# TRUE for clang-cl as well -- it targets the MSVC ABI. So a clang-cl build takes
# the MSVC arm and is handed `/external:W0 /external:anglebrackets`. clang-cl
# accepts the first and does not implement the second, so every fastgltf TU
# reported it as an argument it never acted on: 4 lines, the only warnings left
# in a clean build.
#
# Dropped here rather than by editing the vendored compilers.cmake, matching the
# pmp treatment below -- a patch to third-party source is lost on the next
# update, and this list edit is a no-op if fastgltf ever fixes the branch or
# stops passing the flag. `/external:W0` is deliberately left in place: clang-cl
# takes it without complaint and it is what keeps simdjson's headers quiet.
if(TARGET fastgltf AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    get_target_property(WZ_FASTGLTF_OPTS fastgltf COMPILE_OPTIONS)
    if(WZ_FASTGLTF_OPTS)
        list(REMOVE_ITEM WZ_FASTGLTF_OPTS "/external:anglebrackets")
        set_target_properties(fastgltf
            PROPERTIES COMPILE_OPTIONS "${WZ_FASTGLTF_OPTS}")
    endif()
    unset(WZ_FASTGLTF_OPTS)
endif()

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

# rtaudio_c.cpp calls strncpy, which the MSVC CRT deprecates. Third-party code
# we do not patch, so silence the deprecation on rtaudio's own target only --
# same policy as the pmp `-w` above. Kept off window_engine deliberately: a
# target-wide _CRT_SECURE_NO_WARNINGS would hide real deprecations in our code.
if(TARGET rtaudio)
    target_compile_definitions(rtaudio PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

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

# libyaml is compiled straight into window_engine, so a target-wide define would
# leak onto our own code and could mask a real deprecation in first-party source.
# Scope it to the vendored files. The warning is strdup -- the POSIX-NAME
# deprecation, which is _CRT_NONSTDC_NO_DEPRECATE, a different macro from
# _CRT_SECURE_NO_WARNINGS. Applied to all eight .c files though only api.c warns
# today, so a libyaml update does not reopen this.
#
# Unused vendored FUNCTIONS are deliberately not handled here -- see the named
# allow-lists in image_decode.cpp and tinyexr_impl.cpp. A build-system
# -Wno-unused-function would silence the next one too, and the point is that the
# next one should have to be looked at.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/api.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/dumper.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/emitter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/loader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/parser.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/scanner.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/yaml/libyaml/src/writer.c"
    PROPERTIES COMPILE_DEFINITIONS "_CRT_NONSTDC_NO_DEPRECATE"
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
        "${CMAKE_CURRENT_SOURCE_DIR}/external/stb"
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

# The Windows SDK's xinput.lib exports a DllMain thunk. lld-link (unlike
# link.exe) diagnoses that and skips it -- correct behaviour, nothing fixable on
# our side, one warning on every consumer's link line. INTERFACE because
# window_engine is a static lib and does not link itself. Pinned to this single
# diagnostic id rather than a blanket ignore list, so a future genuine linker
# warning still surfaces.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(window_engine INTERFACE "/ignore:importeddllmain")
endif()

# New renderer (wozzits-rhi) — the engine depends on it (bridge topology A).
# Header-only INTERFACE library, vendored in-repo at rhi/ (was a sibling repo
# before the monorepo consolidation); disable its tests in this build. PUBLIC
# because engine headers (rhi_render_program_bridge.h) expose rhi types to
# consumers.
set(WOZZITS_RHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi"
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
    # wozzits-v8 is NOT vendored in this monorepo (unlike rhi/ and editor/).
    # Point this at a wozzits-v8 checkout to build the optional scripting
    # examples; defaults to a sibling checkout next to the monorepo.
    set(WOZZITS_V8_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../wozzits-v8"
        CACHE PATH "Path to an external wozzits-v8 checkout (not vendored here)")
    if(NOT EXISTS "${WOZZITS_V8_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "WOZZITS_ENABLE_V8_SCRIPTING is ON but no wozzits-v8 checkout was found at "
            "'${WOZZITS_V8_SOURCE_DIR}'. Set -DWOZZITS_V8_SOURCE_DIR=<path> to a wozzits-v8 clone.")
    endif()
    add_subdirectory(
        "${WOZZITS_V8_SOURCE_DIR}"
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
# Generated play-time profiling CSVs (frame_profile_<tag>.csv) live under a
# sample project's resource_root but are runtime telemetry, not resources -- drop
# them from the staged set so a new file per play does not get copied into the
# build resources. (They stay gitignored; see .gitignore.)
list(FILTER WZ_RESOURCE_FILES EXCLUDE REGEX "/frame_profile[^/]*\\.csv$")
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
# The SHIPPING sg_lit shaders, so the on-device lit test drives the real ones
# rather than fixture copies that could drift from them (the same reason the
# puppet render test drives ensure_puppet_program).
set(WZ_ENGINE_SHADER_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/window_engine/resources/shaders")
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
configure_file(
    "${WZ_REBIND_FIXTURE_SRC}/fixture_sky.sky_gaussian.json"
    "${WZ_REBIND_FIXTURE_DST}/fixture_sky.sky_gaussian.json"
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

# Issue #287: a scene for the test_rebind_fixture graph whose one drawable node
# carries a render_to_texture component naming graph node 20 (a render-target
# texture), so the engine fills that texture with no app code and drops the node
# from the main pass.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/render_to_texture_scene.json"
    "${WZ_REBIND_FIXTURE_DST}/render_to_texture.scene.json"
    COPYONLY)

# Self-contained render fixture for the bundle-export smoke test (#295, Seam 3.6):
# a RELOCATABLE project (project-relative source paths, all referenced shaders
# staged locally) so wozzits_export — which requires a self-contained project —
# can bundle it. The graph + manifest are committed (relative paths); the scene,
# sky and the LOCAL shaders are staged from test_rebind_fixture, and the SHIPPING
# sg_lit shaders are staged from the engine (not committed copies, so they cannot
# drift — matching WZ_ENGINE_SHADER_DIR above).
set(WZ_BUNDLE_FIXTURE_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/test_bundle_fixture")
set(WZ_BUNDLE_FIXTURE_DST
    "${CMAKE_BINARY_DIR}/resources/projects/test_bundle_fixture")
configure_file(
    "${WZ_BUNDLE_FIXTURE_SRC}/assets.graph.json"
    "${WZ_BUNDLE_FIXTURE_DST}/assets.graph.json" COPYONLY)
configure_file(
    "${WZ_BUNDLE_FIXTURE_SRC}/.wozzits/project.json"
    "${WZ_BUNDLE_FIXTURE_DST}/.wozzits/project.json" COPYONLY)
configure_file(
    "${WZ_REBIND_FIXTURE_SRC}/scene.json"
    "${WZ_BUNDLE_FIXTURE_DST}/scene.json" COPYONLY)
configure_file(
    "${WZ_REBIND_FIXTURE_SRC}/fixture_sky.sky_gaussian.json"
    "${WZ_BUNDLE_FIXTURE_DST}/fixture_sky.sky_gaussian.json" COPYONLY)
foreach(_wz_bundle_shader
    pull_cube_vs pull_cube_ps custom_field_vs custom_field_ps
    material_vs material_shared material_ps)
    configure_file(
        "${WZ_FIXTURE_SHADER_DIR}/${_wz_bundle_shader}.hlsl"
        "${WZ_BUNDLE_FIXTURE_DST}/shaders/${_wz_bundle_shader}.hlsl" COPYONLY)
endforeach()
foreach(_wz_bundle_sg_lit sg_lit_uv_vs sg_lit_uv_ps sg_lighting_common)
    configure_file(
        "${WZ_ENGINE_SHADER_DIR}/sg_lit/${_wz_bundle_sg_lit}.hlsl"
        "${WZ_BUNDLE_FIXTURE_DST}/shaders/sg_lit/${_wz_bundle_sg_lit}.hlsl" COPYONLY)
endforeach()

# Issues #285/#287/#288: a scene for the test_rebind_fixture graph carrying the
# whole authored chain -- a render-to-texture source filling node 27, a
# composite material (node 28) placing it over a base colour, and a surface
# binding that material at material_albedo through the material-layout program
# (node 26). This is the fixture the composite driver needs: a program layout
# with a texture row for a material to bind.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/composite_material_scene.json"
    "${WZ_REBIND_FIXTURE_DST}/composite_material.scene.json"
    COPYONLY)

# Issue #290: the UV-sampling surface ALONE, so the renderer's descriptor-table
# count is evidence about that one renderable. In the composite scene the
# render-to-texture source binds a table too, which masks a surface that failed
# to realize.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/uv_surface_scene.json"
    "${WZ_REBIND_FIXTURE_DST}/uv_surface.scene.json"
    COPYONLY)

# The UV-sampling LIT variant: the same quad shaded by the shipping sg_lit_uv
# program, so a material texture maps by the mesh's own coordinates on a lit
# surface (#290's shader half, unblocked by #289's shared BRDF).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/lit_uv_surface_scene.json"
    "${WZ_REBIND_FIXTURE_DST}/lit_uv_surface.scene.json"
    COPYONLY)

# Issue #288: three overlapping ALPHA-BLENDED cubes along the view axis for the
# test_rebind_fixture graph (all binding node 44, the alpha_program renderable),
# authored near-to-far. The draw-submission capture test renders it and reports
# what order the recorder saw -- the measurement the ladder register's
# Transparency row was missing.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/alpha_order_scene.json"
    "${WZ_REBIND_FIXTURE_DST}/alpha_order.scene.json"
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
# #252: the graph now carries a pull-mesh render chain (nodes 1-5 = cube mesh +
# pull shaders, 9 = geometry, 10 = program, 8 = pre-built pull-mesh renderable),
# mirroring test_rebind_fixture, so the spawn test can register a DRAWABLE prefab
# and verify a spawn materializes its render bindings incrementally. Configure the
# .in so the bundled shader paths are absolute (machine-correct).
set(WZ_FIXTURE_SHADER_DIR "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/shaders")
configure_file(
    "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/assets.graph.json.in"
    "${WZ_PREFAB_SPAWN_FIXTURE_DST}/assets.graph.json"
    @ONLY)
configure_file(
    "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/scene.json"
    "${WZ_PREFAB_SPAWN_FIXTURE_DST}/scene.json"
    COPYONLY)
# #252 pooling: a scene with a "manager" node bound to pool_prewarm_probe, which
# submits a PARKED spawn-with-identity on self.start and records the completion.
configure_file(
    "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/pool_scene.json"
    "${WZ_PREFAB_SPAWN_FIXTURE_DST}/pool_scene.json"
    COPYONLY)
# #252 pooling regression: a scene whose manager PREWARMS a parked instance then
# UNPARKS its ROOT -- the parked-subtree fix must revive a CHILD behavior.
configure_file(
    "${WZ_PREFAB_SPAWN_FIXTURE_SRC}/pool_deploy_scene.json"
    "${WZ_PREFAB_SPAWN_FIXTURE_DST}/pool_deploy_scene.json"
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
# The two render tests that exercise this fixture (glb_scene_source,
# behavior_dispatch) init the engine with resource_root="resources" and load the
# tank GLB by the resources-relative path "gltf/test-mesh-a.glb". Stage the
# TEST-OWNED GLB (tests/fixtures/gltf/, deliberately named off "tank1.glb" so it
# never collides with the production model in searches) into the staged
# resources, so those tests never pick up window_engine/resources/gltf/tank1.glb
# -- which gameplay work mutates (e.g. the gun-elevation barrel_orientation node).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gltf/test-mesh-a.glb"
    "${CMAKE_BINARY_DIR}/resources/gltf/test-mesh-a.glb"
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

# fp_status_listener.cpp reads two env flags with std::getenv, which the MSVC CRT
# deprecates in favour of the non-standard _dupenv_s. std::getenv is the correct
# call, so the deprecation is suppressed rather than obeyed -- but ONLY on this
# file, so it cannot mask a real deprecation anywhere else. It is compiled into
# every test executable (see rs_add_test_group below), so one un-suppressed line
# here printed 314 times across a full test build.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/fp_status_listener.cpp"
    PROPERTIES COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS"
)

function(rs_add_test_group category name)
    set(target "${category}_${name}")

    add_executable(${target}
        tests/${category}/${name}.cpp
        # Reports the IEEE-754 exception flags each test raises. Compiled into
        # every test executable rather than linked from a static library, so the
        # linker cannot drop its static initializer. Debug-only, see the file.
        tests/support/fp_status_listener.cpp)

    target_include_directories(${target}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/external/ply/wz_ply"
            # so a test can say #include <support/fp_expectations.h>
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
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
    rhi_scene_renderer_rebind_tests
    )

    if(name IN_LIST WZ_TESTS_REQUIRING_RESOURCES)
        add_dependencies(${target} wz_stage_resources)
    endif()


    add_test(NAME ${target} COMMAND ${target})

    # Make every test addressable by its category as a ctest label, so a gate can
    # run or skip a whole category. In particular `ctest -L render` then runs
    # exactly the on-device render suites (the two standalone wozzits_app_v1
    # render tests below carry the same "render" label) -- see BUILDING.md
    # "Tests that require GPU / windowing". Before #317 D1-T11 only those two were
    # labelled, so a
    # `-L render` gate missed the ~33 rs_add_test_group(render ...) device suites
    # and render regressions (D1-C20/C21) shipped green.
    set_tests_properties(${target} PROPERTIES
        LABELS ${category}
        TIMEOUT ${WZ_TEST_TIMEOUT_SECONDS})

    # Test-process environment. WZ_TASKS_FORCE_SERIAL is pinned OFF because
    # wz::tasks::force_serial() latches the env var into a function-local
    # `static const bool` on its first call: a shell that still had it set from a
    # bisect would silently run the WHOLE suite on the serial backend, and every
    # concurrency test would pass green having exercised no concurrency at all.
    # ctest sets this per test process, so it beats the ambient shell without
    # touching the developer's own environment.
    set(test_env "WZ_TASKS_FORCE_SERIAL=0")
    # The floating-point gate, applied to the ctest run only -- see the option.
    if(WZ_FP_STRICT_TESTS)
        list(APPEND test_env "WZ_FP_STRICT=1")
    endif()
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "${test_env}")

endfunction()

# A test that calls the C ABI's EXPORTS, not just its structs. Distinct from
# rs_add_test_group because it links wozzits_abi (the shared library) on top of
# window_engine: the exports live in the DLL, so a test that only links
# window_engine cannot reach them -- which is why wozzits_abi.cpp carried 78
# exports and no direct test for months (issue #308, part A1's standing
# TEST-GAP). WZ_ABI_EXPORTS is deliberately NOT defined here, so WZ_ABI_API
# resolves empty and the declarations are plain externs the linker satisfies
# from the import library.
#
# No runtime copy step is needed: the DLL and the test executables share this
# build directory, so Windows resolves it beside the exe.
function(rs_add_abi_test_group category name)
    set(target "${category}_${name}")

    add_executable(${target}
        tests/${category}/${name}.cpp
        tests/support/fp_status_listener.cpp)   # see rs_add_test_group

    set_target_properties(${target}
        PROPERTIES FOLDER "tests/${category}"
    )

    target_compile_definitions(${target}
        PRIVATE
            WZ_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
            # A project the session exports can actually OPEN: it carries both a
            # manifest and an asset graph. wz_host_create_project alone writes no
            # graph, so a session opened on its output fails with "project manifest
            # does not define an asset graph" -- which would silently SKIP every
            # test needing a live session, i.e. exactly the ones worth having. The
            # test copies this before use and never writes back.
            WZ_ABI_TEST_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/terrain_constraint_fixture"
    )

    target_link_libraries(${target}
        PRIVATE
            gtest
            GTest::gtest_main
            window_engine
            wozzits_abi
    )

    add_test(NAME ${target} COMMAND ${target})

    # Make every test addressable by its category as a ctest label, so a gate can
    # run or skip a whole category. In particular `ctest -L render` then runs
    # exactly the on-device render suites (the two standalone wozzits_app_v1
    # render tests below carry the same "render" label) -- see BUILDING.md
    # "Tests that require GPU / windowing". Before #317 D1-T11 only those two were
    # labelled, so a
    # `-L render` gate missed the ~33 rs_add_test_group(render ...) device suites
    # and render regressions (D1-C20/C21) shipped green.
    set_tests_properties(${target} PROPERTIES
        LABELS ${category}
        TIMEOUT ${WZ_TEST_TIMEOUT_SECONDS})

    # See rs_add_test_group for why WZ_TASKS_FORCE_SERIAL is pinned here.
    set(test_env "WZ_TASKS_FORCE_SERIAL=0")
    # The floating-point gate, applied to the ctest run only -- see the option.
    if(WZ_FP_STRICT_TESTS)
        list(APPEND test_env "WZ_FP_STRICT=1")
    endif()
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "${test_env}")

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

# ----------------------------
# wozzits_sky_bake — pure-CPU CLI that fits a spherical-Gaussian mixture to an
# EXR panorama and writes it as <name>.sky_gaussian.json (Seam A, issue #260).
# No GPU / no engine boot; links the shared window_engine library only.
# ----------------------------
add_executable(wozzits_sky_bake
    src/tools/sky_gaussian_bake/main.cpp
)

target_link_libraries(wozzits_sky_bake
    PRIVATE
        window_engine
)

# wozzits_export — headless standalone-bundle exporter (issue #295, Seam 3).
# Resolves a project, runs the resource-closure walker, and assembles a
# self-contained bundle (project tree + runtime exe + wozzits_app.json). No GPU /
# no engine boot; links the shared window_engine library only.
# ----------------------------
add_executable(wozzits_export
    src/tools/wozzits_export/main.cpp
)

target_link_libraries(wozzits_export
    PRIVATE
        window_engine
)

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
    rs_add_test_group(engine frame_schedule_tests)
    rs_add_test_group(engine runtime_camera_tests)
    rs_add_test_group(engine flying_camera_tests)
    rs_add_test_group(engine project_manifest_tests)
    rs_add_test_group(engine app_bootstrap_config_tests)
    rs_add_test_group(engine bundle_closure_tests)
    rs_add_abi_test_group(engine abi_export_contract_tests)
    rs_add_test_group(engine asset_graph_editor_session_tests)
    rs_add_test_group(engine asset_graph_schema_registry_tests)
    rs_add_test_group(engine asset_catalog_tests)
    rs_add_test_group(engine behavior_actuator_catalog_abi_tests)
    rs_add_test_group(engine behavior_module_catalog_abi_tests)
    rs_add_test_group(engine asset_graph_json_roundtrip_tests)
    rs_add_test_group(engine editor_runtime_control_tests)
    rs_add_test_group(engine runtime_channel_tests)
    rs_add_test_group(engine view_controller_tests)
    rs_add_test_group(engine app_context_teardown_tests)

    # First tests of src/platform/win32 (#313, B4-T4 -- the layer had none).
    rs_add_test_group(platform raw_input_translation_tests)
    rs_add_test_group(platform filesystem_whole_file_io_tests)
    rs_add_test_group(platform filesystem_path_semantics_tests)
    rs_add_test_group(platform filesystem_status_query_tests)
    rs_add_test_group(platform filesystem_tree_ops_tests)
    rs_add_test_group(platform filesystem_async_io_tests)
    rs_add_test_group(platform filesystem_atomic_write_tests)

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
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

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
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

    # Bundle-export smoke test (#295, Seam 3.6): the headless acceptance guard.
    # Export the self-contained render fixture into a bundle, then launch the
    # BUNDLED exe from the bundle dir with NO path args — so it boots from the
    # co-located wozzits_app.json (Seam 1) and its relative-path closure — and
    # assert it renders. Two linked tests via a CTest fixture: the export step
    # (pure CPU, no device) sets up the bundle and always runs; the render step
    # (GPU) requires it and skips (not fails) on no-GPU (kRuntimeNoDeviceExitCode).
    add_test(NAME wozzits_export_bundle_setup
        COMMAND wozzits_export
            --project "${WZ_BUNDLE_FIXTURE_DST}"
            --out "${CMAKE_BINARY_DIR}/bundle_smoke"
            --app-exe "$<TARGET_FILE:wozzits_app_v1>"
            --no-log)
    set_tests_properties(wozzits_export_bundle_setup PROPERTIES
        FIXTURES_SETUP wozzits_bundle
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

    add_test(NAME wozzits_export_bundle_render
        COMMAND "${CMAKE_BINARY_DIR}/bundle_smoke/wozzits_app_v1.exe" --frames 5)
    set_tests_properties(wozzits_export_bundle_render PROPERTIES
        FIXTURES_REQUIRED wozzits_bundle
        SKIP_RETURN_CODE 3
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

    # Seam 3.3 guard: the SEALED export end to end. --seal warms the baked cache
    # (the fixture's procedural scalar field), strips the cache-served sources,
    # writes cache{sealed:true}, and self-verifies a bounded run from the cache
    # alone — so exit 0 proves the whole warm→strip→seal→run pipeline. GPU-
    # dependent; skips (not fails) on no-GPU, where the warm run reports no device
    # and the tool propagates kRuntimeNoDeviceExitCode (3).
    add_test(NAME wozzits_export_bundle_sealed
        COMMAND wozzits_export
            --project "${WZ_BUNDLE_FIXTURE_DST}"
            --out "${CMAKE_BINARY_DIR}/bundle_smoke_sealed"
            --app-exe "$<TARGET_FILE:wozzits_app_v1>"
            --seal --no-log)
    set_tests_properties(wozzits_export_bundle_sealed PROPERTIES
        SKIP_RETURN_CODE 3
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

    # External-resource relocation guard (#295): test_rebind_fixture references
    # shaders OUTSIDE its project dir (its own fixture shaders + the shipping
    # sg_lit shaders), so exporting it exercises relocate_external_sources —
    # copying them into the bundle's resources/ subtree and rewriting the graph —
    # then the bundled exe must render from the relocated copies. Two linked tests:
    # the export (CPU) sets up the bundle; the render (GPU) requires it, skips on
    # no-GPU.
    add_test(NAME wozzits_export_relocate_setup
        COMMAND wozzits_export
            --project "${WZ_REBIND_FIXTURE_DST}"
            --out "${CMAKE_BINARY_DIR}/bundle_smoke_relocate"
            --app-exe "$<TARGET_FILE:wozzits_app_v1>"
            --no-log)
    set_tests_properties(wozzits_export_relocate_setup PROPERTIES
        FIXTURES_SETUP wozzits_bundle_relocate
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

    add_test(NAME wozzits_export_relocate_render
        COMMAND
            "${CMAKE_BINARY_DIR}/bundle_smoke_relocate/wozzits_app_v1.exe"
            --frames 5)
    set_tests_properties(wozzits_export_relocate_render PROPERTIES
        FIXTURES_REQUIRED wozzits_bundle_relocate
        SKIP_RETURN_CODE 3
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

    # Behavior DLL shipping + ABI verify/stamp guard (#295 Seam 3.4/3.5). Export
    # the rendering bundle fixture but SHIP the behavior test plugin via
    # --behavior-modules: the export copies just the DLL into a clean
    # bundle/behavior/, dry-run-loads it to verify its ABI matches the runtime (a
    # mismatch would fail the export), and stamps behavior_abi_version; the bundled
    # exe then loads it and renders. Uses the plugin staged by
    # wozzits_app_behavior_test_plugin's POST_BUILD (built as part of `all`).
    add_test(NAME wozzits_export_behavior_setup
        COMMAND wozzits_export
            --project "${WZ_BUNDLE_FIXTURE_DST}"
            --out "${CMAKE_BINARY_DIR}/bundle_smoke_behavior"
            --app-exe "$<TARGET_FILE:wozzits_app_v1>"
            --behavior-modules "${WZ_BEHAVIOR_FIXTURE_MODULE_DIR}"
            --no-log)
    set_tests_properties(wozzits_export_behavior_setup PROPERTIES
        FIXTURES_SETUP wozzits_bundle_behavior
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

    add_test(NAME wozzits_export_behavior_render
        COMMAND
            "${CMAKE_BINARY_DIR}/bundle_smoke_behavior/wozzits_app_v1.exe"
            --frames 5)
    set_tests_properties(wozzits_export_behavior_render PROPERTIES
        FIXTURES_REQUIRED wozzits_bundle_behavior
        SKIP_RETURN_CODE 3
        LABELS "render"
        TIMEOUT ${WZ_RENDER_TEST_TIMEOUT_SECONDS}
        # These launch the real app/exporter, which drives the task pool -- pin the
        # backend here too (see rs_add_test_group).
        ENVIRONMENT "WZ_TASKS_FORCE_SERIAL=0")

    rs_add_test_group(behavior behavior_registry_core_tests)
    rs_add_test_group(behavior event_channels)
    rs_add_test_group(behavior behavior_plugin_dynamic_module_tests)
    rs_add_test_group(behavior behavior_dispatch_tests)
    rs_add_test_group(behavior peer_state_read_tests)
    rs_add_test_group(behavior behavior_init_tests)
    rs_add_test_group(behavior behavior_descendant_finder_tests)
    rs_add_test_group(behavior statechart_runner_store_tests)
    rs_add_test_group(behavior statechart_actuator_call_tests)
    rs_add_test_group(behavior statechart_event_trigger_tests)
    rs_add_test_group(behavior statechart_named_agent_tests)
    rs_add_test_group(behavior statechart_measure_effect_tests)
    rs_add_test_group(behavior statechart_reward_effect_tests)
    rs_add_test_group(behavior statechart_read_state_tests)
    rs_add_test_group(behavior mind_ir_tests)
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
    rs_add_test_group(behavior spawn_identity_dispatch_tests)
    rs_add_test_group(behavior quantum_agent_behavior_tests)
    rs_add_test_group(behavior quantum_agent_actuator_tests)
    rs_add_test_group(behavior module_params_tests)
    rs_add_test_group(behavior fsm2_behavior_tests)
    rs_add_test_group(cognition qstate_tests)
    rs_add_test_group(cognition mean_field_tests)
    rs_add_test_group(cognition loopy_bp_tests)
    rs_add_test_group(cognition exact_group_tests)
    rs_add_test_group(cognition measure_basis_tests)
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
    rs_add_test_group(cognition graph_tn_tests)
    rs_add_test_group(cognition graph_bp_tests)
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
    rs_add_test_group(jobs critical_path_tests)
    rs_add_test_group(jobs scheduler_benchmark_tests)
    rs_add_test_group(jobs engine_adversarial_tests)

    rs_add_test_group(tasks task_tests)
    rs_add_test_group(tasks task_scheduler_tests)
    rs_add_test_group(tasks fiber_backend_tests)

    rs_add_test_group(io io_executor_tests)
    rs_add_test_group(diagnostics diagnostic_state_tests)
    rs_add_test_group(diagnostics logger_service_tests)
    rs_add_test_group(engine input_events)



    rs_add_test_group(asset new_asset_test_1)
    rs_add_test_group(asset asset_system_adversarial_tests)
    rs_add_test_group(asset engine_asset_library)
    rs_add_test_group(asset scalar_field_tests)
    rs_add_test_group(asset vector_field_tests)
    rs_add_test_group(asset scalar_field_procedural_tests)
    rs_add_test_group(asset scalar_field_terrain_tests)
    rs_add_test_group(asset scalar_field_texture_test)
    rs_add_test_group(asset scalar_field_rhi_residency_test)
    rs_add_test_group(asset scalar_field_mip_pyramid_tests)
    rs_add_test_group(asset gaussian_splat_cloud_rhi_residency_test)
    rs_add_test_group(asset sky_gaussian_rhi_residency_test)
    rs_add_test_group(asset render_target_texture_test)
    rs_add_test_group(asset composite_material_test)
    rs_add_test_group(asset star_catalog_rhi_residency_test)
    rs_add_test_group(asset star_catalog_ply_residency_test)
    rs_add_test_group(asset scalar_field_set_tests)
    rs_add_test_group(asset foundational_assets)
    rs_add_test_group(asset key_utils)
    rs_add_test_group(asset asset_graph_draft)
    rs_add_test_group(asset param_defaults)
    rs_add_test_group(asset asset_graph_authoring)
    rs_add_test_group(asset csv_tests)
    rs_add_test_group(asset engine_disk_cache_provider_tests)

    rs_add_test_group(asset_inochi inochi_load_tests)
    rs_add_test_group(asset_inochi puppet_draw_list_tests)
    rs_add_test_group(asset_inochi puppet_deform_tests)
    rs_add_test_group(asset_inochi puppet_physics_tests)
    rs_add_test_group(asset_inochi puppet_residency_tests)
    rs_add_test_group(asset_inochi puppet_shader_staging_tests)

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
    rs_add_test_group(asset_placed_field placed_field_asset_module)
    rs_add_test_group(asset_clipmap_lattice_schedule clipmap_lattice_schedule_asset_module)
    rs_add_test_group(asset_atmosphere atmosphere_asset_module)
    rs_add_test_group(asset_environment environment_asset_module)
    rs_add_test_group(asset_texture texture_asset_module)
    rs_add_test_group(asset_texture image_decode_limits_tests)
    rs_add_test_group(asset_texture image_decode_formats_tests)
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
    rs_add_test_group(gpu timeline_exclusivity)

    rs_add_test_group(asset_data_table data_table)
    rs_add_test_group(asset_data_table data_table_asset_module)
    rs_add_test_group(asset_data_table data_table_csv_export)
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
    rs_add_test_group(asset_renderable renderable_scalar_field_tests)
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
    rs_add_test_group(asset_scene scene_ecs_construction_tests)
    rs_add_test_group(asset_scene scene_ecs_boundary_tests)
    rs_add_test_group(asset_scene scene_authoring_component_roundtrip_tests)
    rs_add_test_group(asset_scene scene_editor_camera_metadata_tests)
    rs_add_test_group(asset_scene scene_subtree_export_tests)
    rs_add_test_group(asset_scene scenelet_authoring_tests)
    rs_add_test_group(asset_scene scene_document_behavior_config_tests)
    rs_add_test_group(asset_scene scene_document_persistence_tests)
    rs_add_test_group(asset_scene prefab_instantiate_tests)
    rs_add_test_group(asset_scene scene_compute_kernel_roundtrip_tests)
    rs_add_test_group(asset_scene scene_mesh_compute_field_roundtrip_tests)
    rs_add_test_group(asset_scene scene_materialize_compute_kernel_tests)
    rs_add_test_group(asset_scene scene_materialize_mesh_compute_field_tests)
    rs_add_test_group(asset_scene scene_materialize_mesh_import_tests)
    rs_add_test_group(asset_scene scene_materialize_field_sky_tests)
    rs_add_test_group(asset_scene scene_materialize_light_tests)
    rs_add_test_group(asset_scene scene_materialize_terrain_tests)
    rs_add_test_group(asset_scene scene_materialize_default_scene_tests)
    rs_add_test_group(asset_scene scene_atmosphere_binding_tests)
    rs_add_test_group(asset_scene scene_environment_binding_tests)
    rs_add_test_group(asset_scene scene_fingerprint_sensitivity_tests)
    rs_add_test_group(asset_scene scene_json_hostile_document_tests)

    rs_add_test_group(asset_gltf gltf_importer)

    # Containers
    rs_add_test_group(containers mpsc_queue)
    # MPSCQueue's concurrency tests park a producer/consumer inside the two-step
    # publish/consume windows through the header's test seam -- see issue #289
    # and the WZ_MPSC_QUEUE_TEST_HOOKS block in mpsc_queue.h.
    target_compile_definitions(containers_mpsc_queue PRIVATE WZ_MPSC_QUEUE_TEST_HOOKS)
    rs_add_test_group(containers mpsc_ring_buffer)
    rs_add_test_group(containers spsc_queue)
    rs_add_test_group(containers work_stealing_deque)
    # The deque's concurrency tests park a thief inside steal()'s read-then-CAS
    # window through the header's test seam to construct the last-element /
    # recycled-index race deterministically (#304) -- see the
    # WZ_WORK_STEALING_DEQUE_TEST_HOOKS block in work_stealing_deque.h.
    target_compile_definitions(containers_work_stealing_deque PRIVATE WZ_WORK_STEALING_DEQUE_TEST_HOOKS)
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

    rs_add_test_group(sky_gaussian test_sg_math)
    rs_add_test_group(sky_gaussian test_synthetic_recovery)
    rs_add_test_group(sky_gaussian test_determinism)
    rs_add_test_group(sky_gaussian test_serialize_roundtrip)
    rs_add_test_group(sky_gaussian test_fit_convergence)
    rs_add_test_group(sky_gaussian test_creative_params)

    rs_add_test_group(lighting test_sg_lighting)
    rs_add_test_group(starfield test_star_catalog)
    rs_add_test_group(starfield test_star_catalog_ply_import)
    rs_add_test_group(motion test_motion_filter)
    rs_add_test_group(motion test_motion_filter_sim)

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
    rs_add_test_group(render authored_render_targets_tests)
    rs_add_test_group(render stub_backend_test_4)
    rs_add_test_group(render storage_reuse_tests)
    rs_add_test_group(render render_frame_sectioned_test_1)
    rs_add_test_group(render pipeline_benchmark_test_1)
    rs_add_test_group(render rhi_dx12_pipeline_tests)
    rs_add_test_group(render rhi_dx12_compute_dispatch_tests)
    rs_add_test_group(render clipmap_drawn_surface_pin_tests)
    rs_add_test_group(render rhi_dx12_command_recorder_tests)
    rs_add_test_group(render rhi_render_program_bridge_tests)
    rs_add_test_group(render rhi_dummy_backend_integration_tests)
    rs_add_test_group(render rhi_gpu_backend_tests)
    rs_add_test_group(render rhi_scene_renderer_rebind_tests)
    rs_add_test_group(render rhi_gaussian_splat_cloud_render_tests)
    rs_add_test_group(render rhi_star_field_render_tests)
    rs_add_test_group(render rhi_puppet_render_tests)
    rs_add_test_group(render rhi_puppet_authoring_render_tests)
    rs_add_test_group(render rhi_offscreen_rt_tests)
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

    # The #288 frame capture: watches a real device frame's DrawInstanced calls
    # and the renderer's submission order, so the ladder register's Draw
    # submission and Transparency rows rest on a measurement. Loads
    # test_rebind_fixture's graph paired with alpha_order.scene.json.
    rs_add_test_group(render draw_submission_capture_tests)
    add_dependencies(render_draw_submission_capture_tests wz_stage_resources)

    # WozzitsApp_v1 authored render-to-texture test (issue #287). On-device:
    # loads test_rebind_fixture's graph paired with render_to_texture.scene.json;
    # proves an authored render_to_texture source fills its target texture with
    # no app code, and that the source leaves the main pass.
    rs_add_test_group(render wozzits_app_render_to_texture_tests)
    add_dependencies(render_wozzits_app_render_to_texture_tests wz_stage_resources)

    # WozzitsApp_v1 authored composite-material test (issues #285/#288). On-
    # device: loads test_rebind_fixture's graph paired with
    # composite_material.scene.json; proves an authored composite material is
    # filled and bound, and that the composite pass is skipped once nothing
    # feeding it changes.
    rs_add_test_group(render wozzits_app_composite_material_tests)
    add_dependencies(render_wozzits_app_composite_material_tests wz_stage_resources)

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

endif()

# ----------------------------------------------------------------------------
# IDE target organization (Visual Studio / Xcode "Solution Explorer" folders)
# ----------------------------------------------------------------------------
# Group targets into folders so the IDE view isn't one flat top-level list. The
# ~314 test executables already fold themselves under tests/<category> (see the
# set_target_properties FOLDER call in rs_add_test_group / rs_add_abi_test_group);
# this section does the same for everything else -- the engine libraries, the
# apps and CLI tools, the examples, the test-support DLLs, and the vendored
# third-party trees.
#
# FOLDER is purely a generator hint: it changes how projects are grouped in an
# IDE (the VS generator, and VS's Ninja-backed CMake "Targets View") and has no
# effect on what gets built. Ninja/Makefile command-line builds ignore it. Kept
# in one block at end-of-file so every conditional target (examples, test
# plugins) already exists by the time its FOLDER is set.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Set FOLDER on a target only if it exists -- many are guarded by options
# (examples, tests) so they are absent in a lib-only configure.
function(wz_set_folder_if_target target folder)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES FOLDER "${folder}")
    endif()
endfunction()

# Assign a FOLDER to every buildsystem target defined under `dir`, recursively.
# Used to tuck an entire vendored dependency subtree under one node without
# naming each third-party target it happens to create (rtaudio, pmp, fastgltf +
# simdjson, the gtest/gmock quartet, ...).
function(wz_set_folder_recursive dir folder)
    get_property(_wz_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_wz_t IN LISTS _wz_targets)
        set_target_properties(${_wz_t} PROPERTIES FOLDER "${folder}")
    endforeach()
    get_property(_wz_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_wz_sub IN LISTS _wz_subdirs)
        wz_set_folder_recursive("${_wz_sub}" "${folder}")
    endforeach()
endfunction()

# First-party engine libraries (wz_external_ply's objects link into the engine;
# wozzits_rhi is the header-only RHI foundation, an INTERFACE lib).
wz_set_folder_if_target(window_engine   "engine")
wz_set_folder_if_target(wozzits_abi     "engine")
wz_set_folder_if_target(wz_external_ply "engine")
wz_set_folder_if_target(wozzits_rhi     "engine")

# Shippable applications and the editor's play-mode host.
wz_set_folder_if_target(wozzits_app_v1      "apps")
wz_set_folder_if_target(wozzits_editor_host "apps")

# Headless CLI tools.
wz_set_folder_if_target(wozzits_sky_bake "tools")
wz_set_folder_if_target(wozzits_export   "tools")

# Shared resource-staging helper (a custom target, otherwise top-level).
wz_set_folder_if_target(wz_stage_resources "build")

# Examples (only defined when WINDOW_ENGINE_BUILD_EXAMPLES / V8 scripting is ON).
wz_set_folder_if_target(wozzits_rtaudio_smoke             "examples")
wz_set_folder_if_target(rhi_pull_cube                     "examples")
wz_set_folder_if_target(v8_script_smoke                   "examples")
wz_set_folder_if_target(wz_behavior_plugin_template       "examples")
wz_set_folder_if_target(wz_behavior_plugin_angular_motion "examples")

# Test-support plugin DLLs (only defined when WINDOW_ENGINE_ENABLE_TESTS is ON),
# nested under the existing tests/ node beside tests/<category>.
wz_set_folder_if_target(behavior_dynamic_test_plugin           "tests/plugins")
wz_set_folder_if_target(behavior_dynamic_missing_symbol_plugin "tests/plugins")
wz_set_folder_if_target(wozzits_app_behavior_test_plugin       "tests/plugins")

# Vendored third-party trees -> one node each, whatever targets they define.
wz_set_folder_recursive("${CMAKE_CURRENT_SOURCE_DIR}/external/gltf/fastgltf" "dependencies/fastgltf")
wz_set_folder_recursive("${CMAKE_CURRENT_SOURCE_DIR}/external/pmp"           "dependencies/pmp")
wz_set_folder_recursive("${CMAKE_CURRENT_SOURCE_DIR}/external/audio/rtaudio" "dependencies/rtaudio")
if(DEFINED googletest_SOURCE_DIR)
    wz_set_folder_recursive("${googletest_SOURCE_DIR}" "dependencies/googletest")
endif()
