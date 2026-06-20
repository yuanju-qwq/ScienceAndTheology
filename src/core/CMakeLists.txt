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

# Fuel system (item + fluid based)
target_sources(snt_core PRIVATE
    fuel/fuel_registry.cpp
)

# Machine/material systems
target_sources(snt_core PRIVATE
    material/material_item.cpp
    material/material.cpp
    machine/machine.cpp
    machine/recipe.cpp
    machine/module.cpp
)

# Player systems
target_sources(snt_core PRIVATE
    player/player_id.hpp
    player/player_state.hpp
    player/player_manager.hpp
    player/player_manager.cpp
    player/inventory.cpp
    player/equipment.cpp
    player/accessory.cpp
    player/tool_def.cpp
    player/satiation_data.cpp
)

# Common shared systems (used by both GT and AE)
target_sources(snt_core PRIVATE
    common/resource_key.cpp
)

# Crafting system (manual, universal)
target_sources(snt_core PRIVATE
    crafting/crafting.cpp
)

# Network systems (power, fluid, item transport)
target_sources(snt_core PRIVATE
    network/power_network.cpp
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
)

# World generation system
target_sources(snt_core PRIVATE
    world_gen/world_seed.hpp
    world_gen/noise_generator.hpp
    world_gen/noise_generator.cpp
    world_gen/world_gen_config.hpp
    world_gen/world_gen_config.cpp
    world_gen/terrain_generator.hpp
    world_gen/terrain_generator.cpp
    world_gen/tree_species_def.hpp
    world_gen/crop_species_def.hpp
)

# Simulation system (tick orchestration, event bus, state sync)
target_sources(snt_core PRIVATE
    simulation/event_types.hpp
    simulation/event_types.cpp
    simulation/event_bus.hpp
    simulation/event_bus.cpp
    simulation/simulation_system.hpp
    simulation/tick_system.hpp
    simulation/tick_system.cpp
    simulation/machine_system.hpp
    simulation/machine_system.cpp
    simulation/block_physics_system.hpp
    simulation/block_physics_system.cpp
    simulation/tree_growth_system.hpp
    simulation/tree_growth_system.cpp
    simulation/crop_growth_system.hpp
    simulation/crop_growth_system.cpp
    simulation/population_cell.hpp
    simulation/ecosystem_params.hpp
    simulation/ecosystem_system.hpp
    simulation/ecosystem_system.cpp
    simulation/creature_species.hpp
    simulation/creature_species.cpp
    simulation/season_def.hpp
    simulation/season_system.hpp
    simulation/season_system.cpp
    simulation/day_night_def.hpp
    simulation/day_night_system.hpp
    simulation/day_night_system.cpp
    simulation/error_handler.hpp
    simulation/error_handler.cpp
    simulation/region_graph.hpp
    simulation/region_graph.cpp
    simulation/region_system.hpp
    simulation/region_system.cpp
    simulation/tile_fluid_system.hpp
    simulation/tile_fluid_system.cpp
    simulation/satiation_system.hpp
    simulation/satiation_system.cpp
    simulation/state_sync_common.hpp
    simulation/state_sync_server.hpp
    simulation/state_sync_server.cpp
    simulation/state_sync_client.hpp
    simulation/state_sync_client.cpp
)

# Save / load system
target_sources(snt_core PRIVATE
    save/chunk_serializer.hpp
    save/chunk_serializer.cpp
    save/region_file.hpp
    save/region_file.cpp
    save/save_manager.hpp
    save/save_manager.cpp
)

# Future modules:
# target_sources(snt_core PRIVATE
#     common/graph/graph.cpp
# )
# AE2 autocrafting system
target_sources(snt_core PRIVATE
    ae2/ae2_pattern.cpp
    ae2/ae2_crafting_tree.cpp
    ae2/ae2_crafting_resolver.cpp
    ae2/ae2_crafting_cpu.cpp
    ae2/ae2_crafting_service.cpp
    ae2/ae2_pattern_provider.cpp
    ae2/ae2_pattern_cache.cpp
    ae2/ae2_resource_id.cpp
    ae2/ae2_storage_cell.cpp
    ae2/ae2_me_network.cpp
)

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
)

# Universe streaming (U2: Sector-aware chunk streaming, sparse chunks, entity migration, floating origin)
# Design: docs/unified_universe_world_design.md sections 3.2, 8.2, 10, 17, 21.2 (U2)
target_sources(snt_core PRIVATE
    universe/chunk_streaming_system.hpp
    universe/chunk_streaming_system.cpp
    universe/sparse_chunk_policy.hpp
    universe/sparse_chunk_policy.cpp
    universe/entity_sector_tracker.hpp
    universe/entity_sector_tracker.cpp
    universe/floating_origin_core.hpp
    universe/floating_origin_core.cpp
)
