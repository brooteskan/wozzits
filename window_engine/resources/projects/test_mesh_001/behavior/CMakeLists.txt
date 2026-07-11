cmake_minimum_required(VERSION 3.20)

project(terrain_collision_behaviors LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WZ_WINDOW_ENGINE_ROOT
    "D:/code/wozzits/wozzits-window-engine"
    CACHE PATH
    "Path to the wozzits-window-engine repository root")

function(add_terrain_collision_behavior_module target source)
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

add_terrain_collision_behavior_module(
    terrain_collision_behaviors
    sample_behavior_plugin.cpp)

add_terrain_collision_behavior_module(
    player_tank_behavior
    player_tank_behavior_plugin.cpp)

add_terrain_collision_behavior_module(
    quantum_tank_agent
    quantum_tank_agent_plugin.cpp)

add_terrain_collision_behavior_module(
    tank_commander
    tank_commander_plugin.cpp)

add_terrain_collision_behavior_module(
    spawn_player
    spawn_player_plugin.cpp)

add_terrain_collision_behavior_module(
    hit_logger
    hit_logger_plugin.cpp)

add_terrain_collision_behavior_module(
    collidable
    collidable_plugin.cpp)

add_terrain_collision_behavior_module(
    pool_manager
    pool_manager_plugin.cpp)

# --- cognition gallery -------------------------------------------------------
# Reusable quantum-cognition building blocks (source grouped under cognition/).
# The helper forces the DLL output to behavior/build/clang-debug regardless of
# source location, so the flat loader directory still finds them.
add_terrain_collision_behavior_module(
    cognition_lamp
    cognition/cognition_lamp_plugin.cpp)

add_terrain_collision_behavior_module(
    tug_of_war
    cognition/tug_of_war_plugin.cpp)

add_terrain_collision_behavior_module(
    drill_sergeant
    cognition/drill_sergeant_plugin.cpp)

add_terrain_collision_behavior_module(
    zeno_observer
    cognition/zeno_observer_plugin.cpp)

add_terrain_collision_behavior_module(
    skinner
    cognition/skinner_plugin.cpp)
