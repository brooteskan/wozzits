cmake_minimum_required(VERSION 3.14)
project(window_engine LANGUAGES CXX)

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


add_library(window_engine)

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
     "src/event/event.cpp")

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

    add_test(NAME ${target} COMMAND ${target})

    get_target_property(GTEST_INCLUDES GTest::gtest INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "gtest include dirs: ${GTEST_INCLUDES}")
endfunction()

# ----------------------------
# Tests
# ----------------------------

if(SCENE_RENDER_ENABLE_TESTS)
    rs_add_test_group(engine engine)

    rs_add_test_group(logging logging)

    rs_add_test_group(window test_window)

    rs_add_test_group(asset asset_test_1)
endif()
