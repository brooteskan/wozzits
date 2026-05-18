# external/ply/CMakeLists.txt

add_library(wz_external_ply OBJECT
    ./wz_ply/ply_reader.cpp
    ./tinyply/tinyply.cpp
)

target_include_directories(wz_external_ply
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/wz_ply>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tinyply
)

target_compile_features(wz_external_ply PUBLIC cxx_std_20)