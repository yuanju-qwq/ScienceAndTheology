# ScienceAndTheology game host.
#
# This module owns all game content and package assembly. The engine remains a
# library: it receives explicit runtime paths and never reaches into this tree.

# Current-only resource identity and storage contracts. The game owns this
# domain; legacy src/Godot resource APIs are migration references only.
add_library(snt_game_resources STATIC
    resources/resource_key.cpp
    resources/resource_ledger_storage.cpp
    resources/resource_runtime_index.cpp
)
target_include_directories(snt_game_resources PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_resources PUBLIC
    snt_engine_settings
    snt_core
)

# Game automation owns AE/SFM behavior above the compact resource-storage
# contract. It deliberately depends on game resources instead of the retired
# src/Godot resource model.
add_library(snt_game_automation STATIC
    automation/ae_storage_cell.cpp
    automation/sfm_endpoint_registry.cpp
    automation/sfm_resource_transfer.cpp
)
target_include_directories(snt_game_automation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_automation PUBLIC
    snt_engine_settings
    snt_game_resources
)

# ScienceAndTheology world rules are game-owned. The engine supplies only
# generic voxel/chunk primitives and raw region blob framing.
add_library(snt_game_world_data STATIC
    world/defs/block_entity_registry.cpp
    world/defs/creature_species.cpp
    world/defs/machine_collision_overlay.cpp
    world/defs/machine_structure_validator.cpp
)
target_include_directories(snt_game_world_data PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_world_data PUBLIC
    snt_engine_settings
    snt_voxel_data
    snt_game_resources
)

add_library(snt_game_worldgen STATIC
    worldgen/noise_generator.cpp
    worldgen/world_gen_config.cpp
    worldgen/terrain_generator.cpp
)
target_include_directories(snt_game_worldgen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_worldgen PUBLIC
    snt_engine_settings
    snt_core
    snt_voxel_data
    snt_game_world_data
)

add_library(snt_game_world_save STATIC
    world/save/chunk_serializer.cpp
    world/save/player_state_persistence.cpp
    world/save/quest_progress_persistence.cpp
    world/save/save_manager.cpp
    world/save/world_persistence_lifecycle.cpp
)
target_include_directories(snt_game_world_save PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_world_save PUBLIC
    snt_engine_settings
    snt_core
    snt_ecs
    snt_voxel_data
    snt_voxel_storage
    snt_game_world_data
)

add_library(snt_game_world_mobile STATIC
    world/mobile/dynamic_structure.cpp
    world/mobile/ship_local_grid.cpp
    world/mobile/ship_structure.cpp
)
target_include_directories(snt_game_world_mobile PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_world_mobile PUBLIC
    snt_engine_settings
    snt_voxel_data
    snt_game_world_data
    snt_game_worldgen
)

# Stable player identities are game-owned and reusable by the headless server,
# client startup flow, network authentication, and gameplay persistence. This
# target has no SDL, Vulkan, Steamworks, or simulation dependency.
add_library(snt_game_identity STATIC
    player/local_player_profile_store.cpp
    player/player_identity.cpp
)
target_include_directories(snt_game_identity PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_identity PUBLIC
    snt_engine_settings
    snt_core
)

# Game text is content, not an engine service. This target stays headless so
# callers can validate catalogs without pulling in SDL, Vulkan, or retained UI.
add_library(snt_game_localization STATIC
    localization/localization.cpp
)
target_include_directories(snt_game_localization PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_localization PUBLIC
    snt_engine_settings
    snt_core
)
target_link_libraries(snt_game_localization PRIVATE
    nlohmann_json::nlohmann_json
)

# Canonical chemistry facts are immutable game data. Script-owned materials
# and Mod content depend on this boundary but cannot mutate it.
add_library(snt_game_chemistry STATIC
    chemistry/element_catalog.cpp
)
target_include_directories(snt_game_chemistry PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_chemistry PUBLIC
    snt_engine_settings
    snt_core
)

# Game-owned authoritative replication semantics. This target stays headless:
# snt_network owns transport framing while this module owns game message
# versions, authenticated command admission, and AOI/snapshot value contracts.
add_library(snt_game_network STATIC
    network/game_account_peer_authenticator.cpp
    network/game_chunk_replication.cpp
    network/game_client_replication_session.cpp
    network/game_creature_replication.cpp
    network/game_inventory_replication.cpp
    network/game_lan_discovery.cpp
    network/game_quest_book_replication.cpp
    network/game_replication_protocol.cpp
    network/game_server_replication_handler.cpp
    player/player_replication.cpp
)
target_include_directories(snt_game_network PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_network PUBLIC
    snt_engine_settings
    snt_core
    snt_game_resources
    snt_game_identity
    snt_network
    snt_ecs
    snt_voxel_data
    snt_game_world_data
)

# Shared deterministic gameplay closure. Both the graphical client and the
# dedicated server link this target; it has no SDL, Vulkan, renderer, player,
# scene, or UI dependency.
add_library(snt_game_simulation STATIC
    chemistry/builtin_element_material_catalog.cpp
    client/game_session_config.cpp
    client/demo_world_bootstrap.cpp
    client/game_content_registry.cpp
    client/machine_tick_system.cpp
    quest/quest_registry.cpp
    simulation/day_night_cycle.cpp
    simulation/block_physics_system.cpp
    simulation/crop_growth_system.cpp
    simulation/ecosystem_system.cpp
    simulation/wild_creature_system.cpp
    simulation/fluid_lattice_boltzmann.cpp
    simulation/game_content_reload_service.cpp
    simulation/game_fluid_system.cpp
    simulation/machine_interaction_service.cpp
    simulation/machine_placement_registry.cpp
    simulation/machine_runtime_persistence.cpp
    simulation/offline_machine_simulation.cpp
    simulation/offline_network_island_registry.cpp
    simulation/offline_industrial_network_island.cpp
    simulation/region_topology.cpp
    simulation/science_and_theology_simulation_session.cpp
    simulation/season_cycle.cpp
    simulation/tree_growth_system.cpp
    simulation/worldgen_script_content.cpp
)
target_include_directories(snt_game_simulation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_simulation PUBLIC
    snt_engine_settings
    snt_simulation_runtime
    snt_core
    snt_game_resources
    snt_ecs
    snt_script
    snt_voxel_data
    snt_game_world_data
    snt_game_worldgen
    snt_game_chemistry
)
target_link_libraries(snt_game_simulation PRIVATE
    nlohmann_json::nlohmann_json
    snt_game_world_save
)

add_executable(snt_game_client
    client/main.cpp
    runtime/local_player_name_prompt.cpp
    runtime/runtime_package.cpp
    client/client_block_interaction.cpp
    client/creature_interaction.cpp
    client/creature_presentation_world.cpp
    client/day_night_lighting.cpp
    client/inventory_slot_transaction.cpp
    client/gameplay_ui.cpp
    client/lan_server_browser.cpp
    client/quest_book_ui.cpp
    client/science_and_theology_session.cpp
)
target_include_directories(snt_game_client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_client PRIVATE
    snt_client_runtime
    snt_game_identity
    snt_game_localization
    snt_game_network
    snt_game_simulation
    snt_engine_settings
    snt_core
    snt_ecs
    snt_render_components
    snt_gpu_assets
    snt_scene
    snt_player
    snt_voxel
    snt_ui
    snt_third_party
)
set_target_properties(snt_game_client PROPERTIES OUTPUT_NAME science_and_theology)

# Dedicated host baseline. It reuses the exact game simulation session but
# intentionally excludes client presentation modules and transport choices.
add_executable(snt_game_server
    server/main.cpp
    server/game_server_command_sink.cpp
    server/game_server_creature_interaction.cpp
    server/game_server_creature_replication.cpp
    server/game_server_inventory_replication.cpp
    server/game_server_player_death.cpp
    server/game_server_player_interaction.cpp
    server/game_server_quest_book_replication.cpp
    server/game_server_quest_events.cpp
    server/game_server_player_lifecycle.cpp
    server/game_server_player_movement.cpp
    server/game_server_player_replication.cpp
    server/game_server_player_state.cpp
    server/server_chunk_ticket_controller.cpp
    server/science_and_theology_server_session.cpp
    runtime/runtime_package.cpp
)
target_include_directories(snt_game_server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_server PRIVATE
    snt_engine_settings
    snt_core
    snt_simulation_runtime
    snt_game_simulation
    snt_game_network
    snt_game_world_save
    snt_voxel_collision
)
set_target_properties(snt_game_server PROPERTIES OUTPUT_NAME science_and_theology_server)

# Keep the dedicated-server closure honest. The engine already audits its own
# module includes; this host-level check rejects the presentation targets that
# would reintroduce SDL/Vulkan into the shared game simulation or server.
function(snt_assert_headless_game_target target)
    get_target_property(target_links ${target} LINK_LIBRARIES)
    foreach(forbidden_target IN ITEMS
            snt_third_party
            snt_client_runtime
            snt_platform
            snt_input
            snt_render_backend
            snt_gpu_assets
            snt_renderer
            snt_render
            snt_ui
            snt_voxel
            snt_player)
        list(FIND target_links ${forbidden_target} forbidden_index)
        if(NOT forbidden_index EQUAL -1)
            message(FATAL_ERROR
                "Headless target '${target}' links presentation dependency "
                "'${forbidden_target}'. Move it to snt_game_client instead.")
        endif()
    endforeach()
endfunction()

snt_assert_headless_game_target(snt_game_simulation)
snt_assert_headless_game_target(snt_game_server)

# Development smoke test for game-owned world generation plus the generic ECS
# benchmark. It belongs to the host because terrain rules are not engine API.
add_executable(snt_game_world_test
    tools/game_world_test.cpp
)
target_include_directories(snt_game_world_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_world_test PRIVATE
    snt_engine_settings
    snt_core
    snt_ecs
    snt_game_worldgen
)

if(SNT_BUILD_TESTS AND TARGET GTest::gtest)
    # Content registration and worker simulation are intentionally headless.
    # Keep their tests on the same small closure so script/content reload work
    # can be validated without the client, transport, or server test sources.
    add_executable(snt_game_content_tests
        tests/test_ae_storage_cell.cpp
        tests/test_element_catalog.cpp
        tests/test_game_content_registry.cpp
        tests/test_game_script_content.cpp
        tests/test_machine_tick_system.cpp
        tests/test_resource_key.cpp
        tests/test_sfm_resource_transfer.cpp
    )
    target_include_directories(snt_game_content_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/client"
    )
    target_compile_definitions(snt_game_content_tests PRIVATE
        SNT_ENGINE_TEST_ROOT="${SNT_ENGINE_SOURCE_DIR}"
        SNT_GAME_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
    target_link_libraries(snt_game_content_tests PRIVATE
        snt_game_automation
        snt_game_simulation
        snt_engine_settings
        GTest::gtest_main
    )

    # Creature presentation is intentionally a small authority/presentation
    # closure. Keep its regression tests independent from persistence and the
    # graphical runtime so protocol and AOI changes can be validated quickly.
    add_executable(snt_game_creature_tests
        client/creature_interaction.cpp
        server/game_server_creature_replication.cpp
        tests/test_client_creature_interaction.cpp
        tests/test_game_creature_replication.cpp
    )
    target_include_directories(snt_game_creature_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/client"
    )
    target_link_libraries(snt_game_creature_tests PRIVATE
        snt_engine_settings
        snt_core
        snt_voxel_data
        snt_game_world_data
        snt_game_network
        GTest::gtest_main
    )

    add_executable(snt_game_tests
        client/client_block_interaction.cpp
        client/creature_interaction.cpp
        client/creature_presentation_world.cpp
        client/day_night_lighting.cpp
        client/inventory_slot_transaction.cpp
        client/gameplay_ui.cpp
        client/lan_server_browser.cpp
        client/quest_book_ui.cpp
        server/game_server_command_sink.cpp
        server/game_server_creature_interaction.cpp
        server/game_server_creature_replication.cpp
        server/game_server_inventory_replication.cpp
        server/game_server_player_death.cpp
        server/game_server_player_interaction.cpp
        server/game_server_quest_book_replication.cpp
        server/game_server_quest_events.cpp
        server/game_server_player_lifecycle.cpp
        server/game_server_player_movement.cpp
        server/game_server_player_replication.cpp
        server/game_server_player_state.cpp
        server/server_chunk_ticket_controller.cpp
        server/science_and_theology_server_session.cpp
        tests/test_game_world.cpp
        tests/test_block_physics_system.cpp
        tests/test_game_fluid_system.cpp
        tests/test_day_night_cycle.cpp
        tests/test_season_cycle.cpp
        tests/test_region_topology.cpp
        tests/test_crop_growth_system.cpp
        tests/test_creature_presentation_world.cpp
        tests/test_ecosystem_system.cpp
        tests/test_wild_creature_system.cpp
        tests/test_tree_growth_system.cpp
        tests/test_localization.cpp
        tests/test_gameplay_ui.cpp
        tests/test_client_block_interaction.cpp
        tests/test_client_creature_interaction.cpp
        tests/test_quest_book_ui.cpp
        tests/test_machine_interaction_service.cpp
        tests/test_machine_placement_registry.cpp
        tests/test_machine_structure_validator.cpp
        tests/test_machine_runtime_persistence.cpp
        tests/test_offline_machine_simulation.cpp
        tests/test_mobile_structure.cpp
        tests/test_game_components.cpp
        tests/test_player_identity.cpp
        tests/test_player_state_persistence.cpp
        tests/test_player_replication.cpp
        tests/test_game_server_player_death.cpp
        tests/test_game_server_player_interaction.cpp
        tests/test_game_server_quest_events.cpp
        tests/test_game_server_player_movement.cpp
        tests/test_game_server_player_state.cpp
        tests/test_game_quest_book_replication.cpp
        tests/test_game_inventory_replication.cpp
        tests/test_game_creature_replication.cpp
        tests/test_quest_registry.cpp
        tests/test_game_replication.cpp
        tests/test_simulation_session.cpp
        tests/test_server_chunk_ticket_controller.cpp
        tests/test_server_session.cpp
    )
    target_include_directories(snt_game_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/client"
    )
    target_compile_definitions(snt_game_tests PRIVATE
        SNT_ENGINE_TEST_ROOT="${SNT_ENGINE_SOURCE_DIR}"
        SNT_GAME_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
    target_link_libraries(snt_game_tests PRIVATE
        snt_game_simulation
        snt_core
        snt_ecs
        snt_script
        snt_ui
        snt_voxel_data
        snt_voxel_collision
        snt_voxel_storage
        snt_game_world_data
        snt_game_worldgen
        snt_game_world_save
        snt_game_world_mobile
        snt_game_identity
        snt_game_localization
        snt_game_network
        snt_engine_settings
        snt_render_components
        snt_gpu_assets
        snt_third_party
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(snt_game_content_tests DISCOVERY_MODE PRE_TEST)
    gtest_discover_tests(snt_game_creature_tests DISCOVERY_MODE PRE_TEST)
    gtest_discover_tests(snt_game_tests DISCOVERY_MODE PRE_TEST)

    add_custom_command(TARGET snt_game_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:snt_game_tests>
            $<TARGET_FILE_DIR:snt_game_tests>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to game tests"
    )
endif()

# The packaged runtime layout is intentionally simple and independent from the
# development submodule name:
#   <exe-dir>/engine  engine resources
#   <exe-dir>/game    game content
#   <exe-dir>/user    writable logs, saves and caches (created at runtime)
function(snt_assemble_game_content target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/config
            $<TARGET_FILE_DIR:${target}>/game/config
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts
            $<TARGET_FILE_DIR:${target}>/game/scripts
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/../test_assets
            $<TARGET_FILE_DIR:${target}>/game/assets/dev
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/../resource/terrain
            $<TARGET_FILE_DIR:${target}>/game/assets/terrain
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/../resource/items
            $<TARGET_FILE_DIR:${target}>/game/resource/items
        COMMENT "Assembling ScienceAndTheology game content"
    )
endfunction()

add_dependencies(snt_game_client snt_shaders)
add_custom_command(TARGET snt_game_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SNT_SHADER_OUTPUT_DIR}
        $<TARGET_FILE_DIR:snt_game_client>/engine/shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:snt_game_client>/engine/third_party/icu4c
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${SNT_ENGINE_SOURCE_DIR}/third_party/icu4c/icudt_godot.dat
        $<TARGET_FILE_DIR:snt_game_client>/engine/third_party/icu4c/icudt_godot.dat
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scenes
        $<TARGET_FILE_DIR:snt_game_client>/game/scenes
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/locales
        $<TARGET_FILE_DIR:snt_game_client>/game/locales
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:snt_game_client>
        $<TARGET_FILE_DIR:snt_game_client>
    COMMAND_EXPAND_LISTS
    COMMENT "Assembling ScienceAndTheology client resources"
)
snt_assemble_game_content(snt_game_client)

add_custom_command(TARGET snt_game_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:snt_game_server>/engine
    COMMENT "Creating ScienceAndTheology dedicated-server engine root"
)
snt_assemble_game_content(snt_game_server)
