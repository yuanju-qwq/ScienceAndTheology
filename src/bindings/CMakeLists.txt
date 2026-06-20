# GDExtension shared library: bridges C++ core logic to GDScript.

set(SOURCES
    register_types.cpp
    network/gd_power_network.cpp
    crafting/gd_crafting.cpp
    ae2/gd_ae2_autocrafting.cpp
    ae2/gd_me_network.cpp
    network/gd_fluid_network.cpp
    network/gd_item_pipe_network.cpp
    machine/gd_machine.cpp
    world/gd_world_data.cpp
    world/gd_world_gen_config.cpp
    world/gd_terrain_content_registry.cpp
    world/gd_terrain_generator.cpp
    world/gd_furnace_manager.cpp
    world/gd_chunk_helper.cpp
    world/gd_planet_lod.cpp
    simulation/gd_game_command_server.cpp
    simulation/gd_tick_system.cpp
    player/gd_player_inventory.cpp
    player/gd_player_equipment.cpp
    player/gd_player_accessory.cpp
    player/gd_player_helper.cpp
    player/gd_satiation_data.cpp
    magic/gd_rune_registry.cpp
    magic/gd_glyph_registry.cpp
    magic/gd_glyph_conversion.cpp
    magic/gd_source_law_weapon.cpp
    magic/gd_spell_book.cpp
    magic/gd_mana_pool.cpp
    fuel/gd_fuel_registry.cpp
    machine/gd_recipe_database.cpp
    source_law/gd_player_source_law_data.cpp
    quest/gd_quest_system.cpp
    multiblock/gd_multiblock_builder.cpp
    multiblock/gd_multiblock_controller.cpp
    multiplayer/gd_network_server.cpp
    multiplayer/gd_network_client.cpp
    multiplayer/gd_prediction_buffer.cpp
)

add_library(science_and_theology SHARED ${SOURCES})

target_include_directories(science_and_theology PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(science_and_theology PRIVATE godot-cpp snt_core snt_server snt_adapters snt_godot_adapters)

if(MSVC)
    target_compile_options(science_and_theology PRIVATE /FS /utf-8)
endif()

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
