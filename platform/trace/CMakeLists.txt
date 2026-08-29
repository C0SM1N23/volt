volt_add_library(trace
    SOURCES
        metrics/exposition_writer.cpp
        metrics/metric_registry.cpp
        metrics/metrics_server.cpp
        metrics/prometheus_exporter.cpp
        metrics/text_exporter.cpp
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
    add_subdirectory(metrics/tests)
endif()
