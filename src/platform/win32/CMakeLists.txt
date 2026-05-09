target_sources(window_engine PRIVATE
    src/win32.cpp
    src/w_time.cpp
    src/ri_win32.cpp
)

target_include_directories(window_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)