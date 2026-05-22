# Core systems library (pure C++, no engine dependency)
# This library is testable independently of Godot.

add_library(snt_core STATIC)

target_include_directories(snt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# GT systems
target_sources(snt_core PRIVATE
    gt/material/item.cpp
    gt/material/material.cpp
    gt/crafting/crafting.cpp
    gt/machine/machine.cpp
    gt/machine/recipe.cpp
    gt/machine/module.cpp
    gt/power/power_network.cpp
    gt/fluid/fluid_types.cpp
    gt/fluid/fluid_network.cpp
    gt/transport/item_pipe_network.cpp
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
#     ae2/network/network_controller.cpp
#     gt/machine/machine_controller.cpp
#     common/graph/graph.cpp
# )