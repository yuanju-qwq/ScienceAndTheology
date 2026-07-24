# Core systems library (pure C++, no engine dependency)
# This library is testable independently of Godot.

add_library(snt_core STATIC)

target_include_directories(snt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(snt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(MSVC)
    target_compile_options(snt_core PRIVATE /FS /utf-8)
endif()

# Fluid system (universal)
target_sources(snt_core PRIVATE
    fluid/fluid_registry.cpp
)

# Loot table system (weighted drop tables for blocks, creatures, etc.)
target_sources(snt_core PRIVATE
    loot/loot_table_registry.cpp
)

# Fuel system (item + fluid based)
target_sources(snt_core PRIVATE
    fuel/fuel_registry.cpp
)

# Machine/material systems
target_sources(snt_core PRIVATE
    material/material_item.cpp
    material/material.cpp
    machine/recipe.cpp
    machine/machine_definition_registry.cpp
)

# Player systems
target_sources(snt_core PRIVATE
    player/player_handle.hpp
    player/player_state.hpp
    player/player_manager.hpp
    player/player_manager.cpp
    player/inventory.cpp
    player/equipment.cpp
    player/accessory.cpp
    player/tool_def.cpp
    player/satiation_data.cpp
    player/player_vitals.cpp
)

# Common shared systems (used by both GT and AE)
target_sources(snt_core PRIVATE
    common/resource_key.cpp
    common/string_pool.cpp
)

# Crafting system (manual, universal)
target_sources(snt_core PRIVATE
    crafting/crafting.cpp
)

# Network systems (power, fluid, item transport, signal)
target_sources(snt_core PRIVATE
    network/voxel_network_graph.hpp
    network/voxel_network_graph.cpp
    network/signal_network.hpp
    network/signal_network.cpp
    network/power_network.hpp
    network/power_network.cpp
    network/network_topology_builder.hpp
    network/network_topology_builder.cpp
    network/fluid_network.cpp
    network/item_pipe_network.cpp
)

# World data system
target_sources(snt_core PRIVATE
    world/terrain_data.hpp
    world/chunk_data.hpp
    world/gameplay_config.hpp
    world/world_data.hpp
    world/entity_data.hpp
    world/block_entity.hpp
    world/block_entity_registry.hpp
    world/block_entity_registry.cpp
    world/machine_collision_overlay.hpp
    world/machine_collision_overlay.cpp
)

# Multiblock system (GT V3 architecture: element → template → definition → runtime → controller)
target_sources(snt_core PRIVATE
    multiblock/structure_element.hpp
    multiblock/structure_element.cpp
    multiblock/piece_template.hpp
    multiblock/piece_template.cpp
    multiblock/structure_definition.hpp
    multiblock/structure_definition.cpp
    multiblock/structure_runtime.hpp
    multiblock/structure_runtime.cpp
    multiblock/multiblock_controller.hpp
    multiblock/multiblock_controller.cpp
    multiblock/multiblock_machine_service.hpp
    multiblock/multiblock_machine_service.cpp
)

# Simulation system (tick orchestration, event bus, state sync)
target_sources(snt_core PRIVATE
    simulation/event_types.hpp
    simulation/event_types.cpp
    simulation/event_bus.hpp
    simulation/event_bus.cpp
    simulation/simulation_system.hpp
    simulation/tick_profiler.hpp
    simulation/tick_profiler.cpp
    simulation/tick_system.hpp
    simulation/tick_system.cpp
    simulation/season_def.hpp
    simulation/season_system.hpp
    simulation/season_system.cpp
    simulation/day_night_def.hpp
    simulation/day_night_system.hpp
    simulation/day_night_system.cpp
    simulation/tile_fluid_system.hpp
    simulation/tile_fluid_system.cpp
    simulation/satiation_system.hpp
    simulation/satiation_system.cpp
    simulation/state_sync_common.hpp
    simulation/state_sync_server.hpp
    simulation/state_sync_server.cpp
    simulation/state_sync_client.hpp
    simulation/state_sync_client.cpp
    simulation/prediction_buffer.hpp
    simulation/prediction_buffer.cpp
)

# Future modules:
# target_sources(snt_core PRIVATE
#     common/graph/graph.cpp
# )
# Magic system (runes, glyphs, spell books, altars, rituals)
target_sources(snt_core PRIVATE
    magic/rune_registry.cpp
    magic/glyph_registry.cpp
    magic/glyph_conversion.cpp
    magic/spell_compiler.cpp
    magic/ritual_recipe_registry.cpp
    magic/spell_effect/spell_effect_base.cpp
)

# Source Law Sublimation system (V0.1: unified base data, V0.2: initiation & skills)
target_sources(snt_core PRIVATE
    source_law/source_law_types.cpp
    source_law/player_source_law_data.cpp
    source_law/elixir_registry.cpp
    source_law/sublimation_path_registry.cpp
    source_law/dropped_organ_registry.cpp
    source_law/organ_skill_system.cpp
    source_law/mutation_system.cpp
)

# Quest system (progress tracking, condition evaluation, serialization)
target_sources(snt_core PRIVATE
    quest/quest_system.cpp
)

# Universe core (U1: unified coordinates, Sector model, storage shard migration)
# Design: docs/unified_universe_world_design.md sections 2~5, 21.2 (U1)
target_sources(snt_core PRIVATE
    universe/universe_types.hpp
    universe/storage_shard.hpp
    universe/storage_shard.cpp
    universe/sector_manager.hpp
    universe/sector_manager.cpp
    universe/universe_world_core.hpp
    universe/universe_world_core.cpp
    universe/planet_build_frame.hpp
    universe/planet_build_frame.cpp
)

# Universe streaming (U2: Sector-aware chunk streaming, sparse chunks, entity migration, floating origin)
# Design: docs/unified_universe_world_design.md sections 3.2, 8.2, 10, 17, 21.2 (U2)
target_sources(snt_core PRIVATE
    universe/chunk_streaming_system.hpp
    universe/chunk_streaming_system.cpp
    universe/sparse_chunk_policy.hpp
    universe/sparse_chunk_policy.cpp
)

# Universe navigation / planets / simulation / release support (U3-U8)
target_sources(snt_core PRIVATE
    universe/aoi_budget_manager.hpp
    universe/aoi_budget_manager.cpp
    universe/block_space.hpp
    universe/block_space.cpp
    universe/celestial_lod_pipeline.hpp
    universe/celestial_lod_pipeline.cpp
    universe/celestial_lod_system.hpp
    universe/celestial_lod_system.cpp
    universe/cross_sector_relay.hpp
    universe/cross_sector_relay.cpp
    universe/entity_sector_tracker.hpp
    universe/entity_sector_tracker.cpp
    universe/flight_state_tracker.hpp
    universe/flight_state_tracker.cpp
    universe/floating_origin_core.hpp
    universe/floating_origin_core.cpp
    universe/gravity_field_manager.hpp
    universe/gravity_field_manager.cpp
    universe/interest_manager.hpp
    universe/interest_manager.cpp
    universe/multi_sector_sync_coordinator.hpp
    universe/multi_sector_sync_coordinator.cpp
    universe/network_isolation_guard.hpp
    universe/network_isolation_guard.cpp
    universe/performance_budget_monitor.hpp
    universe/performance_budget_monitor.cpp
    universe/save_reliability_manager.hpp
    universe/save_reliability_manager.cpp
    universe/sector_observer_map.hpp
    universe/sector_observer_map.cpp
    universe/sector_simulation_scheduler.hpp
    universe/sector_simulation_scheduler.cpp
    universe/sector_transition_manager.hpp
    universe/sector_transition_manager.cpp
    universe/space_station_sector.hpp
    universe/space_station_sector.cpp
    universe/structure_anchor_manager.hpp
    universe/structure_anchor_manager.cpp
    universe/virtual_planet_simulator.hpp
    universe/virtual_planet_simulator.cpp
)
