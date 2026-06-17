# Godot-specific implementations of engine adapter interfaces.

add_library(snt_godot_adapters STATIC)
target_include_directories(snt_godot_adapters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_godot_adapters PUBLIC snt_adapters snt_core godot-cpp)

if(MSVC)
    target_compile_options(snt_godot_adapters PRIVATE /FS /utf-8)
endif()

target_sources(snt_godot_adapters PRIVATE
    godot_adapters.cpp
)
