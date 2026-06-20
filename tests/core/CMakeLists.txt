add_executable(snt_u0_world_data_smoke
    u0_world_data_smoke.cpp
)

target_link_libraries(snt_u0_world_data_smoke PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u0_world_data_smoke PRIVATE /utf-8)
endif()

add_test(NAME snt.u0.world_data_smoke COMMAND snt_u0_world_data_smoke)

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
