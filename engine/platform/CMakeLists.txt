# Platform abstraction: window/input/filesystem via SDL3.

add_library(snt_platform STATIC
    window.cpp
)
target_include_directories(snt_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_platform PUBLIC snt_engine_settings snt_third_party)
