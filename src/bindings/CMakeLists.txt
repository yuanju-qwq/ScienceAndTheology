# GDExtension shared library: bridges C++ core logic to GDScript.

set(SOURCES
    register_types.cpp
    hello_world/gd_hello_world.cpp
    gd_power_network.cpp
    gd_crafting.cpp
    gd_fluid_network.cpp
    gd_item_pipe_network.cpp
    gd_machine.cpp
    world_bindings/gd_world_data.cpp
    world_bindings/gd_terrain_generator.cpp
)

add_library(science_and_theology SHARED ${SOURCES})

target_include_directories(science_and_theology PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(science_and_theology PRIVATE godot-cpp snt_core snt_adapters snt_godot_adapters)

# Platform-specific output settings
set_target_properties(science_and_theology PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../bin"
)

# Windows: ensure DLL exports are generated
if(WIN32)
    set_target_properties(science_and_theology PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS TRUE
    )
endif()