cmake_minimum_required(VERSION 3.20)

project(mesh_trials_behaviors LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WZ_WINDOW_ENGINE_ROOT
    "D:/code/wozzits/wozzits-window-engine"
    CACHE PATH
    "Path to the wozzits-window-engine repository root")

function(add_mesh_trials_behavior_module target source)
    add_library(${target} SHARED
        ${source})

    target_include_directories(${target}
        PRIVATE
            "${WZ_WINDOW_ENGINE_ROOT}"
            "${WZ_WINDOW_ENGINE_ROOT}/window_engine")

    target_compile_features(${target} PRIVATE cxx_std_20)

    set_target_properties(${target}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/build/clang-debug"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/build/clang-debug"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/build/clang-debug")

    if(MSVC)
        target_compile_definitions(${target}
            PRIVATE
                WIN32_LEAN_AND_MEAN
                NOMINMAX)
    endif()
endfunction()

add_mesh_trials_behavior_module(
    mesh_trials_behaviors
    sample_behavior_plugin.cpp)
