# Engine application layer: main loop, scene management.

add_executable(snt_engine_app
    main.cpp
)
target_link_libraries(snt_engine_app PRIVATE
    snt_engine_settings
    snt_platform
    snt_core
    snt_ecs
    snt_ui
    snt_render_backend
)
# Ensure shaders are compiled + copied before the engine runs.
add_dependencies(snt_engine_app snt_copy_shaders)
# Output name for convenience.
set_target_properties(snt_engine_app PROPERTIES OUTPUT_NAME snt_engine)
