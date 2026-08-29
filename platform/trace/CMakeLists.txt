volt_add_library(trace
    SOURCES
        src/cycle_clock.cpp
        src/perfetto_export.cpp
        src/trace_capture.cpp
        src/trace_file.cpp
        src/trace_ring.cpp
        src/tracer.cpp
    PUBLIC_LINK volt::pal
)

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
