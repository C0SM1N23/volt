volt_add_test(ipc_tests
    SOURCES
        free_list_test.cpp
        index_ring_test.cpp
        topic_state_test.cpp
        topic_test.cpp
        control_channel_test.cpp
    LINK volt::ipc volt::pal_sim volt::pal_posix
)

# Its own target because it drives real threads through two million
# handovers; a load campaign does not share a binary with fast unit suites.
volt_add_test(ipc_concurrency_tests
    SOURCES ipc_concurrency_test.cpp
    LINK volt::ipc volt::pal_posix
    TIMEOUT 120
)

# The kill -9 recovery campaign needs a victim: a real process that opens
# the topic, hoards samples and dies without destructors.
add_executable(volt_ipc_crash_consumer crash_consumer_main.cpp)
target_compile_features(volt_ipc_crash_consumer PRIVATE cxx_std_23)
volt_set_warnings(volt_ipc_crash_consumer)
volt_set_sanitizers(volt_ipc_crash_consumer)
volt_set_coverage(volt_ipc_crash_consumer)
target_link_libraries(volt_ipc_crash_consumer PRIVATE volt::ipc volt::pal_posix)

volt_add_test(ipc_crash_tests
    SOURCES ipc_crash_test.cpp
    LINK volt::ipc volt::pal_posix
)
target_compile_definitions(ipc_crash_tests PRIVATE
    VOLT_IPC_CRASH_CONSUMER="$<TARGET_FILE:volt_ipc_crash_consumer>")
add_dependencies(ipc_crash_tests volt_ipc_crash_consumer)

# Real time on a real scheduler: the 1 kHz cadence test means nothing on a
# virtual clock, so it runs only against the POSIX backend.
volt_add_test(ipc_slow_consumer_tests
    SOURCES ipc_slow_consumer_test.cpp
    LINK volt::ipc volt::pal_posix
    TIMEOUT 120
)

# Zero-copy is proven, not assumed: every memcpy the compiler emits or the
# code calls is routed through a counter and the hot window must not move it.
# The builtin is disabled so the compiler cannot hide small copies from the
# linker, and the wrap only exists on this target.
volt_add_test(ipc_zero_copy_tests
    SOURCES ipc_zero_copy_test.cpp
    LINK volt::ipc volt::pal_posix
)
target_compile_options(ipc_zero_copy_tests PRIVATE -fno-builtin-memcpy)
target_link_options(ipc_zero_copy_tests PRIVATE -Wl,--wrap=memcpy)

# The K3 campaign: every SPEC 10.1 mechanism, same payload, same method.
# Real threads and real sockets; skipped under instrumentation because a
# sanitizer measures itself.
volt_add_test(ipc_benchmark_tests
    SOURCES ipc_benchmark_test.cpp
    LINK volt::ipc volt::memory volt::pal_posix
    TIMEOUT 300
)
