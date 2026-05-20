# Engine adapter interfaces (header-only, pure virtual)
# These define the contract between core systems and any game engine.

add_library(snt_adapters INTERFACE)
target_include_directories(snt_adapters INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})