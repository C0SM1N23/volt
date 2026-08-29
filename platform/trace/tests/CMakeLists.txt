volt_add_test(trace_tests
    SOURCES trace_test.cpp
    LINK volt::trace volt::pal_posix
)

# The exported trace is checked by a script that shares no code with the
# exporter. Both paths are handed to the test rather than guessed, so it works
# from any build directory.
target_compile_definitions(trace_tests PRIVATE
    VOLT_TRACE_VALIDATOR="${CMAKE_SOURCE_DIR}/tools/trace_validate/validate_perfetto.py"
    VOLT_TRACE_SAMPLE_PATH="${CMAKE_CURRENT_BINARY_DIR}/volt_sample_trace.json"
)

# Measures the cost of a trace point, so it needs the real clock and threads.
volt_add_test(trace_benchmark_tests
    SOURCES trace_benchmark_test.cpp
    LINK volt::trace volt::pal_posix
)
