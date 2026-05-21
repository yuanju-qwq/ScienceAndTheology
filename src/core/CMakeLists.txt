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
    gt/power/power_network.cpp
)

# Future modules:
# target_sources(snt_core PRIVATE
#     ae2/network/network_controller.cpp
#     gt/machine/machine_controller.cpp
#     common/graph/graph.cpp
# )