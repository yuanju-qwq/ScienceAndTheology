# Engine application layer: thin executable wrapper around Engine class.
# P2.B1: main.cpp is reduced to Engine::init/run/shutdown; all subsystem
# setup lives in the snt_engine static lib.

add_executable(snt_engine_app
    main.cpp
)
target_link_libraries(snt_engine_app PRIVATE
    snt_engine_settings
    snt_engine
)
# Ensure shaders are compiled + copied before the engine runs.
add_dependencies(snt_engine_app snt_copy_shaders)
# Output name for convenience.
set_target_properties(snt_engine_app PROPERTIES OUTPUT_NAME snt_engine)
