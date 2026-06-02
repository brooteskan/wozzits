cmake_minimum_required(VERSION 3.20)

project(wz_behavior_plugin_template LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WZ_WINDOW_ENGINE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH
    "Path to the wozzits-window-engine repository root")

add_library(wz_behavior_plugin_template SHARED
    sample_behavior_plugin.cpp)

add_library(wz_behavior_plugin_angular_motion SHARED
    angular_motion_plugin.cpp)

target_include_directories(wz_behavior_plugin_template
    PRIVATE
        "${WZ_WINDOW_ENGINE_ROOT}"
        "${WZ_WINDOW_ENGINE_ROOT}/window_engine")

target_include_directories(wz_behavior_plugin_angular_motion
    PRIVATE
        "${WZ_WINDOW_ENGINE_ROOT}"
        "${WZ_WINDOW_ENGINE_ROOT}/window_engine")

target_compile_features(wz_behavior_plugin_template PRIVATE cxx_std_20)
target_compile_features(wz_behavior_plugin_angular_motion PRIVATE cxx_std_20)

if(MSVC)
    target_compile_definitions(wz_behavior_plugin_template
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX)
    target_compile_definitions(wz_behavior_plugin_angular_motion
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX)
endif()
