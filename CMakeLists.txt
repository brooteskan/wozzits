cmake_minimum_required(VERSION 3.14)
project(window_engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WINDOW_ENGINE_ENABLE_TESTS "Enable tests" ON)
option(WNIDOW_ENGINE_DEV_BUILD "Use local algo_math" ON)

include(FetchContent)


# ----------------------------
# MSVC runtime configuration
# ----------------------------
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
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


# ----------------------------
# Core library target
# ----------------------------

set (YAML_VERSION_MAJOR 0)
set (YAML_VERSION_MINOR 2)
set (YAML_VERSION_PATCH 5)
set (YAML_VERSION_STRING "${YAML_VERSION_MAJOR}.${YAML_VERSION_MINOR}.${YAML_VERSION_PATCH}")

add_library(window_engine)

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
)

target_link_libraries(window_engine
PUBLIC
    scene_render::scene_render
    algo_math::algo_math
PRIVATE
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

# ----------------------------
# Test helpers
# ----------------------------

function(rs_add_test_group category name)
    set(target "${category}_${name}")

    add_executable(${target}
        tests/${category}/${name}.cpp
        "src/event/event.cpp"
     "src/engine/game_app.cpp")

    set_target_properties(${target}
        PROPERTIES FOLDER "tests/${category}"
    )

    target_compile_definitions(${target}
        PRIVATE SCENE_RENDER_TEST=1
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

if(SCENE_RENDER_ENABLE_TESTS)
    target_compile_definitions(window_engine PUBLIC WZ_ENABLE_TESTING)

    rs_add_test_group(engine engine)
    rs_add_test_group(engine runtime_camera_tests)

    rs_add_test_group(logging logging)
    rs_add_test_group(logging logger_queue_tests)
    rs_add_test_group(logging threaded_logger_tests)

    rs_add_test_group(jobs job_graph_tests)
    rs_add_test_group(jobs render_prep_job_tests)
    rs_add_test_group(jobs profiler_tests)

    rs_add_test_group(window test_window)

    rs_add_test_group(asset new_asset_test_1)
    rs_add_test_group(asset engine_asset_library)
    rs_add_test_group(asset scalar_field_tests)
    rs_add_test_group(asset scalar_field_procedural_tests)
    rs_add_test_group(asset scalar_field_texture_test)
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
    rs_add_test_group(game_app game_app_main)
endif()
