# ScienceAndTheology game host.
#
# This module owns all game content and package assembly. The engine remains a
# library: it receives explicit runtime paths and never reaches into this tree.

add_executable(snt_game_client
    client/main.cpp
)
target_link_libraries(snt_game_client PRIVATE
    snt_engine
    snt_engine_settings
)
set_target_properties(snt_game_client PROPERTIES OUTPUT_NAME science_and_theology)

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