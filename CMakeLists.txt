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
option(WINDOW_ENGINE_DEV_BUILD "Use local algo_math" ON)
option(WOZZITS_ENABLE_V8_SCRIPTING "Build optional V8 scripting examples" OFF)


include(FetchContent)


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
# dependency: scene_render
# ----------------------
# 1. If already provided (top-level project)
if(NOT TARGET scene_render::scene_render)

    find_package(scene_render QUIET)

    if(NOT TARGET scene_render::scene_render)
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../wozzits-scene-render/CMakeLists.txt")
            add_subdirectory(
                "${CMAKE_CURRENT_LIST_DIR}/../wozzits-scene-render"
                "${CMAKE_BINARY_DIR}/scene_render_build"
            )
        else()
            message(FATAL_ERROR "scene_render not found")
        endif()
    endif()

endif()

# ----------------------
# dependency: fastgltf
# ----------------------
add_subdirectory(external/gltf/fastgltf)

# ----------------------
# dependency: tinyply
# ----------------------
add_subdirectory(external/ply)


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

add_subdirectory(external/audio/rtaudio)

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
)

target_link_libraries(window_engine
PUBLIC
    scene_render::scene_render
    algo_math::algo_math
PRIVATE
    rtaudio
    fastgltf::fastgltf
    dwmapi
    d3d12
    dxgi
    dxguid)

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
            algo_math::algo_math
            scene_render::scene_render
            window_engine
    )

    set(WZ_TESTS_REQUIRING_RESOURCES
    test_window
    test_mesh_wireframe
    test_gaussian_splat
    game_app_main
    )

    if(name IN_LIST WZ_TESTS_REQUIRING_RESOURCES)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/window_engine/resources"
                "$<TARGET_FILE_DIR:${target}>/resources"
            COMMENT "Copying resources for ${target}"
        )
    endif()


    add_test(NAME ${target} COMMAND ${target})

    get_target_property(GTEST_INCLUDES GTest::gtest INTERFACE_INCLUDE_DIRECTORIES)
    message(STATUS "gtest include dirs: ${GTEST_INCLUDES}")
endfunction()

# ----------------------------
# Tests
# ----------------------------

if(WINDOW_ENGINE_BUILD_EXAMPLES)
    add_executable(wozzits_rtaudio_smoke
        examples/rtaudio/main.cpp
    )

    target_link_libraries(wozzits_rtaudio_smoke
        PRIVATE
            window_engine
    )
endif()

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

if(WINDOW_ENGINE_ENABLE_TESTS AND SCENE_RENDER_ENABLE_TESTS)
    target_compile_definitions(window_engine PUBLIC WZ_ENABLE_TESTING)

    rs_add_test_group(engine engine)
    rs_add_test_group(engine runtime_camera_tests)

    rs_add_test_group(logging logging)
    rs_add_test_group(logging logger_queue_tests)
    rs_add_test_group(logging threaded_logger_tests)

    rs_add_test_group(jobs job_graph_tests)
    # rs_add_test_group(jobs render_prep_job_tests)
    rs_add_test_group(jobs profiler_tests)



    rs_add_test_group(asset new_asset_test_1)
    rs_add_test_group(asset engine_asset_library)
    rs_add_test_group(asset scalar_field_tests)
    rs_add_test_group(asset scalar_field_procedural_tests)
    rs_add_test_group(asset scalar_field_texture_test)
    rs_add_test_group(asset scalar_field_set_tests)
    rs_add_test_group(asset foundational_assets)
    rs_add_test_group(asset csv_tests)

    rs_add_test_group(asset_json yyjson_wrapper)
    rs_add_test_group(asset_json json_polytree)
    rs_add_test_group(asset_json json_table)
    rs_add_test_group(asset_json json_asset_module)

    rs_add_test_group(asset_toml toml_parser)
    rs_add_test_group(asset_toml toml_polytree)
    rs_add_test_group(asset_toml toml_asset_module)

    rs_add_test_group(asset_mesh mesh_table)
    rs_add_test_group(asset_mesh mesh_key_factory)
    rs_add_test_group(asset_mesh mesh_asset_module_interface)
    rs_add_test_group(asset_mesh procedural_mesh)
    rs_add_test_group(asset_mesh mesh_asset_module) 

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
    rs_add_test_group(gpu dx12_descriptor_allocator)

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

    rs_add_test_group(asset_renderable renderable_table)
    rs_add_test_group(asset_renderable renderable_asset_module)

    rs_add_test_group(asset_render_program render_program_asset_module)

    rs_add_test_group(asset_scene scene_asset_module)

    rs_add_test_group(asset_gltf gltf_importer)
    # rs_add_test_group(game_app game_app_main)

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
            algo_math::algo_math
            scene_render::scene_render
            window_engine
    )

    add_custom_command(TARGET ${BENCHMARK_APP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/window_engine/resources"
            "$<TARGET_FILE_DIR:${BENCHMARK_APP_TARGET}>/resources"
        COMMENT "Copying resources for ${BENCHMARK_APP_TARGET}"
    )

endif()
