volt_add_test(log_tests
    SOURCES
        log_test.cpp
        log_rotation_test.cpp
    LINK volt::log volt::pal_sim
)

# Its own target because it drives eight real threads through eight million
# records: a load test does not belong in the same binary as the fast unit
# tests, and it needs longer than the default per-test limit.
volt_add_test(log_concurrency_tests
    SOURCES log_concurrency_test.cpp
    LINK volt::log volt::pal_posix
)

# Measures the producer path, so it needs the real clock and the real threads.
volt_add_test(log_benchmark_tests
    SOURCES log_benchmark_test.cpp
    LINK volt::log volt::pal_posix
)
