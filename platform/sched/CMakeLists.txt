volt_add_library(sched
    SOURCES
        src/hdr_histogram.cpp
        src/rate_monotonic_scheduler.cpp
        src/rm_priority.cpp
        src/task_loop.cpp
        src/task_spec.cpp
    PUBLIC_LINK volt::pal volt::trace
)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
