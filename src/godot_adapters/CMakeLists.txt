# Godot-specific implementations of engine adapter interfaces.

add_library(snt_godot_adapters STATIC)
target_include_directories(snt_godot_adapters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_godot_adapters PUBLIC snt_adapters snt_core godot-cpp)

target_sources(snt_godot_adapters PRIVATE
    godot_adapters.cpp
    terrain_material_palette.hpp
    terrain_material_palette.cpp
)