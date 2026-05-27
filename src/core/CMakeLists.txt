# Core systems library (pure C++, no engine dependency)
# This library is testable independently of Godot.

add_library(snt_core STATIC)

target_include_directories(snt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Fluid system (universal)
target_sources(snt_core PRIVATE
    fluid/fluid_registry.cpp
)

# Machine/material systems
target_sources(snt_core PRIVATE
    material/material_item.cpp
    material/material.cpp
    machine/machine.cpp
    machine/recipe.cpp
    machine/module.cpp
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
    world/world_data.hpp
)

# World generation system
target_sources(snt_core PRIVATE
    world_gen/world_seed.hpp
    world_gen/noise_generator.hpp
    world_gen/noise_generator.cpp
    world_gen/terrain_generator.hpp
    world_gen/terrain_generator.cpp
)

# Threading / async support
target_sources(snt_core PRIVATE
    threading/worker_pool.hpp
    threading/worker_pool.cpp
)

# Save / load system
target_sources(snt_core PRIVATE
    save/chunk_serializer.hpp
    save/chunk_serializer.cpp
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
