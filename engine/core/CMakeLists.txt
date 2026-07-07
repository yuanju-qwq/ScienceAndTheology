# Core: Job System + Logger (foundational utilities).
# - Job System: P1 serial stub. P2 will replace with real thread pool.
# - Logger: lightweight printf-style logging with runtime level filter.
add_library(snt_core STATIC
    job_system.cpp
    log.cpp
)
target_include_directories(snt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(snt_core PUBLIC snt_engine_settings)
