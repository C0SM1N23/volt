volt_add_test(sched_tests
    SOURCES
        hdr_histogram_test.cpp
        schedule_checker_test.cpp
        schedule_generator_test.cpp
        rm_priority_test.cpp
        task_spec_test.cpp
    LINK volt::sched
)

# Real threads, real timers, real priorities (or their documented fallback):
# the behaviour campaign runs against the POSIX backend and sleeps real time.
# The table is judged by a script that shares no code with the generator.
# Both paths are handed to the test rather than guessed, so it works from any
# build directory.
volt_add_test(sched_validator_tests
    SOURCES schedule_validator_test.cpp
    LINK volt::sched volt::pal_posix
)
target_compile_definitions(sched_validator_tests PRIVATE
    VOLT_SCHEDULE_VALIDATOR="${CMAKE_SOURCE_DIR}/tools/tt_schedule_gen/validate_schedule.py"
    VOLT_SCHEDULE_SCRATCH="${CMAKE_CURRENT_BINARY_DIR}/volt_schedule")

# The time-triggered and deadline classes on the real backend: real timers,
# real threads, and the SPEC 38 phase alignment measured rather than assumed.
volt_add_test(sched_classes_tests
    SOURCES sched_classes_test.cpp
    LINK volt::sched volt::pal_posix
    TIMEOUT 120
)

volt_add_test(sched_scheduler_tests
    SOURCES sched_scheduler_test.cpp
    LINK volt::sched volt::pal_posix
    TIMEOUT 120
)

# Both drift claims: the exact virtual hour at K2 scale and ten real seconds
# against the kernel timer.
volt_add_test(sched_drift_tests
    SOURCES sched_drift_test.cpp
    LINK volt::sched volt::pal_posix volt::pal_sim
    TIMEOUT 120
)

# One task set under all three classes, for the table in
# docs/PERFORMANCE.md. Skipped under instrumentation.
volt_add_test(sched_comparison_tests
    SOURCES sched_comparison_test.cpp
    LINK volt::sched volt::pal_posix
    TIMEOUT 120
)

# K1 measured on the real backend; skipped under instrumentation.
volt_add_test(sched_benchmark_tests
    SOURCES sched_benchmark_test.cpp
    LINK volt::sched volt::pal_posix
    TIMEOUT 120
)
