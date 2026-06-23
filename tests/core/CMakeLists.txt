add_executable(snt_u0_world_data_smoke
    u0_world_data_smoke.cpp
)

target_link_libraries(snt_u0_world_data_smoke PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u0_world_data_smoke PRIVATE /utf-8)
endif()

add_test(NAME snt.u0.world_data_smoke COMMAND snt_u0_world_data_smoke)

# Block physics: origin-cell scheduling, event emission, support beam protection
add_executable(snt_block_physics_system
    block_physics_system.cpp
)

target_link_libraries(snt_block_physics_system PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_block_physics_system PRIVATE /utf-8)
endif()

add_test(NAME snt.block_physics_system COMMAND snt_block_physics_system)

# M2: Multi-player local validation (no network)
# Design: docs/多人游戏系统设计.md §6.2 M2
add_executable(snt_m2_multi_player
    m2_multi_player.cpp
)

target_link_libraries(snt_m2_multi_player PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_m2_multi_player PRIVATE /utf-8)
endif()

add_test(NAME snt.m2.multi_player COMMAND snt_m2_multi_player)

# M3: Protocol codec + network round-trip tests
# Design: docs/多人游戏系统设计.md §4 方案B
add_executable(snt_m3_server_network
    m3_server_network.cpp
)

target_link_libraries(snt_m3_server_network PRIVATE snt_core snt_server)

if(MSVC)
    target_compile_options(snt_m3_server_network PRIVATE /utf-8)
endif()

add_test(NAME snt.m3.server_network COMMAND snt_m3_server_network)

# M5: Multi-planet concurrent tests
# Design: docs/多人游戏系统设计.md §3.6 多星球并发, §6.2 M5
add_executable(snt_m5_multi_planet
    m5_multi_planet.cpp
)

target_link_libraries(snt_m5_multi_planet PRIVATE snt_core snt_server)

if(MSVC)
    target_compile_options(snt_m5_multi_planet PRIVATE /utf-8)
endif()

add_test(NAME snt.m5.multi_planet COMMAND snt_m5_multi_planet)

# M6: Client prediction + rollback tests
# Design: docs/多人游戏系统设计.md §6.2 M6, §3.4 命令协议升级
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
