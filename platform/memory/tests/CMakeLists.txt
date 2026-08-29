find_package(rapidcheck CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)

volt_add_test(memory_tests
    SOURCES memory_test.cpp
    LINK volt::memory
)

volt_add_test(memory_property_tests
    SOURCES fixed_pool_property_test.cpp
    LINK volt::memory rapidcheck
)

# Ubuntu's packaged RapidCheck still instantiates std::aligned_storage, which
# C++23 deprecates. Keep that third-party warning local to the only target that
# instantiates RapidCheck templates; every VOLT source retains the full warning set.
target_compile_options(memory_property_tests PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wno-deprecated-declarations>
)

volt_add_test(memory_queue_model_tests
    SOURCES queue_model_test.cpp
    LINK volt::memory
)

volt_add_test(memory_queue_concurrency_tests
    SOURCES queue_concurrency_test.cpp
    LINK volt::memory volt::pal_posix
)

# Not a ctest case: it reports timings rather than asserting on them, and the
# numbers it produces are published in docs/PERFORMANCE.md. It still carries the
# project warning, sanitizer and coverage flags so it links in every preset.
add_executable(memory_benchmarks memory_benchmark.cpp)
target_compile_features(memory_benchmarks PRIVATE cxx_std_23)
volt_set_warnings(memory_benchmarks)
volt_set_sanitizers(memory_benchmarks)
volt_set_coverage(memory_benchmarks)
target_link_libraries(memory_benchmarks PRIVATE volt::memory volt::pal_posix benchmark::benchmark)
