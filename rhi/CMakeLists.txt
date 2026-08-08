cmake_minimum_required(VERSION 3.21)
project(wozzits_rhi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Group targets into IDE folders (Solution Explorer / VS CMake Targets View)
# rather than a flat list -- see the test-target FOLDER below. Harmless when this
# tree is consumed as a subdirectory of the monorepo (the parent sets it too).
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(WOZZITS_RHI_BUILD_TESTS "Build wozzits-rhi tests" ON)

# Header-only foundation library. Consumers (and tests) get the include path.
add_library(wozzits_rhi INTERFACE)
target_include_directories(wozzits_rhi
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(wozzits_rhi INTERFACE cxx_std_20)

if(WOZZITS_RHI_BUILD_TESTS)
    enable_testing()

    # First-party warning posture from line one (the #167 ethos: surface
    # problems at compile time, not as silent runtime fallthroughs).
    # clang-cl identifies as MSVC-like, so the MSVC branch covers it.
    if(MSVC)
        set(WZ_RHI_WARNINGS /W4)
    else()
        set(WZ_RHI_WARNINGS -Wall -Wextra)
    endif()

    set(WZ_RHI_TESTS
        tag_registry_tests
        render_program_registry_tests
        render_program_bridge_tests
        shader_module_tests
        constants_layout_tests
        shader_resource_group_layout_tests
        shader_resource_group_tests
        geometry_view_tests
        draw_list_tag_tests
        draw_packet_tests
        draw_encode_tests
        render_packet_validation_tests
        render_packet_contract_tests
        compute_program_tests
        handle_tests
        gpu_resource_registry_tests
        frame_graph_tests
        frame_graph_execution_tests)

    foreach(test_name ${WZ_RHI_TESTS})
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE wozzits_rhi)
        target_compile_options(${test_name} PRIVATE ${WZ_RHI_WARNINGS})
        add_test(NAME ${test_name} COMMAND ${test_name})
        # Tuck the test exes under a tests/ node in the IDE instead of the flat
        # top-level list. Only reached in a standalone rhi build -- the monorepo
        # forces WOZZITS_RHI_BUILD_TESTS OFF, so no test targets exist there.
        set_target_properties(${test_name} PROPERTIES FOLDER "tests")
    endforeach()
endif()
