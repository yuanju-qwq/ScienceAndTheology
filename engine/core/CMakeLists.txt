# Core: Job System (multi-threading framework).
# P1: serial stub implementation. P2 will replace with real thread pool.

add_library(snt_core STATIC
    job_system.cpp
)
target_include_directories(snt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_core PUBLIC snt_engine_settings)
