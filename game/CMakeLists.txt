# ScienceAndTheology game host.
#
# This module owns all game content and package assembly. The engine remains a
# library: it receives explicit runtime paths and never reaches into this tree.

# ScienceAndTheology world rules are game-owned. The engine supplies only
# generic voxel/chunk primitives and raw region blob framing.
add_library(snt_game_world_data STATIC
    world/defs/block_entity_registry.cpp
    world/defs/creature_species.cpp
    world/defs/machine_collision_overlay.cpp
)
target_include_directories(snt_game_world_data PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_world_data PUBLIC
    snt_engine_settings
    snt_voxel_data
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
    snt_voxel_data
    snt_game_world_data
)

add_library(snt_game_world_save STATIC
    world/save/chunk_serializer.cpp
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

# Game-owned authoritative replication semantics. This target stays headless:
# snt_network owns transport framing while this module owns game message
# versions, authenticated command admission, and future AOI/snapshot contracts.
add_library(snt_game_network STATIC
    network/game_account_peer_authenticator.cpp
    network/game_client_replication_session.cpp
    network/game_replication_protocol.cpp
    network/game_server_replication_handler.cpp
)
target_include_directories(snt_game_network PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_network PUBLIC
    snt_engine_settings
    snt_core
    snt_game_identity
    snt_network
    snt_ecs
    snt_voxel_data
)

# Shared deterministic gameplay closure. Both the graphical client and the
# dedicated server link this target; it has no SDL, Vulkan, renderer, player,
# scene, or UI dependency.
add_library(snt_game_simulation STATIC
    client/game_session_config.cpp
    client/demo_world_bootstrap.cpp
    client/game_content_registry.cpp
    client/machine_tick_system.cpp
    quest/quest_registry.cpp
    simulation/science_and_theology_simulation_session.cpp
)
target_include_directories(snt_game_simulation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_simulation PUBLIC
    snt_engine_settings
    snt_simulation_runtime
    snt_core
    snt_ecs
    snt_script
    snt_voxel_data
    snt_game_world_data
    snt_game_worldgen
)
target_link_libraries(snt_game_simulation PRIVATE
    nlohmann_json::nlohmann_json
    snt_game_world_save
)

add_executable(snt_game_client
    client/main.cpp
    runtime/local_player_name_prompt.cpp
    runtime/runtime_package.cpp
    client/gameplay_ui.cpp
    client/science_and_theology_session.cpp
)
target_include_directories(snt_game_client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_client PRIVATE
    snt_client_runtime
    snt_game_identity
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
    server/game_server_player_lifecycle.cpp
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
    add_executable(snt_game_tests
        client/gameplay_ui.cpp
        server/game_server_command_sink.cpp
        server/game_server_player_lifecycle.cpp
        server/science_and_theology_server_session.cpp
        tests/test_game_world.cpp
        tests/test_gameplay_ui.cpp
        tests/test_game_content_registry.cpp
        tests/test_game_script_content.cpp
        tests/test_machine_tick_system.cpp
        tests/test_game_components.cpp
        tests/test_player_identity.cpp
        tests/test_quest_registry.cpp
        tests/test_game_replication.cpp
        tests/test_simulation_session.cpp
        tests/test_server_session.cpp
    )
    target_include_directories(snt_game_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/client"
    )
    target_compile_definitions(snt_game_tests PRIVATE
        SNT_ENGINE_TEST_ROOT="${SNT_ENGINE_SOURCE_DIR}"
    )
    target_link_libraries(snt_game_tests PRIVATE
        snt_game_simulation
        snt_core
        snt_ecs
        snt_script
        snt_ui
        snt_voxel_data
        snt_voxel_storage
        snt_game_world_data
        snt_game_worldgen
        snt_game_world_save
        snt_game_identity
        snt_game_network
        snt_engine_settings
        snt_third_party
        GTest::gtest_main
    )

    include(GoogleTest)
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
