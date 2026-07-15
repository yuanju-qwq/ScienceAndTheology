add_executable(snt_u0_world_data_smoke
    u0_world_data_smoke.cpp
)

target_link_libraries(snt_u0_world_data_smoke PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u0_world_data_smoke PRIVATE /utf-8)
endif()

add_test(NAME snt.u0.world_data_smoke COMMAND snt_u0_world_data_smoke)

# Machine collision overlay: per-(dimension, cell) overlay consumed by chunk
# collision generation so machines get collision without per-object
# StaticBody3D nodes. Current runtime boundary: docs/项目架构与运行时.md.
add_executable(snt_machine_collision_overlay
    machine_collision_overlay.cpp
)

target_link_libraries(snt_machine_collision_overlay PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_machine_collision_overlay PRIVATE /utf-8)
endif()

add_test(NAME snt.machine_collision_overlay COMMAND snt_machine_collision_overlay)

# Player vitals: per-frame health/hunger/atmosphere simulation sunk from
# PlayerController.gd. Tests the pure C++ core (PlayerVitals) directly.
# Current runtime boundary: docs/项目架构与运行时.md.
add_executable(snt_player_vitals
    player_vitals.cpp
)

target_link_libraries(snt_player_vitals PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_player_vitals PRIVATE /utf-8)
endif()

add_test(NAME snt.player_vitals COMMAND snt_player_vitals)

# Signal network: per-block digital wire conduction, OR-semantics, components
# Current world-network boundary: docs/unified_universe_world_design.md §14.
add_executable(snt_signal_network_system
    signal_network_system.cpp
)

target_link_libraries(snt_signal_network_system PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_signal_network_system PRIVATE /utf-8)
endif()

add_test(NAME snt.signal_network_system COMMAND snt_signal_network_system)

# Power network: per-block cable conduction, shared pool, per-tile loss
# Current world-network boundary: docs/unified_universe_world_design.md §14.
add_executable(snt_power_network_per_block
    power_network_per_block.cpp
)

target_link_libraries(snt_power_network_per_block PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_power_network_per_block PRIVATE /utf-8)
endif()

add_test(NAME snt.power_network_per_block COMMAND snt_power_network_per_block)

# Network topology builder: BlockEntity <-> network bridge, connection masks
# Current world-network boundary: docs/unified_universe_world_design.md §14.
add_executable(snt_network_topology_builder
    network_topology_builder.cpp
)

target_link_libraries(snt_network_topology_builder PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_network_topology_builder PRIVATE /utf-8)
endif()

add_test(NAME snt.network_topology_builder COMMAND snt_network_topology_builder)

# Block physics: origin-cell scheduling, event emission, support beam protection
add_executable(snt_block_physics_system
    block_physics_system.cpp
)

target_link_libraries(snt_block_physics_system PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_block_physics_system PRIVATE /utf-8)
endif()

add_test(NAME snt.block_physics_system COMMAND snt_block_physics_system)

# Dynamic mobile structure: static terrain -> moving entity -> write-back
add_executable(snt_mobile_structure
    mobile_structure.cpp
)

target_link_libraries(snt_mobile_structure PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_mobile_structure PRIVATE /utf-8)
endif()

add_test(NAME snt.mobile_structure COMMAND snt_mobile_structure)

# GT multiblock closure: controller -> formation -> hatch summary -> tick gate
add_executable(snt_multiblock_machine_service
    multiblock_machine_service.cpp
)

target_link_libraries(snt_multiblock_machine_service PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_multiblock_machine_service PRIVATE /utf-8)
endif()

add_test(NAME snt.multiblock_machine_service COMMAND snt_multiblock_machine_service)

# M2: Multi-player local validation (no network)
# Current multiplayer protocol: docs/游戏网络协议设计.md.
add_executable(snt_m2_multi_player
    m2_multi_player.cpp
)

target_link_libraries(snt_m2_multi_player PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_m2_multi_player PRIVATE /utf-8)
endif()

add_test(NAME snt.m2.multi_player COMMAND snt_m2_multi_player)

# M3: Protocol codec + network round-trip tests
# Current multiplayer protocol: docs/游戏网络协议设计.md.
add_executable(snt_m3_server_network
    m3_server_network.cpp
)

target_link_libraries(snt_m3_server_network PRIVATE snt_core snt_server)

if(MSVC)
    target_compile_options(snt_m3_server_network PRIVATE /utf-8)
endif()

add_test(NAME snt.m3.server_network COMMAND snt_m3_server_network)

# M5: Multi-planet concurrent tests
# Current universe design: docs/unified_universe_world_design.md.
add_executable(snt_m5_multi_planet
    m5_multi_planet.cpp
)

target_link_libraries(snt_m5_multi_planet PRIVATE snt_core snt_server)

if(MSVC)
    target_compile_options(snt_m5_multi_planet PRIVATE /utf-8)
endif()

add_test(NAME snt.m5.multi_planet COMMAND snt_m5_multi_planet)

# M6: Client prediction + rollback tests
# Legacy prediction test; client prediction has no current protocol contract.
add_executable(snt_m6_prediction
    m6_prediction.cpp
)

target_link_libraries(snt_m6_prediction PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_m6_prediction PRIVATE /utf-8)
endif()

add_test(NAME snt.m6.prediction COMMAND snt_m6_prediction)

# U1: unified universe core tests
# Design: docs/unified_universe_world_design.md section 21.2 (U1)
add_executable(snt_u1_universe_core
    u1_universe_core.cpp
)

target_link_libraries(snt_u1_universe_core PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u1_universe_core PRIVATE /utf-8)
endif()

add_test(NAME snt.u1.universe_core COMMAND snt_u1_universe_core)

# U2: Sector-aware chunk streaming, sparse chunks, entity migration, floating origin
# Design: docs/unified_universe_world_design.md section 21.2 (U2)
add_executable(snt_u2_universe_streaming
    u2_universe_streaming.cpp
)

target_link_libraries(snt_u2_universe_streaming PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u2_universe_streaming PRIVATE /utf-8)
endif()

add_test(NAME snt.u2.universe_streaming COMMAND snt_u2_universe_streaming)

# U3: flight modes, celestial LOD, unified interest management
# Design: docs/unified_universe_world_design.md section 21.2 (U3)
add_executable(snt_u3_universe_navigation
    u3_universe_navigation.cpp
)

target_link_libraries(snt_u3_universe_navigation PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u3_universe_navigation PRIVATE /utf-8)
endif()

add_test(NAME snt.u3.universe_navigation COMMAND snt_u3_universe_navigation)

# U4: second real planet, continuous landing, gravity fields, LOD pipeline
# Design: docs/unified_universe_world_design.md section 21.2 (U4)
add_executable(snt_u4_universe_planets
    u4_universe_planets.cpp
)

target_link_libraries(snt_u4_universe_planets PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u4_universe_planets PRIVATE /utf-8)
endif()

add_test(NAME snt.u4.universe_planets COMMAND snt_u4_universe_planets)

# U5: cross-sector BlockSpace, structure anchors, space station sectors
# Design: docs/unified_universe_world_design.md section 21.2 (U5)
add_executable(snt_u5_universe_building
    u5_universe_building.cpp
)

target_link_libraries(snt_u5_universe_building PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u5_universe_building PRIVATE /utf-8)
endif()

add_test(NAME snt.u5.universe_building COMMAND snt_u5_universe_building)

# U6: sector-aware simulation, cross-sector relays, network isolation, virtual planet sim
# Design: docs/unified_universe_world_design.md section 21.2 (U6)
add_executable(snt_u6_universe_simulation
    u6_universe_simulation.cpp
)

target_link_libraries(snt_u6_universe_simulation PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u6_universe_simulation PRIVATE /utf-8)
endif()

add_test(NAME snt.u6.universe_simulation COMMAND snt_u6_universe_simulation)

# U7: multi-sector AOI, per-observer budget, concurrent sector sync
# Design: docs/unified_universe_world_design.md section 21.2 (U7)
add_executable(snt_u7_universe_multiplayer
    u7_universe_multiplayer.cpp
)

target_link_libraries(snt_u7_universe_multiplayer PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u7_universe_multiplayer PRIVATE /utf-8)
endif()

add_test(NAME snt.u7.universe_multiplayer COMMAND snt_u7_universe_multiplayer)

# U8: performance budget, save reliability, stress scenarios, fault recovery
# Design: docs/unified_universe_world_design.md section 21.2 (U8)
add_executable(snt_u8_universe_release
    u8_universe_release.cpp
)

target_link_libraries(snt_u8_universe_release PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u8_universe_release PRIVATE /utf-8)
endif()

add_test(NAME snt.u8.universe_release COMMAND snt_u8_universe_release)
