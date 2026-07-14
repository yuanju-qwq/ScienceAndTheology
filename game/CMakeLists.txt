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
    world/save/save_manager.cpp
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

add_executable(snt_game_client
    client/main.cpp
    client/game_session_config.cpp
    client/demo_world_bootstrap.cpp
    client/game_content_registry.cpp
    client/gameplay_ui.cpp
    client/machine_tick_system.cpp
    client/science_and_theology_session.cpp
)
target_include_directories(snt_game_client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(snt_game_client PRIVATE
    snt_client_runtime
    snt_engine_settings
    snt_core
    snt_ecs
    snt_render_components
    snt_gpu_assets
    snt_scene
    snt_script
    snt_player
    snt_voxel
    snt_game_world_data
    snt_game_worldgen
    snt_ui
    snt_third_party
)
set_target_properties(snt_game_client PROPERTIES OUTPUT_NAME science_and_theology)

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
        client/game_content_registry.cpp
        client/gameplay_ui.cpp
        client/machine_tick_system.cpp
        tests/test_game_world.cpp
        tests/test_gameplay_ui.cpp
        tests/test_game_content_registry.cpp
        tests/test_game_script_content.cpp
        tests/test_machine_tick_system.cpp
        tests/test_game_components.cpp
    )
    target_include_directories(snt_game_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/client"
    )
    target_compile_definitions(snt_game_tests PRIVATE
        SNT_ENGINE_TEST_ROOT="${SNT_ENGINE_SOURCE_DIR}"
    )
    target_link_libraries(snt_game_tests PRIVATE
        snt_core
        snt_ecs
        snt_script
        snt_ui
        snt_voxel_data
        snt_voxel_storage
        snt_game_world_data
        snt_game_worldgen
        snt_game_world_save
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
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:snt_game_client>/game/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scenes
        $<TARGET_FILE_DIR:snt_game_client>/game/scenes
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:snt_game_client>/game/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/../test_assets
        $<TARGET_FILE_DIR:snt_game_client>/game/assets/dev
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/../resource/terrain
        $<TARGET_FILE_DIR:snt_game_client>/game/assets/terrain
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:snt_game_client>
        $<TARGET_FILE_DIR:snt_game_client>
    COMMAND_EXPAND_LISTS
    COMMENT "Assembling ScienceAndTheology runtime package"
)
