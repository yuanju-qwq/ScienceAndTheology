# ECS module: EnTT-based entity-component-system.
# P1.5: World, components, systems (camera, render).

add_library(snt_ecs STATIC
    camera_system.cpp
)
target_include_directories(snt_ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_ecs PUBLIC snt_engine_settings snt_third_party snt_platform)
