volt_add_test(sched_tests
    SOURCES
        hdr_histogram_test.cpp
        rm_priority_test.cpp
        task_spec_test.cpp
    LINK volt::sched
)

# Real threads, real timers, real priorities (or their documented fallback):
# the behaviour campaign runs against the POSIX backend and sleeps real time.
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

# K1 measured on the real backend; skipped under instrumentation.
volt_add_test(sched_benchmark_tests
    SOURCES sched_benchmark_test.cpp
    LINK volt::sched volt::pal_posix
    TIMEOUT 120
)
