volt_add_library(sched
    SOURCES
        src/hdr_histogram.cpp
        src/edf_scheduler.cpp
        src/periodic_runner.cpp
        src/rate_monotonic_scheduler.cpp
        src/schedule_checker.cpp
        src/schedule_generator.cpp
        src/rm_priority.cpp
        src/task_loop.cpp
        src/time_triggered_scheduler.cpp
        src/task_spec.cpp
    PUBLIC_LINK volt::pal volt::trace
)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
