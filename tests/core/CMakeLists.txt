add_executable(snt_u0_world_data_smoke
    u0_world_data_smoke.cpp
)

target_link_libraries(snt_u0_world_data_smoke PRIVATE snt_core)

if(MSVC)
    target_compile_options(snt_u0_world_data_smoke PRIVATE /utf-8)
endif()

add_test(NAME snt.u0.world_data_smoke COMMAND snt_u0_world_data_smoke)

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
