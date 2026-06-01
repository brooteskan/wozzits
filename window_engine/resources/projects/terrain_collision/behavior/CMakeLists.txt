cmake_minimum_required(VERSION 3.20)

project(terrain_collision_behaviors LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WZ_WINDOW_ENGINE_ROOT
    "D:/code/wozzits/wozzits-window-engine"
    CACHE PATH
    "Path to the wozzits-window-engine repository root")

add_library(terrain_collision_behaviors SHARED
    sample_behavior_plugin.cpp)

target_include_directories(terrain_collision_behaviors
    PRIVATE
        "${WZ_WINDOW_ENGINE_ROOT}"
        "${WZ_WINDOW_ENGINE_ROOT}/window_engine")

target_compile_features(terrain_collision_behaviors PRIVATE cxx_std_20)

set_target_properties(terrain_collision_behaviors
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/build/clang-debug"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/build/clang-debug"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/build/clang-debug")

if(MSVC)
    target_compile_definitions(terrain_collision_behaviors
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX)
endif()

