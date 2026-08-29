volt_add_test(metrics_tests
    SOURCES metrics_test.cpp
    LINK volt::trace volt::memory volt::pal_posix
)

# Its own target because the reference campaign records a million samples and
# sorts them: a correctness measurement does not belong in the same binary as
# suites that finish in microseconds.
volt_add_test(metrics_histogram_tests
    SOURCES histogram_test.cpp
    LINK volt::trace
)

# Its own target because it opens real sockets and starts real threads, which
# is a different kind of failure from a formatting mistake.
volt_add_test(metrics_server_tests
    SOURCES metrics_server_test.cpp
    LINK volt::trace volt::pal_posix
)

# The exposition is checked by a script that shares no code with the exporter.
# Both paths are handed to the test rather than guessed, so it works from any
# build directory.
volt_add_test(metrics_exposition_tests
    SOURCES exposition_validation_test.cpp
    LINK volt::trace volt::pal_posix
)

target_compile_definitions(metrics_exposition_tests PRIVATE
    VOLT_METRICS_VALIDATOR="${CMAKE_SOURCE_DIR}/tools/metrics_validate/validate_prometheus.py"
    VOLT_METRICS_SAMPLE_PATH="${CMAKE_CURRENT_BINARY_DIR}/volt_sample_metrics.txt"
    VOLT_METRICS_BROKEN_PATH="${CMAKE_CURRENT_BINARY_DIR}/volt_broken_metrics.txt"
)
